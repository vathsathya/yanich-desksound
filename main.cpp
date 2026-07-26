#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <iostream>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <string>
#include <cmath>
#include <algorithm>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <timeapi.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "mmdevapi.lib")
#pragma comment(lib, "Winmm.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Advapi32.lib")

#define PORT 5000
#define DISCOVERY_PORT 5001
#define WM_TRAYICON (WM_USER + 1)
#define ID_CHK_STARTUP 1020

// Server Channel Mode Enum
enum ServerChannelMode { MODE_AUTO_SYNC, MODE_SWAP_LR, MODE_FORCE_STEREO };
std::atomic<ServerChannelMode> g_serverChannelMode{MODE_AUTO_SYNC};

// Frame Header Channel Mode Tag (0 = STEREO, 1 = LEFT, 2 = RIGHT)
#define MODE_TAG_STEREO 0
#define MODE_TAG_LEFT   1
#define MODE_TAG_RIGHT  2

// Global Server State
std::vector<SOCKET> g_clientSockets;
std::mutex g_clientMutex;
std::atomic<bool> g_running{true};
std::atomic<bool> g_serverActive{true};

std::atomic<float> g_masterVolume{100.0f}; // 0% to 100%
std::atomic<float> g_gainL{0.0f};         // -10dB to +10dB
std::atomic<float> g_gainR{0.0f};         // -10dB to +10dB
std::atomic<bool> g_isMuted{false};

std::atomic<float> g_rmsL{0.0f};
std::atomic<float> g_rmsR{0.0f};

std::string g_client1IpStr = "None";
std::string g_client2IpStr = "None";
std::string g_localIpsStr = "";

WAVEFORMATEX *g_pwfx = nullptr;
std::mutex g_formatMutex;

HWND g_hwndMain = NULL;
HWND g_hChkStartup = NULL;
NOTIFYICONDATA g_nid = {};

enum DragTarget { DRAG_NONE, DRAG_MASTER, DRAG_GAIN_L, DRAG_GAIN_R };
DragTarget g_activeDrag = DRAG_NONE;

template <class T> void SafeRelease(T **ppT) {
    if (*ppT) {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

bool IsRunOnStartupEnabled() {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char path[MAX_PATH];
        DWORD size = sizeof(path);
        LONG res = RegQueryValueExA(hKey, "YanichDeskSound", NULL, NULL, (LPBYTE)path, &size);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return false;
}

void SetRunOnStartup(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            char exePath[MAX_PATH];
            GetModuleFileNameA(NULL, exePath, MAX_PATH);
            std::string cmd = "\"" + std::string(exePath) + "\" -silent";
            RegSetValueExA(hKey, "YanichDeskSound", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)(cmd.length() + 1));
        } else {
            RegDeleteValueA(hKey, "YanichDeskSound");
        }
        RegCloseKey(hKey);
    }
}

bool IsPrivateLocalIP(const sockaddr_in& addr) {
    const unsigned char* ip = (const unsigned char*)&(addr.sin_addr.s_addr);
    if (ip[0] == 127) return true;
    if (ip[0] == 10) return true;
    if (ip[0] == 172 && (ip[1] >= 16 && ip[1] <= 31)) return true;
    if (ip[0] == 192 && ip[1] == 168) return true;
    return false;
}

void UdpDiscoveryThread() {
    SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) return;

    int optval = 1;
    setsockopt(udpSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));

    sockaddr_in discoveryAddr{};
    discoveryAddr.sin_family = AF_INET;
    discoveryAddr.sin_addr.s_addr = INADDR_ANY;
    discoveryAddr.sin_port = htons(DISCOVERY_PORT);

    if (bind(udpSocket, (sockaddr*)&discoveryAddr, sizeof(discoveryAddr)) == ERROR_SUCCESS) {
        char recvBuf[256];
        sockaddr_in clientAddr{};
        int clientAddrLen = sizeof(clientAddr);

        while (g_running) {
            int bytesRead = recvfrom(udpSocket, recvBuf, sizeof(recvBuf) - 1, 0, (sockaddr*)&clientAddr, &clientAddrLen);
            if (bytesRead > 0) {
                if (!g_serverActive.load() || !IsPrivateLocalIP(clientAddr)) continue;
                recvBuf[bytesRead] = '\0';
                if (strstr(recvBuf, "DESKSOUND_DISCOVER") != NULL) {
                    const char* replyMsg = "DESKSOUND_SERVER|5000";
                    sendto(udpSocket, replyMsg, (int)strlen(replyMsg), 0, (sockaddr*)&clientAddr, clientAddrLen);
                }
            }
        }
    }
    closesocket(udpSocket);
}

