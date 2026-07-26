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
#include <functiondiscoverykeys_devpkey.h>

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

// Per-Client Channel Mode Enum
enum ClientChannelMode { CLIENT_MODE_STEREO = 0, CLIENT_MODE_LEFT = 1, CLIENT_MODE_RIGHT = 2 };
std::atomic<ClientChannelMode> g_client1Channel{CLIENT_MODE_LEFT};
std::atomic<ClientChannelMode> g_client2Channel{CLIENT_MODE_RIGHT};
std::atomic<int> g_openDropdown{0}; // 0 = closed, 1 = Client 1 menu open, 2 = Client 2 menu open, 3 = PC Audio Device menu open

template <class T> void SafeRelease(T **ppT) {
    if (*ppT) {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

struct AudioDeviceInfo {
    std::string id;
    std::string name;
};
std::vector<AudioDeviceInfo> g_audioDevices;
std::atomic<int> g_selectedDeviceIndex{0}; // 0 = Default
std::mutex g_deviceMutex;

void EnumerateAudioDevices() {
    std::lock_guard<std::mutex> lock(g_deviceMutex);
    g_audioDevices.clear();
    g_audioDevices.push_back({ "", "Default Playback Device" });

    IMMDeviceEnumerator* pEnumerator = NULL;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator))) {
        IMMDeviceCollection* pCollection = NULL;
        if (SUCCEEDED(pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection))) {
            UINT count = 0;
            pCollection->GetCount(&count);
            for (UINT i = 0; i < count; ++i) {
                IMMDevice* pDevice = NULL;
                if (SUCCEEDED(pCollection->Item(i, &pDevice))) {
                    LPWSTR pstrID = NULL;
                    pDevice->GetId(&pstrID);

                    IPropertyStore* pProps = NULL;
                    std::string friendlyName = "Audio Device";
                    if (SUCCEEDED(pDevice->OpenPropertyStore(STGM_READ, &pProps))) {
                        PROPVARIANT varName;
                        PropVariantInit(&varName);
                        if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &varName))) {
                            if (varName.vt == VT_LPWSTR && varName.pwszVal) {
                                char nameBuf[256] = {0};
                                WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, nameBuf, sizeof(nameBuf), NULL, NULL);
                                friendlyName = nameBuf;
                            }
                            PropVariantClear(&varName);
                        }
                        SafeRelease(&pProps);
                    }

                    if (pstrID) {
                        char idBuf[512] = {0};
                        WideCharToMultiByte(CP_UTF8, 0, pstrID, -1, idBuf, sizeof(idBuf), NULL, NULL);
                        g_audioDevices.push_back({ idBuf, friendlyName });
                        CoTaskMemFree(pstrID);
                    }
                    SafeRelease(&pDevice);
                }
            }
            SafeRelease(&pCollection);
        }
        SafeRelease(&pEnumerator);
    }
}

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
std::atomic<float> g_gainL{100.0f};        // 0% to 100% (Left Volume)
std::atomic<float> g_gainR{100.0f};        // 0% to 100% (Right Volume)
std::atomic<bool> g_isMuted{false};
std::atomic<bool> g_isMutedL{false};
std::atomic<bool> g_isMutedR{false};

std::atomic<float> g_rmsL{0.0f};
std::atomic<float> g_rmsR{0.0f};

std::atomic<bool> g_isTestAudioPlaying{false};
std::atomic<DWORD> g_testAudioStartTime{0};

std::string g_client1IpStr = "None";
std::string g_client2IpStr = "None";
std::string g_localIpsStr = "";

WAVEFORMATEX *g_pwfx = nullptr;
std::mutex g_formatMutex;

HWND g_hwndMain = NULL;
HWND g_hChkStartup = NULL;
NOTIFYICONDATA g_nid = {};

HFONT g_hFontTitle = NULL;
HFONT g_hFontBold  = NULL;
HFONT g_hFontSub   = NULL;
HFONT g_hFontBtn   = NULL;