void FetchLocalIPAddresses() {
    char hostname[256];
    g_localIpsStr = "";
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        if (getaddrinfo(hostname, nullptr, &hints, &res) == 0) {
            for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
                sockaddr_in* ipv4 = (sockaddr_in*)p->ai_addr;
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(ipv4->sin_addr), ipStr, sizeof(ipStr));
                if (std::string(ipStr) != "127.0.0.1" && IsPrivateLocalIP(*ipv4)) {
                    if (!g_localIpsStr.empty()) g_localIpsStr += " | ";
                    g_localIpsStr += ipStr;
                }
            }
            freeaddrinfo(res);
        }
    }
    if (g_localIpsStr.empty()) g_localIpsStr = "127.0.0.1";
}

void KickClient(int index) {
    std::lock_guard<std::mutex> lock(g_clientMutex);
    if (index >= 0 && (size_t)index < g_clientSockets.size()) {
        closesocket(g_clientSockets[index]);
        g_clientSockets.erase(g_clientSockets.begin() + index);

        if (index == 0) {
            g_client1IpStr = g_client2IpStr;
            g_client2IpStr = "None";
        } else {
            g_client2IpStr = "None";
        }
    }
    if (g_hwndMain) InvalidateRect(g_hwndMain, NULL, FALSE);
}

void AcceptClientsThread(SOCKET listenSocket) {
    while (g_running) {
        sockaddr_in clientAddr{};
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &clientAddrLen);

        if (clientSocket == INVALID_SOCKET) {
            if (!g_running) break;
            continue;
        }

        if (!g_serverActive.load()) {
            closesocket(clientSocket);
            continue;
        }

        char clientIp[INET_ADDRSTRLEN] = "Client";
        inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIp, sizeof(clientIp));

        if (!IsPrivateLocalIP(clientAddr)) {
            closesocket(clientSocket);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(g_clientMutex);
            if (g_clientSockets.size() >= 2) {
                closesocket(clientSocket);
                continue;
            }

            int optVal = 1;
            setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&optVal, sizeof(optVal));
            setsockopt(clientSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optVal, sizeof(optVal));

            u_long nonBlockingMode = 1;
            ioctlsocket(clientSocket, FIONBIO, &nonBlockingMode);

            // Send initial 32-byte format handshake
            {
                std::lock_guard<std::mutex> formatLock(g_formatMutex);
                if (g_pwfx) {
                    char formatHeader[33] = {0};
                    snprintf(formatHeader, sizeof(formatHeader), "FORMAT|%u|%u|%u", g_pwfx->nSamplesPerSec, g_pwfx->nChannels, g_pwfx->wBitsPerSample);
                    size_t headerLen = strlen(formatHeader);
                    for (size_t i = headerLen; i < 32; ++i) formatHeader[i] = ' ';
                    formatHeader[32] = '\0';
                    send(clientSocket, formatHeader, 32, 0);
                }
            }

            g_clientSockets.push_back(clientSocket);
            if (g_clientSockets.size() == 1) {
                g_client1IpStr = clientIp;
            } else if (g_clientSockets.size() == 2) {
                g_client2IpStr = clientIp;
            }
        }
        if (g_hwndMain) InvalidateRect(g_hwndMain, NULL, FALSE);
    }
}

// Memory-Optimized Zero-Allocation Audio Packet Sender
bool SendAudioPacketWithTag(SOCKET sock, uint32_t modeTag, const char* pAudioData, int audioBytes) {
    thread_local static std::vector<char> s_sendPacketBuffer;
    size_t totalSize = 4 + audioBytes;
    if (s_sendPacketBuffer.size() < totalSize) {
        s_sendPacketBuffer.resize(totalSize);
    }

    uint32_t netTag = htonl(modeTag);
    memcpy(s_sendPacketBuffer.data(), &netTag, 4);
    memcpy(s_sendPacketBuffer.data() + 4, pAudioData, audioBytes);

    int sent = send(sock, s_sendPacketBuffer.data(), (int)totalSize, 0);
    if (sent == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
        return false;
    }
    return true;
}