void InitFonts() {
    if (!g_hFontTitle) g_hFontTitle = CreateFontA(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    if (!g_hFontBold)  g_hFontBold  = CreateFontA(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    if (!g_hFontSub)   g_hFontSub   = CreateFontA(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    if (!g_hFontBtn)   g_hFontBtn   = CreateFontA(12, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
}

void CleanupFonts() {
    if (g_hFontTitle) DeleteObject(g_hFontTitle);
    if (g_hFontBold)  DeleteObject(g_hFontBold);
    if (g_hFontSub)   DeleteObject(g_hFontSub);
    if (g_hFontBtn)   DeleteObject(g_hFontBtn);
}

enum DragTarget { DRAG_NONE, DRAG_MASTER, DRAG_GAIN_L, DRAG_GAIN_R };
DragTarget g_activeDrag = DRAG_NONE;

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

        g_openDropdown.store(0);
        if (index == 0) {
            g_client1IpStr = g_client2IpStr;
            g_client1Channel.store(g_client2Channel.load());
            g_client2IpStr = "None";
            g_client2Channel.store(CLIENT_MODE_RIGHT);
        } else {
            g_client2IpStr = "None";
            g_client2Channel.store(CLIENT_MODE_RIGHT);
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
            int sndBufSize = 64 * 1024;
            setsockopt(clientSocket, SOL_SOCKET, SO_SNDBUF, (const char*)&sndBufSize, sizeof(sndBufSize));
            setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&optVal, sizeof(optVal));
            setsockopt(clientSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optVal, sizeof(optVal));

            u_long nonBlockingMode = 1;
            ioctlsocket(clientSocket, FIONBIO, &nonBlockingMode);

            g_clientSockets.push_back(clientSocket);
            const char* initModeStr = "STEREO";
            if (g_clientSockets.size() == 1) {
                g_client1IpStr = clientIp;
                g_client1Channel.store(CLIENT_MODE_LEFT);
                initModeStr = "LEFT";
            } else if (g_clientSockets.size() == 2) {
                g_client2IpStr = clientIp;
                g_client2Channel.store(CLIENT_MODE_RIGHT);
                initModeStr = "RIGHT";
            }

            // Send initial 32-byte format handshake
            {
                std::lock_guard<std::mutex> formatLock(g_formatMutex);
                if (g_pwfx) {
                    char formatHeader[33] = {0};
                    snprintf(formatHeader, sizeof(formatHeader), "FORMAT|%u|%u|%u|%s", g_pwfx->nSamplesPerSec, g_pwfx->nChannels, g_pwfx->wBitsPerSample, initModeStr);
                    size_t headerLen = strlen(formatHeader);
                    for (size_t i = headerLen; i < 32; ++i) formatHeader[i] = ' ';
                    formatHeader[32] = '\0';
                    send(clientSocket, formatHeader, 32, 0);
                }
            }
        }
        if (g_hwndMain) InvalidateRect(g_hwndMain, NULL, FALSE);
    }
}

// Memory-Optimized Zero-Allocation Audio Packet Sender
bool SendAudioPacketWithTag(SOCKET sock, uint32_t modeTag, const char* pAudioData, int audioBytes) {
    thread_local static std::vector<char> s_sendPacketBuffer;
    size_t totalSize = 8 + audioBytes;
    if (s_sendPacketBuffer.size() < totalSize) {
        s_sendPacketBuffer.resize(totalSize);
    }

    uint32_t netTag = htonl(modeTag);
    uint32_t netLen = htonl((uint32_t)audioBytes);

    memcpy(s_sendPacketBuffer.data(), &netTag, 4);
    memcpy(s_sendPacketBuffer.data() + 4, &netLen, 4);
    memcpy(s_sendPacketBuffer.data() + 8, pAudioData, audioBytes);

    int sent = send(sock, s_sendPacketBuffer.data(), (int)totalSize, 0);
    if (sent == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
        return false;
    }
    return true;
}

inline float ReadSampleAsFloat(const BYTE* pFrame, UINT32 channelIndex, UINT32 bitsPerSample, bool isFloatFormat) {
    if (isFloatFormat && bitsPerSample == 32) {
        const float* pFloats = (const float*)pFrame;
        return pFloats[channelIndex];
    } else if (bitsPerSample == 16) {
        const int16_t* pInt16 = (const int16_t*)pFrame;
        return pInt16[channelIndex] / 32768.0f;
    } else if (bitsPerSample == 24) {
        const BYTE* pSample = pFrame + (channelIndex * 3);
        int32_t val = (int32_t)((pSample[2] << 16) | (pSample[1] << 8) | pSample[0]);
        if (val & 0x800000) val |= 0xFF000000;
        return val / 8388608.0f;
    } else if (bitsPerSample == 32 && !isFloatFormat) {
        const int32_t* pInt32 = (const int32_t*)pFrame;
        return pInt32[channelIndex] / 2147483648.0f;
    }
    return 0.0f;
}

// WASAPI Audio Loop with 24/7 Infinite Auto-Recovery Loop
void WasapiAudioLoop() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    CoInitialize(NULL);
    timeBeginPeriod(1);

    static std::vector<float> s_buf1;
    static std::vector<float> s_buf2;
    static std::vector<float> s_bufStereo;

    while (g_running) {
        IMMDeviceEnumerator *pEnumerator = NULL;
        IMMDevice *pDevice = NULL;
        IAudioClient *pAudioClient = NULL;
        IAudioCaptureClient *pCaptureClient = NULL;

        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator))) { Sleep(1000); continue; }

        int chosenIdx = g_selectedDeviceIndex.load();
        std::string chosenId = "";
        {
            std::lock_guard<std::mutex> lock(g_deviceMutex);
            if (chosenIdx > 0 && chosenIdx < (int)g_audioDevices.size()) {
                chosenId = g_audioDevices[chosenIdx].id;
            }
        }

        if (chosenId.empty()) {
            if (FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice))) { SafeRelease(&pEnumerator); Sleep(1000); continue; }
        } else {
            wchar_t wId[512];
            MultiByteToWideChar(CP_UTF8, 0, chosenId.c_str(), -1, wId, 512);
            if (FAILED(pEnumerator->GetDevice(wId, &pDevice))) {
                if (FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice))) { SafeRelease(&pEnumerator); Sleep(1000); continue; }
            }
        }

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

                    bool isTestPlaying = g_isTestAudioPlaying.load();
                    DWORD nowTick = GetTickCount();
                    if (isTestPlaying) {
                        DWORD elapsed = nowTick - g_testAudioStartTime.load();
                        if (elapsed > 2000) {
                            g_isTestAudioPlaying.store(false);
                            isTestPlaying = false;
                        }
                    }

                    if (!g_serverActive.load() || (!isTestPlaying && (flags & AUDCLNT_BUFFERFLAGS_SILENT))) {
                        memset(pData, 0, bytesToRead);
                        g_rmsL.store(0.0f);
                        g_rmsR.store(0.0f);
                    } else if (g_pwfx && g_pwfx->nChannels >= 1) {
                        bool isFloatFormat = (g_pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
                        if (g_pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                            WAVEFORMATEXTENSIBLE* pExt = (WAVEFORMATEXTENSIBLE*)g_pwfx;
                            if (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
                                isFloatFormat = true;
                            }
                        }
                        UINT32 bitsPerSample = g_pwfx->wBitsPerSample;
                        UINT32 chCount = g_pwfx->nChannels;
                        UINT32 bytesPerFrame = g_pwfx->nBlockAlign;

                        float masterLinear = g_isMuted.load() ? 0.0f : (g_masterVolume.load() / 100.0f);
                        float gainLLinear  = g_isMutedL.load() ? 0.0f : (g_gainL.load() / 100.0f);
                        float gainRLinear  = g_isMutedR.load() ? 0.0f : (g_gainR.load() / 100.0f);

                        float volL = masterLinear * gainLLinear;
                        float volR = masterLinear * gainRLinear;

                        UINT32 totalSamples = numFramesAvailable * 2;
                        if (s_buf1.size() < totalSamples) s_buf1.resize(totalSamples);
                        if (s_buf2.size() < totalSamples) s_buf2.resize(totalSamples);
                        if (s_bufStereo.size() < totalSamples) s_bufStereo.resize(totalSamples);

                        float sumL = 0.0f, sumR = 0.0f;
                        const BYTE* pByteData = (const BYTE*)pData;

                        for (UINT32 i = 0; i < numFramesAvailable; ++i) {
                            const BYTE* pFrame = pByteData + (i * bytesPerFrame);

                            float rawL = 0.0f;
                            float rawR = 0.0f;

                            if (isTestPlaying) {
                                DWORD elapsed = nowTick - g_testAudioStartTime.load();
                                static float s_phase = 0.0f;
                                s_phase += 1.0f / (float)(g_pwfx->nSamplesPerSec ? g_pwfx->nSamplesPerSec : 48000);
                                if (elapsed < 1000) {
                                    rawL = 0.4f * sinf(2.0f * 3.14159265f * 440.0f * s_phase);
                                    rawR = 0.0f;
                                } else {
                                    rawL = 0.0f;
                                    rawR = 0.4f * sinf(2.0f * 3.14159265f * 880.0f * s_phase);
                                }
                            } else if (chCount == 1) {
                                rawL = ReadSampleAsFloat(pFrame, 0, bitsPerSample, isFloatFormat);
                                rawR = rawL;
                            } else {
                                float inL = ReadSampleAsFloat(pFrame, 0, bitsPerSample, isFloatFormat);
                                float inR = ReadSampleAsFloat(pFrame, 1, bitsPerSample, isFloatFormat);

                                // Strict Differential Crosstalk Cancellation Filter
                                // Eliminates Realtek Audio Console / Windows Sound Enhancements Crosstalk & Monofication
                                float diff = inL - inR;
                                float threshold = 0.05f;
                                if (fabsf(diff) > threshold) {
                                    if (diff > 0.0f) {
                                        rawL = diff;
                                        rawR = 0.0f;
                                    } else {
                                        rawL = 0.0f;
                                        rawR = -diff;
                                    }
                                } else {
                                    rawL = inL;
                                    rawR = inR;
                                }
                            }

                            float sampleL = rawL * volL;
                            float sampleR = rawR * volR;

                            if (sampleL > 1.0f) sampleL = 1.0f; else if (sampleL < -1.0f) sampleL = -1.0f;
                            if (sampleR > 1.0f) sampleR = 1.0f; else if (sampleR < -1.0f) sampleR = -1.0f;

                            sumL += sampleL * sampleL;
                            sumR += sampleR * sampleR;

                            s_buf1[i * 2 + 0] = sampleL;
                            s_buf1[i * 2 + 1] = sampleL;

                            s_buf2[i * 2 + 0] = sampleR;
                            s_buf2[i * 2 + 1] = sampleR;

                            s_bufStereo[i * 2 + 0] = sampleL;
                            s_bufStereo[i * 2 + 1] = sampleR;
                        }
                        g_rmsL.store(sqrtf(sumL / numFramesAvailable));
                        g_rmsR.store(sqrtf(sumR / numFramesAvailable));
                    }

                    // Broadcast Audio Frame Block
                    if (g_serverActive.load()) {
                        std::lock_guard<std::mutex> lock(g_clientMutex);
                        size_t clientCount = g_clientSockets.size();
                        if (clientCount > 0) {
                            int payloadBytes = (int)(numFramesAvailable * 2 * sizeof(float));

                            // Send Client 1
                            if (clientCount >= 1) {
                                ClientChannelMode ch1 = g_client1Channel.load();
                                uint32_t tag = (ch1 == CLIENT_MODE_LEFT) ? MODE_TAG_LEFT : (ch1 == CLIENT_MODE_RIGHT) ? MODE_TAG_RIGHT : MODE_TAG_STEREO;
                                const char* bufPtr = (ch1 == CLIENT_MODE_LEFT) ? (const char*)s_buf1.data() : (ch1 == CLIENT_MODE_RIGHT) ? (const char*)s_buf2.data() : (const char*)s_bufStereo.data();

                                bool ok1 = SendAudioPacketWithTag(g_clientSockets[0], tag, bufPtr, payloadBytes);
                                if (!ok1) {
                                    closesocket(g_clientSockets[0]);
                                    g_clientSockets.erase(g_clientSockets.begin());
                                    g_client1IpStr = g_client2IpStr;
                                    g_client1Channel.store(g_client2Channel.load());
                                    g_client2IpStr = "None";
                                    g_client2Channel.store(CLIENT_MODE_RIGHT);
                                    if (g_hwndMain) InvalidateRect(g_hwndMain, NULL, FALSE);
                                }
                            }

                            // Send Client 2
                            if (g_clientSockets.size() >= 2) {
                                ClientChannelMode ch2 = g_client2Channel.load();
                                uint32_t tag = (ch2 == CLIENT_MODE_LEFT) ? MODE_TAG_LEFT : (ch2 == CLIENT_MODE_RIGHT) ? MODE_TAG_RIGHT : MODE_TAG_STEREO;
                                const char* bufPtr = (ch2 == CLIENT_MODE_LEFT) ? (const char*)s_buf1.data() : (ch2 == CLIENT_MODE_RIGHT) ? (const char*)s_buf2.data() : (const char*)s_bufStereo.data();

                                bool ok2 = SendAudioPacketWithTag(g_clientSockets[1], tag, bufPtr, payloadBytes);
                                if (!ok2) {
                                    closesocket(g_clientSockets[1]);
                                    g_clientSockets.erase(g_clientSockets.begin() + 1);
                                    g_client2IpStr = "None";
                                    g_client2Channel.store(CLIENT_MODE_RIGHT);
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

    HFONT oldF = (HFONT)SelectObject(hdc, g_hFontBtn ? g_hFontBtn : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(hdc, textCol);
    SetBkMode(hdc, TRANSPARENT);

    DrawTextA(hdc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldF);
}

void HandleMousePos(HWND hwnd, int mx, int my, bool isClick) {
    int trackX1 = 145, trackX2 = 335;
    int trackW = trackX2 - trackX1;

    RECT btnToggleServer = { 380, 72, 480, 102 };
    RECT btnDeviceDropdown = { 150, 112, 480, 134 };

    RECT btnDropdownC1 = { 250, 164, 380, 182 };
    RECT btnKickClient1= { 390, 164, 475, 182 };

    RECT btnDropdownC2 = { 250, 186, 380, 204 };
    RECT btnKickClient2= { 390, 186, 475, 204 };

    RECT btnMasterMinus  = { 345, 388, 385, 410 };
    RECT btnMasterPlus   = { 390, 388, 430, 410 };

    RECT btnLeftMinus    = { 345, 426, 385, 448 };
    RECT btnLeftPlus     = { 390, 426, 430, 448 };

    RECT btnRightMinus   = { 345, 464, 385, 486 };
    RECT btnRightPlus    = { 390, 464, 430, 486 };

    RECT btnReset        = { 430, 356, 480, 376 };

    if (isClick) {
        POINT pt = { mx, my };

        // Handle open dropdown selection first
        int openMenu = g_openDropdown.load();
        if (openMenu == 1) {
            RECT rOpt1 = { 250, 194, 380, 212 };
            RECT rOpt2 = { 250, 214, 380, 232 };
            RECT rOpt3 = { 250, 234, 380, 252 };
            if (PtInRect(&rOpt1, pt)) {
                g_client1Channel.store(CLIENT_MODE_LEFT);
                g_openDropdown.store(0);
                InvalidateRect(hwnd, NULL, FALSE); return;
            }
            if (PtInRect(&rOpt2, pt)) {
                g_client1Channel.store(CLIENT_MODE_RIGHT);
                g_openDropdown.store(0);
                InvalidateRect(hwnd, NULL, FALSE); return;
            }
            if (PtInRect(&rOpt3, pt)) {
                g_client1Channel.store(CLIENT_MODE_STEREO);
                g_openDropdown.store(0);
                InvalidateRect(hwnd, NULL, FALSE); return;
            }
            g_openDropdown.store(0);
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (openMenu == 2) {
            RECT rOpt1 = { 250, 136, 380, 154 };
            RECT rOpt2 = { 250, 156, 380, 174 };
            RECT rOpt3 = { 250, 176, 380, 194 };
            if (PtInRect(&rOpt1, pt)) {
                g_client2Channel.store(CLIENT_MODE_LEFT);
                g_openDropdown.store(0);
                InvalidateRect(hwnd, NULL, FALSE); return;
            }
            if (PtInRect(&rOpt2, pt)) {
                g_client2Channel.store(CLIENT_MODE_RIGHT);
                g_openDropdown.store(0);
                InvalidateRect(hwnd, NULL, FALSE); return;
            }
            if (PtInRect(&rOpt3, pt)) {
                g_client2Channel.store(CLIENT_MODE_STEREO);
                g_openDropdown.store(0);
                InvalidateRect(hwnd, NULL, FALSE); return;
            }
            g_openDropdown.store(0);
            InvalidateRect(hwnd, NULL, FALSE);
        }

        RECT btnSwapLR = { 375, 152, 475, 170 };
        if (PtInRect(&btnSwapLR, pt)) {
            ClientChannelMode tmp = g_client1Channel.load();
            g_client1Channel.store(g_client2Channel.load());
            g_client2Channel.store(tmp);
            g_openDropdown.store(0);
            InvalidateRect(hwnd, NULL, FALSE); return;
        }

        RECT btnTestSound = { 375, 238, 475, 258 };
        if (PtInRect(&btnTestSound, pt)) {
            g_testAudioStartTime.store(GetTickCount());
            g_isTestAudioPlaying.store(true);
            g_openDropdown.store(0);
            InvalidateRect(hwnd, NULL, FALSE); return;
        } else if (openMenu == 3) {
            std::lock_guard<std::mutex> lock(g_deviceMutex);
            int itemY = 136;
            for (size_t k = 0; k < g_audioDevices.size() && k < 8; ++k) {
                RECT rOpt = { 145, itemY, 480, itemY + 20 };
                if (PtInRect(&rOpt, pt)) {
                    g_selectedDeviceIndex.store((int)k);
                    g_openDropdown.store(0);
                    InvalidateRect(hwnd, NULL, FALSE); return;
                }
                itemY += 20;
            }
            g_openDropdown.store(0);
            InvalidateRect(hwnd, NULL, FALSE);
        }

        if (PtInRect(&btnDeviceDropdown, pt)) {
            if (openMenu != 3) {
                EnumerateAudioDevices();
                g_openDropdown.store(3);
            } else {
                g_openDropdown.store(0);
            }
            InvalidateRect(hwnd, NULL, FALSE); return;
        }

        if (PtInRect(&btnToggleServer, pt)) {
            bool current = g_serverActive.load();
            g_serverActive.store(!current);
            if (!current == false) {
                std::lock_guard<std::mutex> lock(g_clientMutex);
                for (SOCKET s : g_clientSockets) closesocket(s);
                g_clientSockets.clear();
                g_client1IpStr = "None";
                g_client2IpStr = "None";
                g_client1Channel.store(CLIENT_MODE_LEFT);
                g_client2Channel.store(CLIENT_MODE_RIGHT);
                g_openDropdown.store(0);
            }
            InvalidateRect(hwnd, NULL, FALSE); return;
        }

        if (g_client1IpStr != "None") {
            if (PtInRect(&btnDropdownC1, pt)) {
                g_openDropdown.store((openMenu == 1) ? 0 : 1);
                InvalidateRect(hwnd, NULL, FALSE); return;
            }
            if (PtInRect(&btnKickClient1, pt)) {
                KickClient(0);
                return;
            }
        }

        if (g_client2IpStr != "None") {
            if (PtInRect(&btnDropdownC2, pt)) {
                g_openDropdown.store((openMenu == 2) ? 0 : 2);
                InvalidateRect(hwnd, NULL, FALSE); return;
            }
            if (PtInRect(&btnKickClient2, pt)) {
                KickClient(1);
                return;
            }
        }

        RECT btnMuteMaster = { 345, 388, 425, 410 };
        RECT btnMuteLeft   = { 345, 426, 425, 448 };
        RECT btnMuteRight  = { 345, 464, 425, 486 };

        if (PtInRect(&btnReset, pt)) {
            g_masterVolume.store(100.0f);
            g_gainL.store(100.0f);
            g_gainR.store(100.0f);
            g_isMuted.store(false);
            g_isMutedL.store(false);
            g_isMutedR.store(false);
            InvalidateRect(hwnd, NULL, FALSE); return;
        }

        if (PtInRect(&btnMuteMaster, pt)) {
            g_isMuted.store(!g_isMuted.load());
            InvalidateRect(hwnd, NULL, FALSE); return;
        }

        if (PtInRect(&btnMuteLeft, pt)) {
            g_isMutedL.store(!g_isMutedL.load());
            InvalidateRect(hwnd, NULL, FALSE); return;
        }

        if (PtInRect(&btnMuteRight, pt)) {
            g_isMutedR.store(!g_isMutedR.load());
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
            g_gainL.store(norm * 100.0f);
        } else if (g_activeDrag == DRAG_GAIN_R) {
            g_gainR.store(norm * 100.0f);
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
        RECT card1 = { 20, 60, rcClient.right - 20, 142 };
        FillRect(memDC, &card1, cardBrush);

        bool isActive = g_serverActive.load();

        HBRUSH dotBrush = CreateSolidBrush(isActive ? RGB(0, 230, 118) : RGB(255, 82, 82));
        HBRUSH oldB = (HBRUSH)SelectObject(memDC, dotBrush);
        HPEN nullPen = CreatePen(PS_NULL, 0, RGB(0,0,0));
        HPEN oldP = (HPEN)SelectObject(memDC, nullPen);
        Ellipse(memDC, 35, 76, 47, 88);
        SelectObject(memDC, oldB);
        SelectObject(memDC, oldP);
        DeleteObject(dotBrush);
        DeleteObject(nullPen);

        SelectObject(memDC, g_hFontBold ? g_hFontBold : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(255, 255, 255));
        
        std::string statusText = isActive ? "Server Status: RUNNING (Port 5000)" : "Server Status: STOPPED";
        TextOutA(memDC, 55, 72, statusText.c_str(), (int)statusText.length());

        SelectObject(memDC, g_hFontSub ? g_hFontSub : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(160, 175, 200));
        std::string ipLine = "Local IP: " + g_localIpsStr;
        TextOutA(memDC, 55, 92, ipLine.c_str(), (int)ipLine.length());

        // Device Label & Dropdown
        TextOutA(memDC, 35, 116, "PC Audio Device:", 16);
        std::string selectedDevName = "Default Playback Device";
        {
            std::lock_guard<std::mutex> lock(g_deviceMutex);
            int selIdx = g_selectedDeviceIndex.load();
            if (selIdx >= 0 && selIdx < (int)g_audioDevices.size()) {
                selectedDevName = g_audioDevices[selIdx].name;
            }
        }
        if (selectedDevName.length() > 34) selectedDevName = selectedDevName.substr(0, 31) + "...";
        selectedDevName += "  v";

        RECT btnDeviceDropdown = { 150, 112, 480, 134 };
        DrawPillButton(memDC, btnDeviceDropdown, selectedDevName.c_str(), RGB(34, 42, 60), RGB(0, 229, 255));

        RECT btnToggleServer = { 380, 68, 480, 96 };
        if (isActive) {
            DrawPillButton(memDC, btnToggleServer, "STOP SERVER", RGB(255, 82, 82), RGB(255, 255, 255));
        } else {
            DrawPillButton(memDC, btnToggleServer, "START SERVER", RGB(0, 230, 118), RGB(18, 22, 33));
        }

        // Active Clients Card
        RECT card2 = { 20, 150, rcClient.right - 20, 225 };
        FillRect(memDC, &card2, cardBrush);

        SelectObject(memDC, g_hFontBold ? g_hFontBold : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(0, 229, 255));
        TextOutA(memDC, 35, 156, "Active Clients", 14);

        if (g_clientSockets.size() >= 2) {
            RECT btnSwapLR = { 375, 152, 475, 170 };
            DrawPillButton(memDC, btnSwapLR, "Swap L/R", RGB(34, 42, 60), RGB(0, 229, 255));
        }

        SelectObject(memDC, g_hFontSub ? g_hFontSub : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(255, 255, 255));

        std::string c1Text = "Client #1: " + g_client1IpStr;
        std::string c2Text = "Client #2: " + g_client2IpStr;

        TextOutA(memDC, 35, 176, c1Text.c_str(), (int)c1Text.length());
        TextOutA(memDC, 35, 198, c2Text.c_str(), (int)c2Text.length());

        if (g_client1IpStr != "None") {
            RECT btnDropdownC1 = { 250, 174, 380, 192 };
            RECT btnKickClient1= { 390, 174, 475, 192 };

            ClientChannelMode ch1 = g_client1Channel.load();
            std::string labelC1 = (ch1 == CLIENT_MODE_LEFT) ? "Left (L)  v" : (ch1 == CLIENT_MODE_RIGHT) ? "Right (R)  v" : "Stereo (L+R)  v";

            DrawPillButton(memDC, btnDropdownC1, labelC1.c_str(), RGB(34, 42, 60), RGB(0, 229, 255));
            DrawPillButton(memDC, btnKickClient1, "Disconnect", RGB(255, 82, 82), RGB(255, 255, 255));
        }

        if (g_client2IpStr != "None") {
            RECT btnDropdownC2 = { 250, 196, 380, 214 };
            RECT btnKickClient2= { 390, 196, 475, 214 };

            ClientChannelMode ch2 = g_client2Channel.load();
            std::string labelC2 = (ch2 == CLIENT_MODE_LEFT) ? "Left (L)  v" : (ch2 == CLIENT_MODE_RIGHT) ? "Right (R)  v" : "Stereo (L+R)  v";

            DrawPillButton(memDC, btnDropdownC2, labelC2.c_str(), RGB(34, 42, 60), RGB(0, 229, 255));
            DrawPillButton(memDC, btnKickClient2, "Disconnect", RGB(255, 82, 82), RGB(255, 255, 255));
        }

        // Stereo Peak Audio Visualizer Meter Card
        RECT card3 = { 20, 230, rcClient.right - 20, 335 };
        FillRect(memDC, &card3, cardBrush);

        SelectObject(memDC, g_hFontBold ? g_hFontBold : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(255, 255, 255));
        TextOutA(memDC, 35, 242, "Live Stereo Audio Visualizer Meter", 34);

        RECT btnTestSound = { 375, 238, 475, 258 };
        bool isTesting = g_isTestAudioPlaying.load();
        DrawPillButton(memDC, btnTestSound, isTesting ? "Playing..." : "Test Sound", isTesting ? RGB(0, 230, 118) : RGB(34, 42, 60), isTesting ? RGB(18, 22, 33) : RGB(0, 229, 255));

        HBRUSH meterBg = CreateSolidBrush(RGB(18, 22, 33));
        RECT rBarL_Bg = { 70, 270, rcClient.right - 40, 288 };
        RECT rBarR_Bg = { 70, 300, rcClient.right - 40, 318 };
        FillRect(memDC, &rBarL_Bg, meterBg);
        FillRect(memDC, &rBarR_Bg, meterBg);
        DeleteObject(meterBg);

        SelectObject(memDC, g_hFontSub ? g_hFontSub : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
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

        SelectObject(memDC, g_hFontBold ? g_hFontBold : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(0, 229, 255));
        TextOutA(memDC, 35, 360, "Volume & Channel Gain Control", 29);

        RECT btnReset = { 430, 356, 480, 376 };
        DrawPillButton(memDC, btnReset, "Reset", RGB(34, 42, 60), RGB(255, 255, 255));

        SelectObject(memDC, g_hFontSub ? g_hFontSub : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(255, 255, 255));
        TextOutA(memDC, 35, 392, "Master Volume:", 14);
        TextOutA(memDC, 35, 430, "Left (L) Volume:", 16);
        TextOutA(memDC, 35, 468, "Right (R) Volume:", 17);

        // Draw active dropdown popup menu overlay at VERY END (Highest Z-Order)
        int openMenu = g_openDropdown.load();
        if (openMenu == 1 && g_client1IpStr != "None") {
            RECT rMenuBg = { 248, 194, 382, 258 };
            DrawRoundedRect(memDC, rMenuBg, RGB(18, 22, 33), 6);
            RECT rOpt1 = { 250, 196, 380, 214 };
            RECT rOpt2 = { 250, 216, 380, 234 };
            RECT rOpt3 = { 250, 236, 380, 254 };
            ClientChannelMode ch1 = g_client1Channel.load();
            DrawPillButton(memDC, rOpt1, "Left Channel (L)",  (ch1 == CLIENT_MODE_LEFT)   ? RGB(0, 229, 255) : RGB(34, 42, 60), (ch1 == CLIENT_MODE_LEFT)   ? RGB(18, 22, 33) : RGB(255, 255, 255));
            DrawPillButton(memDC, rOpt2, "Right Channel (R)", (ch1 == CLIENT_MODE_RIGHT)  ? RGB(0, 229, 255) : RGB(34, 42, 60), (ch1 == CLIENT_MODE_RIGHT)  ? RGB(18, 22, 33) : RGB(255, 255, 255));
            DrawPillButton(memDC, rOpt3, "Stereo (L+R)",      (ch1 == CLIENT_MODE_STEREO) ? RGB(0, 229, 255) : RGB(34, 42, 60), (ch1 == CLIENT_MODE_STEREO) ? RGB(18, 22, 33) : RGB(255, 255, 255));
        } else if (openMenu == 2 && g_client2IpStr != "None") {
            RECT rMenuBg = { 248, 130, 382, 194 };
            DrawRoundedRect(memDC, rMenuBg, RGB(18, 22, 33), 6);
            RECT rOpt1 = { 250, 132, 380, 150 };
            RECT rOpt2 = { 250, 152, 380, 170 };
            RECT rOpt3 = { 250, 172, 380, 190 };
            ClientChannelMode ch2 = g_client2Channel.load();
            DrawPillButton(memDC, rOpt1, "Left Channel (L)",  (ch2 == CLIENT_MODE_LEFT)   ? RGB(0, 229, 255) : RGB(34, 42, 60), (ch2 == CLIENT_MODE_LEFT)   ? RGB(18, 22, 33) : RGB(255, 255, 255));
            DrawPillButton(memDC, rOpt2, "Right Channel (R)", (ch2 == CLIENT_MODE_RIGHT)  ? RGB(0, 229, 255) : RGB(34, 42, 60), (ch2 == CLIENT_MODE_RIGHT)  ? RGB(18, 22, 33) : RGB(255, 255, 255));
            DrawPillButton(memDC, rOpt3, "Stereo (L+R)",      (ch2 == CLIENT_MODE_STEREO) ? RGB(0, 229, 255) : RGB(34, 42, 60), (ch2 == CLIENT_MODE_STEREO) ? RGB(18, 22, 33) : RGB(255, 255, 255));
        } else if (openMenu == 3) {
            std::lock_guard<std::mutex> lock(g_deviceMutex);
            int count = (int)g_audioDevices.size();
            if (count > 8) count = 8;
            RECT rMenuBg = { 148, 134, 482, 136 + count * 20 };
            DrawRoundedRect(memDC, rMenuBg, RGB(18, 22, 33), 6);
            int itemY = 136;
            int selIdx = g_selectedDeviceIndex.load();
            for (size_t k = 0; k < g_audioDevices.size() && k < 8; ++k) {
                RECT rOpt = { 150, itemY, 480, itemY + 18 };
                std::string devName = g_audioDevices[k].name;
                if (devName.length() > 34) devName = devName.substr(0, 31) + "...";
                DrawPillButton(memDC, rOpt, devName.c_str(), (selIdx == (int)k) ? RGB(0, 229, 255) : RGB(34, 42, 60), (selIdx == (int)k) ? RGB(18, 22, 33) : RGB(255, 255, 255));
                itemY += 20;
            }
        }

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

        float gLNorm = std::max(0.0f, std::min(1.0f, g_gainL.load() / 100.0f));
        RECT rTrackL_Bg = { trackX1, 434, trackX2, 440 };
        DrawRoundedRect(memDC, rTrackL_Bg, RGB(24, 30, 44), 6);
        if (gLNorm > 0.0f) {
            RECT rTrackL_Fill = { trackX1, 434, trackX1 + (int)(gLNorm * trackW), 440 };
            DrawRoundedRect(memDC, rTrackL_Fill, RGB(0, 230, 118), 6);
        }
        int kxL = trackX1 + (int)(gLNorm * trackW);
        RECT rKnobL = { kxL - 7, 430, kxL + 7, 444 };
        DrawRoundedRect(memDC, rKnobL, RGB(255, 255, 255), 14);

        float gRNorm = std::max(0.0f, std::min(1.0f, g_gainR.load() / 100.0f));
        RECT rTrackR_Bg = { trackX1, 472, trackX2, 478 };
        DrawRoundedRect(memDC, rTrackR_Bg, RGB(24, 30, 44), 6);
        if (gRNorm > 0.0f) {
            RECT rTrackR_Fill = { trackX1, 472, trackX1 + (int)(gRNorm * trackW), 478 };
            DrawRoundedRect(memDC, rTrackR_Fill, RGB(0, 230, 118), 6);
        }
        int kxR = trackX1 + (int)(gRNorm * trackW);
        RECT rKnobR = { kxR - 7, 468, kxR + 7, 482 };
        DrawRoundedRect(memDC, rKnobR, RGB(255, 255, 255), 14);

        RECT btnMuteMaster = { 345, 388, 425, 410 };
        RECT btnMuteLeft   = { 345, 426, 425, 448 };
        RECT btnMuteRight  = { 345, 464, 425, 486 };

        bool isMutedM = g_isMuted.load();
        bool isMutedL = g_isMutedL.load();
        bool isMutedR = g_isMutedR.load();

        DrawPillButton(memDC, btnMuteMaster, isMutedM ? "Muted" : "Mute", isMutedM ? RGB(255, 82, 82) : RGB(34, 42, 60), isMutedM ? RGB(255, 255, 255) : RGB(0, 229, 255));
        DrawPillButton(memDC, btnMuteLeft,   isMutedL ? "Muted" : "Mute", isMutedL ? RGB(255, 82, 82) : RGB(34, 42, 60), isMutedL ? RGB(255, 255, 255) : RGB(0, 230, 118));
        DrawPillButton(memDC, btnMuteRight,  isMutedR ? "Muted" : "Mute", isMutedR ? RGB(255, 82, 82) : RGB(34, 42, 60), isMutedR ? RGB(255, 255, 255) : RGB(0, 230, 118));

        char strMaster[16], strL[16], strR[16];
        snprintf(strMaster, sizeof(strMaster), "%d%%", (int)g_masterVolume.load());
        snprintf(strL, sizeof(strL), "%d%%", (int)g_gainL.load());
        snprintf(strR, sizeof(strR), "%d%%", (int)g_gainR.load());

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
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    CoInitialize(NULL);
    InitFonts();
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return -1;

    FetchLocalIPAddresses();
    EnumerateAudioDevices();

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
    CleanupFonts();

    return (int)msg.wParam;
}