// WASAPI Audio Loop with 24/7 Infinite Auto-Recovery Loop
void WasapiAudioLoop() {
    CoInitialize(NULL);
    timeBeginPeriod(1);

    static std::vector<float> s_buf1;
    static std::vector<float> s_buf2;

    while (g_running) {
        IMMDeviceEnumerator *pEnumerator = NULL;
        IMMDevice *pDevice = NULL;
        IAudioClient *pAudioClient = NULL;
        IAudioCaptureClient *pCaptureClient = NULL;

        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator))) { Sleep(1000); continue; }
        if (FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice))) { SafeRelease(&pEnumerator); Sleep(1000); continue; }
        if (FAILED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient))) { SafeRelease(&pDevice); SafeRelease(&pEnumerator); Sleep(1000); continue; }

        {
            std::lock_guard<std::mutex> formatLock(g_formatMutex);
            if (g_pwfx) { CoTaskMemFree(g_pwfx); g_pwfx = nullptr; }
            if (FAILED(pAudioClient->GetMixFormat(&g_pwfx))) { SafeRelease(&pAudioClient); SafeRelease(&pDevice); SafeRelease(&pEnumerator); Sleep(1000); continue; }
        }

        if (FAILED(pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, g_pwfx, NULL))) { SafeRelease(&pAudioClient); SafeRelease(&pDevice); SafeRelease(&pEnumerator); Sleep(1000); continue; }
        if (FAILED(pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCaptureClient))) { SafeRelease(&pAudioClient); SafeRelease(&pDevice); SafeRelease(&pEnumerator); Sleep(1000); continue; }
        if (FAILED(pAudioClient->Start())) { SafeRelease(&pCaptureClient); SafeRelease(&pAudioClient); SafeRelease(&pDevice); SafeRelease(&pEnumerator); Sleep(1000); continue; }

        BYTE *pData;
        UINT32 numFramesAvailable;
        DWORD flags;

        while (g_running) {
            Sleep(2);
            if (FAILED(pCaptureClient->GetNextPacketSize(&numFramesAvailable))) break; // Device lost/changed -> auto recovery

            while (numFramesAvailable > 0 && g_running) {
                if (SUCCEEDED(pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL)) && numFramesAvailable > 0) {
                    UINT32 bytesToRead = numFramesAvailable * g_pwfx->nBlockAlign;

                    if (!g_serverActive.load() || (flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                        memset(pData, 0, bytesToRead);
                        g_rmsL.store(0.0f);
                        g_rmsR.store(0.0f);
                    } else if (g_pwfx->wBitsPerSample == 32 && g_pwfx->nChannels == 2) {
                        float masterLinear = g_isMuted.load() ? 0.0f : (g_masterVolume.load() / 100.0f);
                        float gainLLinear = powf(10.0f, g_gainL.load() / 20.0f);
                        float gainRLinear = powf(10.0f, g_gainR.load() / 20.0f);

                        float volL = masterLinear * gainLLinear;
                        float volR = masterLinear * gainRLinear;

                        float* pFloatData = (float*)pData;
                        float sumL = 0.0f, sumR = 0.0f;

                        for (UINT32 i = 0; i < numFramesAvailable; ++i) {
                            pFloatData[i * 2 + 0] *= volL;
                            pFloatData[i * 2 + 1] *= volR;

                            if (pFloatData[i * 2 + 0] > 1.0f) pFloatData[i * 2 + 0] = 1.0f;
                            else if (pFloatData[i * 2 + 0] < -1.0f) pFloatData[i * 2 + 0] = -1.0f;

                            if (pFloatData[i * 2 + 1] > 1.0f) pFloatData[i * 2 + 1] = 1.0f;
                            else if (pFloatData[i * 2 + 1] < -1.0f) pFloatData[i * 2 + 1] = -1.0f;

                            sumL += pFloatData[i * 2 + 0] * pFloatData[i * 2 + 0];
                            sumR += pFloatData[i * 2 + 1] * pFloatData[i * 2 + 1];
                        }
                        g_rmsL.store(sqrtf(sumL / numFramesAvailable));
                        g_rmsR.store(sqrtf(sumR / numFramesAvailable));
                    }

                    // Broadcast Audio Frame Block
                    if (g_serverActive.load()) {
                        std::lock_guard<std::mutex> lock(g_clientMutex);
                        size_t clientCount = g_clientSockets.size();
                        ServerChannelMode mode = g_serverChannelMode.load();

                        if (clientCount == 1 || mode == MODE_FORCE_STEREO) {
                            for (auto it = g_clientSockets.begin(); it != g_clientSockets.end(); ) {
                                if (!SendAudioPacketWithTag(*it, MODE_TAG_STEREO, (const char*)pData, bytesToRead)) {
                                    closesocket(*it);
                                    it = g_clientSockets.erase(it);
                                    g_client1IpStr = g_clientSockets.size() > 0 ? g_client2IpStr : "None";
                                    g_client2IpStr = "None";
                                    if (g_hwndMain) InvalidateRect(g_hwndMain, NULL, FALSE);
                                } else {
                                    ++it;
                                }
                            }
                        } else if (clientCount >= 2 && g_pwfx->wBitsPerSample == 32) {
                            UINT32 totalSamples = numFramesAvailable * 2;
                            if (s_buf1.size() < totalSamples) s_buf1.resize(totalSamples);
                            if (s_buf2.size() < totalSamples) s_buf2.resize(totalSamples);

                            float* pFloatData = (float*)pData;
                            uint32_t tag1 = MODE_TAG_LEFT;
                            uint32_t tag2 = MODE_TAG_RIGHT;

                            for (UINT32 i = 0; i < numFramesAvailable; ++i) {
                                float sL = pFloatData[i * 2 + 0];
                                float sR = pFloatData[i * 2 + 1];

                                if (mode == MODE_SWAP_LR) {
                                    tag1 = MODE_TAG_RIGHT;
                                    tag2 = MODE_TAG_LEFT;
                                    s_buf1[i * 2 + 0] = sR; s_buf1[i * 2 + 1] = sR;
                                    s_buf2[i * 2 + 0] = sL; s_buf2[i * 2 + 1] = sL;
                                } else {
                                    tag1 = MODE_TAG_LEFT;
                                    tag2 = MODE_TAG_RIGHT;
                                    s_buf1[i * 2 + 0] = sL; s_buf1[i * 2 + 1] = sL;
                                    s_buf2[i * 2 + 0] = sR; s_buf2[i * 2 + 1] = sR;
                                }
                            }

                            if (!SendAudioPacketWithTag(g_clientSockets[0], tag1, (const char*)s_buf1.data(), bytesToRead)) {
                                closesocket(g_clientSockets[0]);
                                g_clientSockets.erase(g_clientSockets.begin());
                                g_client1IpStr = g_client2IpStr;
                                g_client2IpStr = "None";
                                if (g_hwndMain) InvalidateRect(g_hwndMain, NULL, FALSE);
                            }

                            if (g_clientSockets.size() >= 2) {
                                if (!SendAudioPacketWithTag(g_clientSockets[1], tag2, (const char*)s_buf2.data(), bytesToRead)) {
                                    closesocket(g_clientSockets[1]);
                                    g_clientSockets.erase(g_clientSockets.begin() + 1);
                                    g_client2IpStr = "None";
                                    if (g_hwndMain) InvalidateRect(g_hwndMain, NULL, FALSE);
                                }
                            }
                        }
                    }
                }

                pCaptureClient->ReleaseBuffer(numFramesAvailable);
                pCaptureClient->GetNextPacketSize(&numFramesAvailable);
            }
        }

        if (pAudioClient) pAudioClient->Stop();
        SafeRelease(&pCaptureClient);
        SafeRelease(&pAudioClient);
        SafeRelease(&pDevice);
        SafeRelease(&pEnumerator);
        Sleep(500);
    }

    timeEndPeriod(1);
    CoUninitialize();
}

void DrawRoundedRect(HDC hdc, RECT rect, COLORREF color, int radius) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_NULL, 0, RGB(0, 0, 0));
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);

    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void DrawPillButton(HDC hdc, RECT rect, const char* label, COLORREF bgCol = RGB(34, 42, 60), COLORREF textCol = RGB(0, 229, 255)) {
    DrawRoundedRect(hdc, rect, bgCol, 8);

    HFONT hFontBtn = CreateFontA(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    HFONT oldF = (HFONT)SelectObject(hdc, hFontBtn);
    SetTextColor(hdc, textCol);
    SetBkMode(hdc, TRANSPARENT);

    DrawTextA(hdc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldF);
    DeleteObject(hFontBtn);
}

void HandleMousePos(HWND hwnd, int mx, int my, bool isClick) {
    int trackX1 = 145, trackX2 = 335;
    int trackW = trackX2 - trackX1;

    RECT btnToggleServer = { 380, 72, 480, 102 };

    RECT btnModeAuto  = { 220, 140, 300, 160 };
    RECT btnModeSwap  = { 308, 140, 385, 160 };
    RECT btnModeStereo= { 393, 140, 480, 160 };

    RECT btnKickClient1 = { 400, 164, 480, 182 };
    RECT btnKickClient2 = { 400, 186, 480, 204 };

    RECT btnMasterMinus  = { 345, 388, 385, 410 };
    RECT btnMasterPlus   = { 390, 388, 430, 410 };

    RECT btnLeftMinus    = { 345, 426, 385, 448 };
    RECT btnLeftPlus     = { 390, 426, 430, 448 };

    RECT btnRightMinus   = { 345, 464, 385, 486 };
    RECT btnRightPlus    = { 390, 464, 430, 486 };

    RECT btnReset        = { 430, 356, 480, 376 };

    if (isClick) {
        POINT pt = { mx, my };

        if (PtInRect(&btnToggleServer, pt)) {
            bool current = g_serverActive.load();
            g_serverActive.store(!current);
            if (!current == false) {
                std::lock_guard<std::mutex> lock(g_clientMutex);
                for (SOCKET s : g_clientSockets) closesocket(s);
                g_clientSockets.clear();
                g_client1IpStr = "None";
                g_client2IpStr = "None";
            }
            InvalidateRect(hwnd, NULL, FALSE); return;
        }

        if (g_client1IpStr != "None" && PtInRect(&btnKickClient1, pt)) {
            KickClient(0);
            return;
        }
        if (g_client2IpStr != "None" && PtInRect(&btnKickClient2, pt)) {
            KickClient(1);
            return;
        }

        if (PtInRect(&btnModeAuto, pt)) {
            g_serverChannelMode.store(MODE_AUTO_SYNC);
            InvalidateRect(hwnd, NULL, FALSE); return;
        }
        if (PtInRect(&btnModeSwap, pt)) {
            g_serverChannelMode.store(MODE_SWAP_LR);
            InvalidateRect(hwnd, NULL, FALSE); return;
        }
        if (PtInRect(&btnModeStereo, pt)) {
            g_serverChannelMode.store(MODE_FORCE_STEREO);
            InvalidateRect(hwnd, NULL, FALSE); return;
        }

        if (PtInRect(&btnReset, pt)) {
            g_masterVolume.store(100.0f);
            g_gainL.store(0.0f);
            g_gainR.store(0.0f);
            InvalidateRect(hwnd, NULL, FALSE); return;
        }

        if (PtInRect(&btnMasterMinus, pt)) {
            float v = g_masterVolume.load() - 10.0f;
            g_masterVolume.store(std::max(0.0f, v));
            InvalidateRect(hwnd, NULL, FALSE); return;
        }
        if (PtInRect(&btnMasterPlus, pt)) {
            float v = g_masterVolume.load() + 10.0f;
            g_masterVolume.store(std::min(100.0f, v));
            InvalidateRect(hwnd, NULL, FALSE); return;
        }

        if (PtInRect(&btnLeftMinus, pt)) {
            float v = g_gainL.load() - 2.0f;
            g_gainL.store(std::max(-10.0f, v));
            InvalidateRect(hwnd, NULL, FALSE); return;
        }
        if (PtInRect(&btnLeftPlus, pt)) {
            float v = g_gainL.load() + 2.0f;
            g_gainL.store(std::min(10.0f, v));
            InvalidateRect(hwnd, NULL, FALSE); return;
        }

        if (PtInRect(&btnRightMinus, pt)) {
            float v = g_gainR.load() - 2.0f;
            g_gainR.store(std::max(-10.0f, v));
            InvalidateRect(hwnd, NULL, FALSE); return;
        }
        if (PtInRect(&btnRightPlus, pt)) {
            float v = g_gainR.load() + 2.0f;
            g_gainR.store(std::min(10.0f, v));
            InvalidateRect(hwnd, NULL, FALSE); return;
        }

        if (my >= 380 && my <= 412) g_activeDrag = DRAG_MASTER;
        else if (my >= 418 && my <= 450) g_activeDrag = DRAG_GAIN_L;
        else if (my >= 456 && my <= 488) g_activeDrag = DRAG_GAIN_R;
    }

    if (g_activeDrag != DRAG_NONE) {
        float norm = (float)(mx - trackX1) / (float)trackW;
        norm = std::max(0.0f, std::min(1.0f, norm));

        if (g_activeDrag == DRAG_MASTER) {
            g_masterVolume.store(norm * 100.0f);
        } else if (g_activeDrag == DRAG_GAIN_L) {
            g_gainL.store(-10.0f + norm * 20.0f);
        } else if (g_activeDrag == DRAG_GAIN_R) {
            g_gainR.store(-10.0f + norm * 20.0f);
        }
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        SetTimer(hwnd, 1, 30, NULL);
        
        g_nid.cbSize = sizeof(NOTIFYICONDATA);
        g_nid.hWnd = hwnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;
        g_nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(101));
        lstrcpy(g_nid.szTip, TEXT("Yanich DeskSound Server"));
        Shell_NotifyIcon(NIM_ADD, &g_nid);

        bool startupChecked = IsRunOnStartupEnabled();
        g_hChkStartup = CreateWindowExA(0, "BUTTON", "Run on Windows Startup", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 515, 180, 20, hwnd, (HMENU)ID_CHK_STARTUP, GetModuleHandle(NULL), NULL);

        HFONT hFontSub = CreateFontA(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        SendMessage(g_hChkStartup, WM_SETFONT, (WPARAM)hFontSub, TRUE);
        SendMessage(g_hChkStartup, BM_SETCHECK, startupChecked ? BST_CHECKED : BST_UNCHECKED, 0);
        break;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == ID_CHK_STARTUP) {
            LRESULT chkState = SendMessage(g_hChkStartup, BM_GETCHECK, 0, 0);
            SetRunOnStartup(chkState == BST_CHECKED);
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        SetCapture(hwnd);
        int mx = LOWORD(lParam);
        int my = HIWORD(lParam);
        HandleMousePos(hwnd, mx, my, true);
        break;
    }

    case WM_MOUSEMOVE: {
        if (wParam & MK_LBUTTON) {
            int mx = LOWORD(lParam);
            int my = HIWORD(lParam);
            HandleMousePos(hwnd, mx, my, false);
        }
        break;
    }

    case WM_LBUTTONUP: {
        ReleaseCapture();
        g_activeDrag = DRAG_NONE;
        break;
    }

    case WM_TIMER: {
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    }

    case WM_TRAYICON: {
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        break;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rcClient;
        GetClientRect(hwnd, &rcClient);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        HBRUSH bgBrush = CreateSolidBrush(RGB(18, 22, 33));
        FillRect(memDC, &rcClient, bgBrush);
        DeleteObject(bgBrush);

        SetBkMode(memDC, TRANSPARENT);

        HFONT hFontTitle = CreateFontA(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(memDC, hFontTitle);
        SetTextColor(memDC, RGB(255, 255, 255));
        TextOutA(memDC, 20, 20, "Yanich DeskSound Server", 23);
        SelectObject(memDC, oldFont);
        DeleteObject(hFontTitle);

        // Server Status Card
        HBRUSH cardBrush = CreateSolidBrush(RGB(28, 34, 48));
        RECT card1 = { 20, 60, rcClient.right - 20, 115 };
        FillRect(memDC, &card1, cardBrush);

        bool isActive = g_serverActive.load();

        HBRUSH dotBrush = CreateSolidBrush(isActive ? RGB(0, 230, 118) : RGB(255, 82, 82));
        HBRUSH oldB = (HBRUSH)SelectObject(memDC, dotBrush);
        HPEN nullPen = CreatePen(PS_NULL, 0, RGB(0,0,0));
        HPEN oldP = (HPEN)SelectObject(memDC, nullPen);
        Ellipse(memDC, 35, 80, 47, 92);
        SelectObject(memDC, oldB);
        SelectObject(memDC, oldP);
        DeleteObject(dotBrush);
        DeleteObject(nullPen);

        HFONT hFontBold = CreateFontA(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        SelectObject(memDC, hFontBold);
        SetTextColor(memDC, RGB(255, 255, 255));
        
        std::string statusText = isActive ? "Server Status: RUNNING (Port 5000)" : "Server Status: STOPPED";
        TextOutA(memDC, 55, 76, statusText.c_str(), (int)statusText.length());

        HFONT hFontSub = CreateFontA(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        SelectObject(memDC, hFontSub);
        SetTextColor(memDC, RGB(160, 175, 200));
        std::string ipLine = "Local IP: " + g_localIpsStr;
        TextOutA(memDC, 55, 94, ipLine.c_str(), (int)ipLine.length());

        RECT btnToggleServer = { 380, 72, 480, 102 };
        if (isActive) {
            DrawPillButton(memDC, btnToggleServer, "STOP SERVER", RGB(255, 82, 82), RGB(255, 255, 255));
        } else {
            DrawPillButton(memDC, btnToggleServer, "START SERVER", RGB(0, 230, 118), RGB(18, 22, 33));
        }

        // Active Clients Card
        RECT card2 = { 20, 130, rcClient.right - 20, 215 };
        FillRect(memDC, &card2, cardBrush);

        SelectObject(memDC, hFontBold);
        SetTextColor(memDC, RGB(0, 229, 255));
        TextOutA(memDC, 35, 142, "Active Clients", 14);

        ServerChannelMode curMode = g_serverChannelMode.load();
        RECT btnModeAuto  = { 220, 140, 300, 160 };
        RECT btnModeSwap  = { 308, 140, 385, 160 };
        RECT btnModeStereo= { 393, 140, 480, 160 };

        DrawPillButton(memDC, btnModeAuto,  "Auto Sync",  (curMode == MODE_AUTO_SYNC) ? RGB(0, 229, 255) : RGB(34, 42, 60), (curMode == MODE_AUTO_SYNC) ? RGB(18, 22, 33) : RGB(255, 255, 255));
        DrawPillButton(memDC, btnModeSwap,  "Swap L ⇄ R", (curMode == MODE_SWAP_LR)   ? RGB(0, 229, 255) : RGB(34, 42, 60), (curMode == MODE_SWAP_LR)   ? RGB(18, 22, 33) : RGB(255, 255, 255));
        DrawPillButton(memDC, btnModeStereo,"Force Stereo",(curMode == MODE_FORCE_STEREO)?RGB(0, 229, 255): RGB(34, 42, 60), (curMode == MODE_FORCE_STEREO)?RGB(18, 22, 33): RGB(255, 255, 255));

        SelectObject(memDC, hFontSub);
        SetTextColor(memDC, RGB(255, 255, 255));

        size_t cCount = 0;
        {
            std::lock_guard<std::mutex> lock(g_clientMutex);
            cCount = g_clientSockets.size();
        }

        std::string c1Text = "Client #1: " + g_client1IpStr;
        std::string c2Text = "Client #2: " + g_client2IpStr;

        if (cCount == 1 || curMode == MODE_FORCE_STEREO) {
            if (g_client1IpStr != "None") c1Text += " [Stereo L+R]";
            if (g_client2IpStr != "None") c2Text += " [Stereo L+R]";
        } else if (curMode == MODE_SWAP_LR) {
            if (g_client1IpStr != "None") c1Text += " [Right Channel R]";
            if (g_client2IpStr != "None") c2Text += " [Left Channel L]";
        } else { // AUTO_SYNC
            if (g_client1IpStr != "None") c1Text += " [Left Channel L]";
            if (g_client2IpStr != "None") c2Text += " [Right Channel R]";
        }

        TextOutA(memDC, 35, 166, c1Text.c_str(), (int)c1Text.length());
        TextOutA(memDC, 35, 188, c2Text.c_str(), (int)c2Text.length());

        if (g_client1IpStr != "None") {
            RECT btnKickClient1 = { 400, 164, 480, 182 };
            DrawPillButton(memDC, btnKickClient1, "Disconnect", RGB(255, 82, 82), RGB(255, 255, 255));
        }

        if (g_client2IpStr != "None") {
            RECT btnKickClient2 = { 400, 186, 480, 204 };
            DrawPillButton(memDC, btnKickClient2, "Disconnect", RGB(255, 82, 82), RGB(255, 255, 255));
        }

        // Stereo Peak Audio Visualizer Meter Card
        RECT card3 = { 20, 230, rcClient.right - 20, 335 };
        FillRect(memDC, &card3, cardBrush);

        SelectObject(memDC, hFontBold);
        SetTextColor(memDC, RGB(255, 255, 255));
        TextOutA(memDC, 35, 242, "Live Stereo Audio Visualizer Meter", 34);

        HBRUSH meterBg = CreateSolidBrush(RGB(18, 22, 33));
        RECT rBarL_Bg = { 70, 270, rcClient.right - 40, 288 };
        RECT rBarR_Bg = { 70, 300, rcClient.right - 40, 318 };
        FillRect(memDC, &rBarL_Bg, meterBg);
        FillRect(memDC, &rBarR_Bg, meterBg);
        DeleteObject(meterBg);

        SelectObject(memDC, hFontSub);
        SetTextColor(memDC, RGB(160, 175, 200));
        TextOutA(memDC, 35, 272, "L:", 2);
        TextOutA(memDC, 35, 302, "R:", 2);

        float rmsL = g_rmsL.load();
        float rmsR = g_rmsR.load();
        int maxBarW = (rcClient.right - 40) - 70;
        int barW_L = (int)(rmsL * 3.0f * maxBarW);
        int barW_R = (int)(rmsR * 3.0f * maxBarW);
        if (barW_L > maxBarW) barW_L = maxBarW;
        if (barW_R > maxBarW) barW_R = maxBarW;

        HBRUSH meterFill = CreateSolidBrush(RGB(0, 229, 255));
        if (barW_L > 0) {
            RECT rBarL = { 70, 270, 70 + barW_L, 288 };
            FillRect(memDC, &rBarL, meterFill);
        }
        if (barW_R > 0) {
            RECT rBarR = { 70, 300, 70 + barW_R, 318 };
            FillRect(memDC, &rBarR, meterFill);
        }
        DeleteObject(meterFill);

        // Volume & Channel Gain Control Card
        RECT card4 = { 20, 350, rcClient.right - 20, 505 };
        FillRect(memDC, &card4, cardBrush);

        SelectObject(memDC, hFontBold);
        SetTextColor(memDC, RGB(0, 229, 255));
        TextOutA(memDC, 35, 360, "Volume & Channel Gain Control", 29);

        RECT btnReset = { 430, 356, 480, 376 };
        DrawPillButton(memDC, btnReset, "Reset", RGB(34, 42, 60), RGB(255, 255, 255));

        SelectObject(memDC, hFontSub);
        SetTextColor(memDC, RGB(255, 255, 255));
        TextOutA(memDC, 35, 392, "Master Volume:", 14);
        TextOutA(memDC, 35, 430, "Left (L) Gain:", 14);
        TextOutA(memDC, 35, 468, "Right (R) Gain:", 15);

        int trackX1 = 145, trackX2 = 335;
        int trackW = trackX2 - trackX1;

        float mNorm = std::max(0.0f, std::min(1.0f, g_masterVolume.load() / 100.0f));
        RECT rTrackM_Bg = { trackX1, 396, trackX2, 402 };
        DrawRoundedRect(memDC, rTrackM_Bg, RGB(24, 30, 44), 6);
        if (mNorm > 0.0f) {
            RECT rTrackM_Fill = { trackX1, 396, trackX1 + (int)(mNorm * trackW), 402 };
            DrawRoundedRect(memDC, rTrackM_Fill, RGB(0, 229, 255), 6);
        }
        int kxM = trackX1 + (int)(mNorm * trackW);
        RECT rKnobM = { kxM - 7, 392, kxM + 7, 406 };
        DrawRoundedRect(memDC, rKnobM, RGB(255, 255, 255), 14);

        float gLNorm = std::max(0.0f, std::min(1.0f, (g_gainL.load() + 10.0f) / 20.0f));
        RECT rTrackL_Bg = { trackX1, 434, trackX2, 440 };
        DrawRoundedRect(memDC, rTrackL_Bg, RGB(24, 30, 44), 6);
        if (gLNorm > 0.0f) {
            RECT rTrackL_Fill = { trackX1, 434, trackX1 + (int)(gLNorm * trackW), 440 };
            DrawRoundedRect(memDC, rTrackL_Fill, RGB(0, 230, 118), 6);
        }
        int kxL = trackX1 + (int)(gLNorm * trackW);
        RECT rKnobL = { kxL - 7, 430, kxL + 7, 444 };
        DrawRoundedRect(memDC, rKnobL, RGB(255, 255, 255), 14);

        float gRNorm = std::max(0.0f, std::min(1.0f, (g_gainR.load() + 10.0f) / 20.0f));
        RECT rTrackR_Bg = { trackX1, 472, trackX2, 478 };
        DrawRoundedRect(memDC, rTrackR_Bg, RGB(24, 30, 44), 6);
        if (gRNorm > 0.0f) {
            RECT rTrackR_Fill = { trackX1, 472, trackX1 + (int)(gRNorm * trackW), 478 };
            DrawRoundedRect(memDC, rTrackR_Fill, RGB(0, 230, 118), 6);
        }
        int kxR = trackX1 + (int)(gRNorm * trackW);
        RECT rKnobR = { kxR - 7, 468, kxR + 7, 482 };
        DrawRoundedRect(memDC, rKnobR, RGB(255, 255, 255), 14);

        RECT btnMasterMinus = { 345, 388, 385, 410 };
        RECT btnMasterPlus  = { 390, 388, 430, 410 };
        DrawPillButton(memDC, btnMasterMinus, "-10");
        DrawPillButton(memDC, btnMasterPlus,  "+10");

        RECT btnLeftMinus   = { 345, 426, 385, 448 };
        RECT btnLeftPlus    = { 390, 426, 430, 448 };
        DrawPillButton(memDC, btnLeftMinus, "-2dB", RGB(34, 42, 60), RGB(255, 255, 255));
        DrawPillButton(memDC, btnLeftPlus,  "+2dB", RGB(34, 42, 60), RGB(255, 255, 255));

        RECT btnRightMinus  = { 345, 464, 385, 486 };
        RECT btnRightPlus   = { 390, 464, 430, 486 };
        DrawPillButton(memDC, btnRightMinus, "-2dB", RGB(34, 42, 60), RGB(255, 255, 255));
        DrawPillButton(memDC, btnRightPlus,  "+2dB", RGB(34, 42, 60), RGB(255, 255, 255));

        char strMaster[16], strL[16], strR[16];
        snprintf(strMaster, sizeof(strMaster), "%d%%", (int)g_masterVolume.load());
        snprintf(strL, sizeof(strL), "%+.1fdB", g_gainL.load());
        snprintf(strR, sizeof(strR), "%+.1fdB", g_gainR.load());

        SetTextColor(memDC, RGB(0, 230, 118));
        TextOutA(memDC, rcClient.right - 65, 392, strMaster, (int)strlen(strMaster));
        TextOutA(memDC, rcClient.right - 65, 430, strL, (int)strlen(strL));
        TextOutA(memDC, rcClient.right - 65, 468, strR, (int)strlen(strR));

        HFONT hFontFooter = CreateFontA(14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
        SelectObject(memDC, hFontFooter);
        SetTextColor(memDC, RGB(0, 229, 255));
        TextOutA(memDC, rcClient.right - 185, 516, "Created by Vath Sathya", 22);
        DeleteObject(hFontFooter);

        DeleteObject(cardBrush);
        DeleteObject(hFontBold);
        DeleteObject(hFontSub);

        BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, RGB(160, 175, 200));
        SetBkColor(hdcStatic, RGB(18, 22, 33));
        static HBRUSH hbrBg = CreateSolidBrush(RGB(18, 22, 33));
        return (INT_PTR)hbrBg;
    }

    case WM_DESTROY: {
        g_running = false;
        Shell_NotifyIcon(NIM_DELETE, &g_nid);
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return -1;

    FetchLocalIPAddresses();

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        WSACleanup();
        return -1;
    }

    int optval = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR || listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listenSocket);
        WSACleanup();
        return -1;
    }

    std::thread udpThread(UdpDiscoveryThread);
    udpThread.detach();

    std::thread acceptThread(AcceptClientsThread, listenSocket);
    acceptThread.detach();

    std::thread audioThread(WasapiAudioLoop);
    audioThread.detach();

    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA), CS_CLASSDC, WndProc, 0L, 0L, hInstance, LoadIcon(hInstance, MAKEINTRESOURCE(101)), LoadCursor(NULL, IDC_ARROW), (HBRUSH)CreateSolidBrush(RGB(18, 22, 33)), NULL, "YanichDeskSoundGUIClass", NULL };
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(0, "YanichDeskSoundGUIClass", "Yanich DeskSound Server", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 100, 100, 520, 590, NULL, NULL, hInstance, NULL);
    g_hwndMain = hwnd;

    bool startSilent = (strstr(lpCmdLine, "-silent") != NULL || strstr(lpCmdLine, "-service") != NULL);
    ShowWindow(hwnd, startSilent ? SW_HIDE : nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_running = false;
    closesocket(listenSocket);
    WSACleanup();

    return (int)msg.wParam;
}