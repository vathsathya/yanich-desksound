#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
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
#include <winhttp.h>
#include <dwmapi.h>
#include "version.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "mmdevapi.lib")
#pragma comment(lib, "Winmm.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Gdi32.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "dwmapi.lib")

#define PORT 5000
#define DISCOVERY_PORT 5001
#define WM_TRAYICON (WM_USER + 1)
#define ID_CHK_STARTUP 1020

// Tray Menu Command IDs
#define ID_TRAY_SHOW 2001
#define ID_TRAY_TOGGLE_SERVER 2002
#define ID_TRAY_MUTE_MASTER 2003
#define ID_TRAY_EXIT 2004
#define ID_TRAY_UPDATE 2005

// Per-Client Channel Mode Enum
enum ClientChannelMode { CLIENT_MODE_STEREO = 0, CLIENT_MODE_LEFT = 1, CLIENT_MODE_RIGHT = 2 };
std::atomic<ClientChannelMode> g_client1Channel{CLIENT_MODE_LEFT};
std::atomic<ClientChannelMode> g_client2Channel{CLIENT_MODE_RIGHT};
std::atomic<int> g_openDropdown{0}; // 0 = closed, 1 = Client 1 menu open, 2 = Client 2 menu open, 3 = PC Audio Device menu open

std::atomic<bool> g_updateAvailable{false};
std::string g_latestUpdateTag = "";
std::string g_latestUpdateUrl = "https://github.com/vathsathya/yanich-desksound/releases/latest";
extern HWND g_hwndMain;



bool IsVersionNewer(const std::string& latestTag, const std::string& currentVersion) {
    int lMajor = 0, lMinor = 0, lPatch = 0;
    int cMajor = 0, cMinor = 0, cPatch = 0;

    auto parseVer = [](const std::string& v, int& maj, int& min, int& pat) {
        size_t start = v.find_first_of("0123456789");
        if (start != std::string::npos) {
            sscanf_s(v.c_str() + start, "%d.%d.%d", &maj, &min, &pat);
        }
    };
    parseVer(latestTag, lMajor, lMinor, lPatch);
    parseVer(currentVersion, cMajor, cMinor, cPatch);

    if (lMajor > cMajor) return true;
    if (lMajor < cMajor) return false;
    if (lMinor > cMinor) return true;
    if (lMinor < cMinor) return false;
    return lPatch > cPatch;
}

void CheckForUpdatesAsync() {
    std::thread([]() {
        HINTERNET hSession = WinHttpOpen(L"YanichDeskSound-Server/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return;

        HINTERNET hConnect = WinHttpConnect(hSession, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            return;
        }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/repos/vathsathya/yanich-desksound/releases/latest", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return;
        }

        const wchar_t* headers = L"User-Agent: YanichDeskSound-Server\r\nAccept: application/vnd.github+json\r\n";
        WinHttpAddRequestHeaders(hRequest, headers, (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD dwStatusCode = 0;
            DWORD dwSize = sizeof(dwStatusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);

            if (dwStatusCode == 200) {
                std::string responseStr;
                DWORD dwDownloaded = 0;
                do {
                    char buf[1024];
                    dwSize = sizeof(buf);
                    if (WinHttpReadData(hRequest, buf, dwSize, &dwDownloaded) && dwDownloaded > 0) {
                        responseStr.append(buf, dwDownloaded);
                    }
                } while (dwDownloaded > 0);

                size_t tagPos = responseStr.find("\"tag_name\":");
                if (tagPos != std::string::npos) {
                    size_t start = responseStr.find("\"", tagPos + 11);
                    if (start != std::string::npos) {
                        size_t end = responseStr.find("\"", start + 1);
                        if (end != std::string::npos) {
                            std::string tag = responseStr.substr(start + 1, end - start - 1);
                            if (IsVersionNewer(tag, APP_VERSION_TAG) && !tag.empty()) {
                                g_latestUpdateTag = tag;
                                g_updateAvailable.store(true);
                                if (g_hwndMain) {
                                    InvalidateRect(g_hwndMain, NULL, FALSE);
                                }
                            }
                        }
                    }
                }
            }
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
    }).detach();
}

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
std::atomic<bool> g_deviceChanged{false};
std::mutex g_deviceMutex;

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), NULL, 0);
    std::wstring wstr(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.length(), &wstr[0], count);
    return wstr;
}

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
std::atomic<float> g_rmsL_smooth{0.0f};
std::atomic<float> g_rmsR_smooth{0.0f};

std::atomic<bool> g_isTestAudioPlaying{false};
std::atomic<DWORD> g_testAudioStartTime{0};

std::string g_client1IpStr = "None";
std::string g_client2IpStr = "None";
std::string g_localIpsStr = "";

WAVEFORMATEX *g_pwfx = nullptr;
std::mutex g_formatMutex;

HWND g_hwndMain = NULL;
HWND g_hChkStartup = NULL;
NOTIFYICONDATAW g_nid = {};

HFONT g_hFontTitle  = NULL;
HFONT g_hFontBold   = NULL;
HFONT g_hFontSub    = NULL;
HFONT g_hFontBtn    = NULL;
HFONT g_hFontFooter = NULL;

HBRUSH g_hbrClassBg  = NULL;
HBRUSH g_hbrStaticBg = NULL;

void InitFonts() {
    if (!g_hFontTitle)  g_hFontTitle  = CreateFontW(-20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Leelawadee UI");
    if (!g_hFontBold)   g_hFontBold   = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Leelawadee UI");
    if (!g_hFontSub)    g_hFontSub    = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Leelawadee UI");
    if (!g_hFontBtn)    g_hFontBtn    = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Leelawadee UI");
    if (!g_hFontFooter) g_hFontFooter = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Leelawadee UI");
}

void CleanupFonts() {
    if (g_hFontTitle)  DeleteObject(g_hFontTitle);
    if (g_hFontBold)   DeleteObject(g_hFontBold);
    if (g_hFontSub)    DeleteObject(g_hFontSub);
    if (g_hFontBtn)    DeleteObject(g_hFontBtn);
    if (g_hFontFooter) DeleteObject(g_hFontFooter);
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

std::string GetConfigIniPath() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string path(exePath);
    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return path.substr(0, lastSlash + 1) + "desksound_server.ini";
    }
    return "desksound_server.ini";
}

void SaveServerConfig() {
    std::string ini = GetConfigIniPath();
    WritePrivateProfileStringA("Audio", "MasterVolume", std::to_string((int)g_masterVolume.load()).c_str(), ini.c_str());
    WritePrivateProfileStringA("Audio", "GainL", std::to_string((int)g_gainL.load()).c_str(), ini.c_str());
    WritePrivateProfileStringA("Audio", "GainR", std::to_string((int)g_gainR.load()).c_str(), ini.c_str());
    WritePrivateProfileStringA("Audio", "Muted", g_isMuted.load() ? "1" : "0", ini.c_str());
    WritePrivateProfileStringA("Audio", "MutedL", g_isMutedL.load() ? "1" : "0", ini.c_str());
    WritePrivateProfileStringA("Audio", "MutedR", g_isMutedR.load() ? "1" : "0", ini.c_str());
    WritePrivateProfileStringA("Audio", "SelectedDeviceIndex", std::to_string(g_selectedDeviceIndex.load()).c_str(), ini.c_str());
}

void LoadServerConfig() {
    std::string ini = GetConfigIniPath();
    int masterVol = GetPrivateProfileIntA("Audio", "MasterVolume", 100, ini.c_str());
    int gainL     = GetPrivateProfileIntA("Audio", "GainL", 100, ini.c_str());
    int gainR     = GetPrivateProfileIntA("Audio", "GainR", 100, ini.c_str());
    int muted     = GetPrivateProfileIntA("Audio", "Muted", 0, ini.c_str());
    int mutedL    = GetPrivateProfileIntA("Audio", "MutedL", 0, ini.c_str());
    int mutedR    = GetPrivateProfileIntA("Audio", "MutedR", 0, ini.c_str());
    int devIdx    = GetPrivateProfileIntA("Audio", "SelectedDeviceIndex", 0, ini.c_str());

    g_masterVolume.store((float)masterVol);
    g_gainL.store((float)gainL);
    g_gainR.store((float)gainR);
    g_isMuted.store(muted == 1);
    g_isMutedL.store(mutedL == 1);
    g_isMutedR.store(mutedR == 1);
    g_selectedDeviceIndex.store(devIdx);
}

void EnsureFirewallRulesExist() {
    std::thread([]() {
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};

        char cmdBuf[512];
        strcpy_s(cmdBuf, sizeof(cmdBuf), "netsh advfirewall firewall add rule name=\"Yanich DeskSound Server\" dir=in action=allow protocol=TCP localport=5000 profile=any");
        if (CreateProcessA(NULL, cmdBuf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 2000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }

        char cmdBuf2[512];
        strcpy_s(cmdBuf2, sizeof(cmdBuf2), "netsh advfirewall firewall add rule name=\"Yanich DeskSound Discovery\" dir=in action=allow protocol=UDP localport=5001 profile=any");
        if (CreateProcessA(NULL, cmdBuf2, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 2000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }).detach();
}

bool IsPrivateLocalIP(const sockaddr_in& addr) {
    const unsigned char* ip = (const unsigned char*)&(addr.sin_addr.s_addr);
    if (ip[0] == 127) return true; // Loopback / ADB Reverse
    if (ip[0] == 10) return true;  // Private Class A / USB Tethering
    if (ip[0] == 172 && (ip[1] >= 16 && ip[1] <= 31)) return true; // Private Class B
    if (ip[0] == 192 && ip[1] == 168) return true; // Private Class C
    if (ip[0] == 169 && ip[1] == 254) return true; // Link-Local / USB RNDIS
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

void AutoRunAdbReverseLoop() {
    std::string adbPath = "android-sdk\\platform-tools\\adb.exe";
    DWORD dwAttrib = GetFileAttributesA(adbPath.c_str());
    if (dwAttrib == INVALID_FILE_ATTRIBUTES || (dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) {
        adbPath = "adb.exe";
    }

    std::string adbCmd = "\"" + adbPath + "\" reverse tcp:5000 tcp:5000";

    while (g_running) {
        if (g_serverActive.load()) {
            STARTUPINFOA si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;

            PROCESS_INFORMATION pi{};
            char cmdBuf[MAX_PATH * 2];
            strcpy_s(cmdBuf, sizeof(cmdBuf), adbCmd.c_str());

            if (CreateProcessA(NULL, cmdBuf, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 1500);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(8));
    }
}

void FetchLocalIPAddresses() {
    char hostname[256];
    std::lock_guard<std::mutex> lock(g_clientMutex);
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
        if (g_clientSockets.size() == 1) {
            g_client1Channel.store(CLIENT_MODE_STEREO);
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
            int sndBufSize = 16 * 1024;
            setsockopt(clientSocket, SOL_SOCKET, SO_SNDBUF, (const char*)&sndBufSize, sizeof(sndBufSize));
            setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&optVal, sizeof(optVal));
            setsockopt(clientSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optVal, sizeof(optVal));
            int tos = 0x10;
            setsockopt(clientSocket, IPPROTO_IP, IP_TOS, (const char*)&tos, sizeof(tos));

            u_long nonBlockingMode = 1;
            ioctlsocket(clientSocket, FIONBIO, &nonBlockingMode);

            g_clientSockets.push_back(clientSocket);
            const char* initModeStr = "STEREO";
            if (g_clientSockets.size() == 1) {
                g_client1IpStr = clientIp;
                g_client1Channel.store(CLIENT_MODE_STEREO);
                initModeStr = "STEREO";
            } else if (g_clientSockets.size() == 2) {
                g_client1Channel.store(CLIENT_MODE_LEFT);
                g_client2IpStr = clientIp;
                g_client2Channel.store(CLIENT_MODE_RIGHT);
                initModeStr = "RIGHT";
            }

            // Send initial format handshake
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

// Robust Audio Packet Sender with Complete Socket Buffer Handling
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

    int totalSent = 0;
    int toSend = (int)totalSize;
    int retries = 0;

    while (totalSent < toSend) {
        int sent = send(sock, s_sendPacketBuffer.data() + totalSent, toSend - totalSent, 0);
        if (sent == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                if (totalSent == 0) {
                    // 0 bytes sent so far: safe to skip frame to avoid lag buildup
                    return true;
                }
                // Partial frame already sent: MUST finish sending remaining bytes to preserve TCP stream framing!
                if (++retries > 10) return false; // Stalled client socket -> disconnect
                Sleep(1);
                continue;
            }
            return false; // Socket error -> disconnect
        }
        if (sent == 0) return false;
        totalSent += sent;
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

// WASAPI Audio Loop with Hot Device Switching & Auto Recovery
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

            if (g_deviceChanged.load()) {
                g_deviceChanged.store(false);
                break; // Switch audio device immediately
            }

            if (FAILED(pCaptureClient->GetNextPacketSize(&numFramesAvailable))) break; // Device lost/changed -> auto recovery

            while (numFramesAvailable > 0 && g_running) {
                if (g_deviceChanged.load()) break;

                if (SUCCEEDED(pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL)) && numFramesAvailable > 0) {
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
                                rawL = ReadSampleAsFloat(pFrame, 0, bitsPerSample, isFloatFormat);
                                rawR = ReadSampleAsFloat(pFrame, 1, bitsPerSample, isFloatFormat);
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
                                    if (g_clientSockets.size() == 1) {
                                        g_client1Channel.store(CLIENT_MODE_STEREO);
                                    }
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
                                    if (g_clientSockets.size() == 1) {
                                        g_client1Channel.store(CLIENT_MODE_STEREO);
                                    }
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
        Sleep(300);
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

void DrawPillButtonW(HDC hdc, RECT rect, const wchar_t* label, COLORREF bgCol = RGB(34, 42, 60), COLORREF textCol = RGB(0, 229, 255)) {
    DrawRoundedRect(hdc, rect, bgCol, 8);

    HFONT oldF = (HFONT)SelectObject(hdc, g_hFontBtn ? g_hFontBtn : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(hdc, textCol);
    SetBkMode(hdc, TRANSPARENT);

    DrawTextW(hdc, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldF);
}

HWND g_hwndAbout = NULL;
std::atomic<bool> g_isCheckingUpdateInAbout{ false };

LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode));
        DwmSetWindowAttribute(hwnd, 19, &darkMode, sizeof(darkMode));
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBitmap = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);

        HBRUSH bgBrush = CreateSolidBrush(RGB(15, 19, 28));
        FillRect(memDC, &rc, bgBrush);
        DeleteObject(bgBrush);
        SetBkMode(memDC, TRANSPARENT);

        COLORREF cardBg = RGB(23, 29, 43);

        // Header Title
        SelectObject(memDC, g_hFontTitle ? g_hFontTitle : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(255, 255, 255));
        TextOutW(memDC, 20, 18, L"DeskSound Server", 16);

        RECT rVerBadge = { 260, 18, 435, 40 };
        DrawPillButtonW(memDC, rVerBadge, Utf8ToWide(APP_VERSION_TAG).c_str(), RGB(28, 36, 52), RGB(0, 229, 255));

        // Card 1: App Purpose (គោលបំណងកម្មវិធី)
        RECT c1 = { 20, 55, rc.right - 20, 195 };
        DrawRoundedRect(memDC, c1, cardBg, 14);

        SelectObject(memDC, g_hFontBold ? g_hFontBold : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(0, 229, 255));
        TextOutW(memDC, 35, 68, L"គោលបំណងកម្មវិធី (App Purpose):", 29);

        SelectObject(memDC, g_hFontSub ? g_hFontSub : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(200, 215, 235));
        TextOutW(memDC, 35, 96, L"DeskSound គឺជាកម្មវិធីបញ្ជូន និងទទួលសំឡេង Real-time", 52);
        TextOutW(memDC, 35, 118, L"ដែលផ្ដល់នូវគុណភាពសំឡេងខ្ពស់ និង Latency ទាបបំផុត (Low-Latency)។", 65);

        SetTextColor(memDC, RGB(140, 155, 180));
        TextOutW(memDC, 35, 148, L"High-performance real-time wireless & USB audio streaming server.", 63);
        TextOutW(memDC, 35, 166, L"Turns your Android smartphones into wireless PC speakers.", 57);

        // Card 2: Developer Info (អ្នកបង្កើត)
        RECT c2 = { 20, 207, rc.right - 20, 287 };
        DrawRoundedRect(memDC, c2, cardBg, 14);

        SelectObject(memDC, g_hFontBold ? g_hFontBold : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(0, 229, 255));
        TextOutW(memDC, 35, 220, L"អ្នកបង្កើត (Developer Info):", 27);

        SelectObject(memDC, g_hFontSub ? g_hFontSub : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(255, 255, 255));
        TextOutW(memDC, 35, 248, L"Vath Sathya (@vathsathya)", 25);

        RECT btnGitHub = { rc.right - 170, 242, rc.right - 35, 270 };
        DrawPillButtonW(memDC, btnGitHub, L"GitHub Repo", RGB(32, 40, 58), RGB(0, 229, 255));

        // Card 3: Software Update (បច្ចុប្បន្នភាព)
        RECT c3 = { 20, 299, rc.right - 20, 429 };
        DrawRoundedRect(memDC, c3, cardBg, 14);

        SelectObject(memDC, g_hFontBold ? g_hFontBold : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(0, 229, 255));
        TextOutW(memDC, 35, 312, L"Software Update (បច្ចុប្បន្នភាព):", 33);

        SelectObject(memDC, g_hFontSub ? g_hFontSub : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(200, 215, 235));

        std::wstring updateStatusText;
        if (g_isCheckingUpdateInAbout.load()) {
            updateStatusText = L"Checking GitHub for latest updates...";
        } else if (g_updateAvailable.load()) {
            updateStatusText = L"New update available: " + Utf8ToWide(g_latestUpdateTag);
        } else {
            updateStatusText = L"You are using the latest version (" + Utf8ToWide(APP_VERSION_TAG) + L")";
        }
        TextOutW(memDC, 35, 340, updateStatusText.c_str(), (int)updateStatusText.length());

        RECT btnCheckUpdate = { 35, 372, 220, 410 };
        RECT btnUpdateNow   = { 235, 372, 420, 410 };

        bool isUpdAvail = g_updateAvailable.load();
        DrawPillButtonW(memDC, btnCheckUpdate, L"CHECK FOR UPDATES", RGB(32, 40, 58), RGB(0, 229, 255));
        DrawPillButtonW(memDC, btnUpdateNow, isUpdAvail ? L"UPDATE NOW" : L"VIEW RELEASES", isUpdAvail ? RGB(0, 229, 255) : RGB(28, 36, 52), isUpdAvail ? RGB(18, 22, 33) : RGB(160, 175, 200));

        // Bottom Close Button
        RECT btnCloseDlg = { rc.right - 120, 445, rc.right - 20, 477 };
        DrawPillButtonW(memDC, btnCloseDlg, L"CLOSE", RGB(40, 50, 72), RGB(255, 255, 255));

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        POINT pt = { mx, my };
        RECT rc;
        GetClientRect(hwnd, &rc);

        RECT btnGitHub = { rc.right - 170, 242, rc.right - 35, 270 };
        if (PtInRect(&btnGitHub, pt)) {
            ShellExecuteW(NULL, L"open", L"https://github.com/vathsathya/yanich-desksound", NULL, NULL, SW_SHOWNORMAL);
            break;
        }

        RECT btnCheckUpdate = { 35, 372, 220, 410 };
        if (PtInRect(&btnCheckUpdate, pt)) {
            g_isCheckingUpdateInAbout.store(true);
            InvalidateRect(hwnd, NULL, FALSE);
            std::thread([hwnd]() {
                CheckForUpdatesAsync();
                Sleep(800);
                g_isCheckingUpdateInAbout.store(false);
                if (IsWindow(hwnd)) InvalidateRect(hwnd, NULL, FALSE);
            }).detach();
            break;
        }

        RECT btnUpdateNow = { 235, 372, 420, 410 };
        if (PtInRect(&btnUpdateNow, pt)) {
            std::string url = g_updateAvailable.load() ? g_latestUpdateUrl : "https://github.com/vathsathya/yanich-desksound/releases/latest";
            ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
            break;
        }

        RECT btnCloseDlg = { rc.right - 120, 445, rc.right - 20, 477 };
        if (PtInRect(&btnCloseDlg, pt)) {
            DestroyWindow(hwnd);
            break;
        }
        break;
    }
    case WM_DESTROY: {
        g_hwndAbout = NULL;
        break;
    }
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void ShowAboutDialog(HWND hwndParent) {
    if (g_hwndAbout && IsWindow(g_hwndAbout)) {
        SetForegroundWindow(g_hwndAbout);
        return;
    }

    HINSTANCE hInst = GetModuleHandle(NULL);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = AboutWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_hbrClassBg;
    wc.lpszClassName = L"YanichDeskSoundAboutClass";

    RegisterClassExW(&wc);

    int w = 460, h = 515;
    RECT rParent;
    GetWindowRect(hwndParent, &rParent);
    int x = rParent.left + (rParent.right - rParent.left - w) / 2;
    int y = rParent.top + (rParent.bottom - rParent.top - h) / 2;

    g_hwndAbout = CreateWindowExW(WS_EX_DLGMODALFRAME, L"YanichDeskSoundAboutClass", L"About Yanich DeskSound Server", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, x, y, w, h, hwndParent, NULL, hInst, NULL);
    if (g_hwndAbout) {
        ShowWindow(g_hwndAbout, SW_SHOW);
        UpdateWindow(g_hwndAbout);
    }
}

void HandleMousePos(HWND hwnd, int mx, int my, bool isClick) {
    int trackX1 = 145, trackX2 = 335;
    int trackW = trackX2 - trackX1;

    RECT btnToggleServer = { 380, 36, 480, 66 };
    RECT btnDeviceDropdown = { 150, 70, 480, 92 };

    RECT btnDropdownC1 = { 250, 136, 380, 154 };
    RECT btnKickClient1= { 390, 136, 475, 154 };

    RECT btnDropdownC2 = { 250, 158, 380, 176 };
    RECT btnKickClient2= { 390, 158, 475, 176 };

    RECT btnMasterMinus  = { 345, 352, 385, 374 };
    RECT btnMasterPlus   = { 390, 352, 430, 374 };

    RECT btnLeftMinus    = { 345, 390, 385, 412 };
    RECT btnLeftPlus     = { 390, 390, 430, 412 };

    RECT btnRightMinus   = { 345, 428, 385, 450 };
    RECT btnRightPlus    = { 390, 428, 430, 450 };

    RECT btnReset        = { 430, 320, 480, 340 };

    if (isClick) {
        POINT pt = { mx, my };
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);

        RECT rFooterText = { rcClient.right - 150, 476, rcClient.right - 20, 500 };

        if (PtInRect(&rFooterText, pt)) {
            ShowAboutDialog(hwnd);
            return;
        }

        if (g_updateAvailable.load()) {
            RECT btnUpdate = { rcClient.right - 180, 18, rcClient.right - 20, 40 };
            if (PtInRect(&btnUpdate, pt)) {
                ShellExecuteA(NULL, "open", g_latestUpdateUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
                return;
            }
        }

        // Handle open dropdown selection first
        int openMenu = g_openDropdown.load();
        if (openMenu == 1) {
            RECT rOpt1 = { 250, 160, 380, 178 };
            RECT rOpt2 = { 250, 180, 380, 198 };
            RECT rOpt3 = { 250, 200, 380, 218 };
            if (PtInRect(&rOpt1, pt)) {
                g_client1Channel.store(CLIENT_MODE_LEFT);
                if (g_clientSockets.size() >= 2) g_client2Channel.store(CLIENT_MODE_RIGHT);
                g_openDropdown.store(0);
                InvalidateRect(hwnd, NULL, FALSE); return;
            }
            if (PtInRect(&rOpt2, pt)) {
                g_client1Channel.store(CLIENT_MODE_RIGHT);
                if (g_clientSockets.size() >= 2) g_client2Channel.store(CLIENT_MODE_LEFT);
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
            RECT rOpt1 = { 250, 182, 380, 200 };
            RECT rOpt2 = { 250, 202, 380, 220 };
            RECT rOpt3 = { 250, 222, 380, 240 };
            if (PtInRect(&rOpt1, pt)) {
                g_client2Channel.store(CLIENT_MODE_LEFT);
                if (g_clientSockets.size() >= 1) g_client1Channel.store(CLIENT_MODE_RIGHT);
                g_openDropdown.store(0);
                InvalidateRect(hwnd, NULL, FALSE); return;
            }
            if (PtInRect(&rOpt2, pt)) {
                g_client2Channel.store(CLIENT_MODE_RIGHT);
                if (g_clientSockets.size() >= 1) g_client1Channel.store(CLIENT_MODE_LEFT);
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
        } else if (openMenu == 3) {
            std::lock_guard<std::mutex> lock(g_deviceMutex);
            int itemY = 100;
            for (size_t k = 0; k < g_audioDevices.size() && k < 8; ++k) {
                RECT rOpt = { 150, itemY, 480, itemY + 18 };
                if (PtInRect(&rOpt, pt)) {
                    g_selectedDeviceIndex.store((int)k);
                    g_deviceChanged.store(true); // Signal audio loop thread to switch audio endpoint
                    g_openDropdown.store(0);
                    InvalidateRect(hwnd, NULL, FALSE); return;
                }
                itemY += 20;
            }
            g_openDropdown.store(0);
            InvalidateRect(hwnd, NULL, FALSE);
        }

        RECT btnSwapLR = { 375, 114, 475, 132 };
        if (PtInRect(&btnSwapLR, pt)) {
            ClientChannelMode tmp = g_client1Channel.load();
            g_client1Channel.store(g_client2Channel.load());
            g_client2Channel.store(tmp);
            g_openDropdown.store(0);
            InvalidateRect(hwnd, NULL, FALSE); return;
        }

        RECT btnTestSound = { 375, 200, 475, 220 };
        if (PtInRect(&btnTestSound, pt)) {
            if (g_isTestAudioPlaying.load()) {
                g_isTestAudioPlaying.store(false);
                g_rmsL.store(0.0f);
                g_rmsR.store(0.0f);
            } else {
                g_testAudioStartTime.store(GetTickCount());
                g_isTestAudioPlaying.store(true);
            }
            g_openDropdown.store(0);
            InvalidateRect(hwnd, NULL, FALSE); return;
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
            if (current) {
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

        std::string c1Ip, c2Ip;
        {
            std::lock_guard<std::mutex> lock(g_clientMutex);
            c1Ip = g_client1IpStr;
            c2Ip = g_client2IpStr;
        }

        if (c1Ip != "None") {
            if (PtInRect(&btnDropdownC1, pt)) {
                g_openDropdown.store((openMenu == 1) ? 0 : 1);
                InvalidateRect(hwnd, NULL, FALSE); return;
            }
            if (PtInRect(&btnKickClient1, pt)) {
                KickClient(0);
                return;
            }
        }

        if (c2Ip != "None") {
            if (PtInRect(&btnDropdownC2, pt)) {
                g_openDropdown.store((openMenu == 2) ? 0 : 2);
                InvalidateRect(hwnd, NULL, FALSE); return;
            }
            if (PtInRect(&btnKickClient2, pt)) {
                KickClient(1);
                return;
            }
        }

        RECT btnMuteMaster = { 345, 352, 425, 374 };
        RECT btnMuteLeft   = { 345, 390, 425, 412 };
        RECT btnMuteRight  = { 345, 428, 425, 450 };

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

        if (my >= 344 && my <= 376) g_activeDrag = DRAG_MASTER;
        else if (my >= 382 && my <= 414) g_activeDrag = DRAG_GAIN_L;
        else if (my >= 420 && my <= 452) g_activeDrag = DRAG_GAIN_R;
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
        BOOL darkMode = TRUE;
        DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkMode, sizeof(darkMode));
        DwmSetWindowAttribute(hwnd, 19 /* DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 */, &darkMode, sizeof(darkMode));

        SetTimer(hwnd, 1, 30, NULL);
        
        g_nid.cbSize = sizeof(NOTIFYICONDATAW);
        g_nid.hWnd = hwnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;
        g_nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(101));
        lstrcpyW(g_nid.szTip, L"Yanich DeskSound Server");
        Shell_NotifyIconW(NIM_ADD, &g_nid);

        bool startupChecked = IsRunOnStartupEnabled();
        g_hChkStartup = CreateWindowExW(0, L"BUTTON", L"Run on Windows Startup", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 20, 479, 180, 20, hwnd, (HMENU)ID_CHK_STARTUP, GetModuleHandle(NULL), NULL);

        if (g_hFontSub) {
            SendMessage(g_hChkStartup, WM_SETFONT, (WPARAM)g_hFontSub, TRUE);
        }
        SendMessage(g_hChkStartup, BM_SETCHECK, startupChecked ? BST_CHECKED : BST_UNCHECKED, 0);

        CheckForUpdatesAsync();
        break;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        if (wmId == ID_CHK_STARTUP) {
            LRESULT chkState = SendMessage(g_hChkStartup, BM_GETCHECK, 0, 0);
            SetRunOnStartup(chkState == BST_CHECKED);
        } else if (wmId == ID_TRAY_SHOW) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        } else if (wmId == ID_TRAY_TOGGLE_SERVER) {
            bool current = g_serverActive.load();
            g_serverActive.store(!current);
            if (current) {
                std::lock_guard<std::mutex> lock(g_clientMutex);
                for (SOCKET s : g_clientSockets) closesocket(s);
                g_clientSockets.clear();
                g_client1IpStr = "None";
                g_client2IpStr = "None";
                g_openDropdown.store(0);
            }
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (wmId == ID_TRAY_MUTE_MASTER) {
            g_isMuted.store(!g_isMuted.load());
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (wmId == ID_TRAY_EXIT) {
            g_running = false;
            DestroyWindow(hwnd);
        } else if (wmId == ID_TRAY_UPDATE) {
            ShellExecuteA(NULL, "open", g_latestUpdateUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
        break;
    }

    case WM_CLOSE: {
        ShowWindow(hwnd, SW_HIDE); // Hide to system tray
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        float change = (zDelta > 0) ? 5.0f : -5.0f;
        if (pt.y >= 380 && pt.y <= 412) {
            g_masterVolume.store(std::max(0.0f, std::min(100.0f, g_masterVolume.load() + change)));
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (pt.y >= 418 && pt.y <= 450) {
            g_gainL.store(std::max(0.0f, std::min(100.0f, g_gainL.load() + change)));
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (pt.y >= 456 && pt.y <= 488) {
            g_gainR.store(std::max(0.0f, std::min(100.0f, g_gainR.load() + change)));
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    }

    case WM_LBUTTONDOWN: {
        SetCapture(hwnd);
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        HandleMousePos(hwnd, mx, my, true);
        break;
    }

    case WM_MOUSEMOVE: {
        if (wParam & MK_LBUTTON) {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
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
        if (g_isTestAudioPlaying.load()) {
            DWORD elapsed = GetTickCount() - g_testAudioStartTime.load();
            if (elapsed >= 2000) {
                g_isTestAudioPlaying.store(false);
                g_rmsL.store(0.0f);
                g_rmsR.store(0.0f);
            }
        }

        // Smooth Peak-Decay Animation Filter for Meter
        float rawL = g_rmsL.load();
        float rawR = g_rmsR.load();
        float curL = g_rmsL_smooth.load();
        float curR = g_rmsR_smooth.load();

        float nextL = (rawL > curL) ? rawL : (curL * 0.82f + rawL * 0.18f);
        float nextR = (rawR > curR) ? rawR : (curR * 0.82f + rawR * 0.18f);
        g_rmsL_smooth.store(nextL);
        g_rmsR_smooth.store(nextR);

        if (IsWindowVisible(hwnd)) {
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    }

    case WM_TRAYICON: {
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, L"Show Server GUI");
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_TOGGLE_SERVER, g_serverActive.load() ? L"Stop Server" : L"Start Server");
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_MUTE_MASTER, g_isMuted.load() ? L"Unmute Master" : L"Mute Master");
            if (g_updateAvailable.load()) {
                std::wstring uText = L"🚀 Update Available (" + Utf8ToWide(g_latestUpdateTag) + L")";
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_UPDATE, uText.c_str());
            }
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit Server");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
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

        HBRUSH bgBrush = CreateSolidBrush(RGB(15, 19, 28));
        FillRect(memDC, &rcClient, bgBrush);
        DeleteObject(bgBrush);

        SetBkMode(memDC, TRANSPARENT);

        if (g_updateAvailable.load()) {
            RECT btnUpdate = { rcClient.right - 180, 12, rcClient.right - 20, 34 };
            std::wstring btnText = L"🚀 UPDATE " + Utf8ToWide(g_latestUpdateTag);
            DrawPillButtonW(memDC, btnUpdate, btnText.c_str(), RGB(0, 229, 255), RGB(18, 22, 33));
        }

        COLORREF cardBgColor = RGB(23, 29, 43);

        // Server Status Card
        RECT card1 = { 20, 20, rcClient.right - 20, 102 };
        DrawRoundedRect(memDC, card1, cardBgColor, 14);

        bool isActive = g_serverActive.load();

        HBRUSH dotBrush = CreateSolidBrush(isActive ? RGB(0, 230, 118) : RGB(255, 82, 82));
        HBRUSH oldB = (HBRUSH)SelectObject(memDC, dotBrush);
        HPEN nullPen = CreatePen(PS_NULL, 0, RGB(0,0,0));
        HPEN oldP = (HPEN)SelectObject(memDC, nullPen);
        Ellipse(memDC, 35, 36, 47, 48);
        SelectObject(memDC, oldB);
        SelectObject(memDC, oldP);
        DeleteObject(dotBrush);
        DeleteObject(nullPen);

        SelectObject(memDC, g_hFontBold ? g_hFontBold : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(255, 255, 255));
        
        std::wstring statusText = isActive ? L"Server Status: RUNNING (Port 5000)" : L"Server Status: STOPPED";
        TextOutW(memDC, 55, 32, statusText.c_str(), (int)statusText.length());

        SelectObject(memDC, g_hFontSub ? g_hFontSub : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(160, 175, 200));

        std::string localIps, c1Ip, c2Ip;
        {
            std::lock_guard<std::mutex> lock(g_clientMutex);
            localIps = g_localIpsStr;
            c1Ip = g_client1IpStr;
            c2Ip = g_client2IpStr;
        }

        std::wstring ipLine = L"Local IP: " + Utf8ToWide(localIps);
        TextOutW(memDC, 55, 52, ipLine.c_str(), (int)ipLine.length());

        // Device Label & Dropdown
        TextOutW(memDC, 35, 74, L"PC Audio Device:", 16);
        std::string selectedDevName = "Default Playback Device";
        {
            std::lock_guard<std::mutex> lock(g_deviceMutex);
            int selIdx = g_selectedDeviceIndex.load();
            if (selIdx >= 0 && selIdx < (int)g_audioDevices.size()) {
                selectedDevName = g_audioDevices[selIdx].name;
            }
        }
        if (selectedDevName.length() > 34) selectedDevName = selectedDevName.substr(0, 31) + "...";
        std::wstring wDevBtnName = Utf8ToWide(selectedDevName) + L"  v";

        RECT btnDeviceDropdown = { 150, 70, 480, 92 };
        DrawPillButtonW(memDC, btnDeviceDropdown, wDevBtnName.c_str(), RGB(32, 40, 58), RGB(0, 229, 255));

        RECT btnToggleServer = { 380, 30, 480, 56 };
        if (isActive) {
            DrawPillButtonW(memDC, btnToggleServer, L"STOP SERVER", RGB(55, 25, 33), RGB(255, 82, 82));
        } else {
            DrawPillButtonW(memDC, btnToggleServer, L"START SERVER", RGB(0, 230, 118), RGB(18, 22, 33));
        }

        // Active Clients Card
        RECT card2 = { 20, 110, rcClient.right - 20, 186 };
        DrawRoundedRect(memDC, card2, cardBgColor, 14);

        SelectObject(memDC, g_hFontBold ? g_hFontBold : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(0, 229, 255));
        TextOutW(memDC, 35, 118, L"Active Clients", 14);

        if (g_clientSockets.size() >= 2) {
            RECT btnSwapLR = { 375, 114, 475, 132 };
            DrawPillButtonW(memDC, btnSwapLR, L"Swap L/R", RGB(32, 40, 58), RGB(0, 229, 255));
        }

        SelectObject(memDC, g_hFontSub ? g_hFontSub : (HFONT)GetStockObject(DEFAULT_GUI_FONT));

        if (c1Ip == "None" && c2Ip == "None") {
            SetTextColor(memDC, RGB(130, 145, 170));
            TextOutW(memDC, 35, 144, L"[Idle] No active clients connected. Open DeskSound app on phone.", 65);
        } else {
            SetTextColor(memDC, RGB(255, 255, 255));
            std::wstring c1Text = L"Client #1: " + Utf8ToWide(c1Ip);
            std::wstring c2Text = L"Client #2: " + Utf8ToWide(c2Ip);

            TextOutW(memDC, 35, 138, c1Text.c_str(), (int)c1Text.length());
            TextOutW(memDC, 35, 160, c2Text.c_str(), (int)c2Text.length());

            if (c1Ip != "None") {
                RECT btnDropdownC1 = { 250, 136, 380, 154 };
                RECT btnKickClient1= { 390, 136, 475, 154 };

                ClientChannelMode ch1 = g_client1Channel.load();
                const wchar_t* labelC1 = (ch1 == CLIENT_MODE_LEFT) ? L"Left (L)  v" : (ch1 == CLIENT_MODE_RIGHT) ? L"Right (R)  v" : L"Stereo (L+R)  v";

                DrawPillButtonW(memDC, btnDropdownC1, labelC1, RGB(32, 40, 58), RGB(0, 229, 255));
                DrawPillButtonW(memDC, btnKickClient1, L"Disconnect", RGB(255, 82, 82), RGB(255, 255, 255));
            }

            if (c2Ip != "None") {
                RECT btnDropdownC2 = { 250, 158, 380, 176 };
                RECT btnKickClient2= { 390, 158, 475, 176 };

                ClientChannelMode ch2 = g_client2Channel.load();
                const wchar_t* labelC2 = (ch2 == CLIENT_MODE_LEFT) ? L"Left (L)  v" : (ch2 == CLIENT_MODE_RIGHT) ? L"Right (R)  v" : L"Stereo (L+R)  v";

                DrawPillButtonW(memDC, btnDropdownC2, labelC2, RGB(32, 40, 58), RGB(0, 229, 255));
                DrawPillButtonW(memDC, btnKickClient2, L"Disconnect", RGB(255, 82, 82), RGB(255, 255, 255));
            }
        }

        // Stereo Peak Audio Visualizer Meter Card
        RECT card3 = { 20, 194, rcClient.right - 20, 299 };
        DrawRoundedRect(memDC, card3, cardBgColor, 14);

        SelectObject(memDC, g_hFontBold ? g_hFontBold : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(255, 255, 255));
        TextOutW(memDC, 35, 204, L"Live Stereo Audio Visualizer Meter", 34);

        RECT btnTestSound = { 375, 200, 475, 220 };
        bool isTesting = g_isTestAudioPlaying.load();
        DrawPillButtonW(memDC, btnTestSound, isTesting ? L"Playing..." : L"Test Sound", isTesting ? RGB(0, 230, 118) : RGB(32, 40, 58), isTesting ? RGB(18, 22, 33) : RGB(0, 229, 255));

        RECT rBarL_Bg = { 70, 232, rcClient.right - 40, 250 };
        RECT rBarR_Bg = { 70, 262, rcClient.right - 40, 280 };
        DrawRoundedRect(memDC, rBarL_Bg, RGB(14, 18, 26), 6);
        DrawRoundedRect(memDC, rBarR_Bg, RGB(14, 18, 26), 6);

        SelectObject(memDC, g_hFontSub ? g_hFontSub : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(160, 175, 200));
        TextOutW(memDC, 35, 234, L"L:", 2);
        TextOutW(memDC, 35, 264, L"R:", 2);

        float rmsL = g_rmsL_smooth.load();
        float rmsR = g_rmsR_smooth.load();
        int maxBarW = (rcClient.right - 40) - 70;
        int barW_L = (int)(rmsL * 3.0f * maxBarW);
        int barW_R = (int)(rmsR * 3.0f * maxBarW);
        if (barW_L > maxBarW) barW_L = maxBarW;
        if (barW_R > maxBarW) barW_R = maxBarW;

        if (barW_L > 0) {
            RECT rBarL = { 70, 232, 70 + barW_L, 250 };
            COLORREF colorL = (rmsL > 0.75f) ? RGB(255, 82, 82) : RGB(0, 229, 255);
            DrawRoundedRect(memDC, rBarL, colorL, 6);
        }
        if (barW_R > 0) {
            RECT rBarR = { 70, 262, 70 + barW_R, 280 };
            COLORREF colorR = (rmsR > 0.75f) ? RGB(255, 82, 82) : RGB(0, 229, 255);
            DrawRoundedRect(memDC, rBarR, colorR, 6);
        }

        // Volume & Channel Gain Control Card
        RECT card4 = { 20, 307, rcClient.right - 20, 466 };
        DrawRoundedRect(memDC, card4, cardBgColor, 14);

        SelectObject(memDC, g_hFontBold ? g_hFontBold : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(0, 229, 255));
        TextOutW(memDC, 35, 324, L"Volume & Channel Gain Control", 29);

        RECT btnReset = { 430, 320, 480, 340 };
        DrawPillButtonW(memDC, btnReset, L"Reset", RGB(34, 42, 60), RGB(255, 255, 255));

        SelectObject(memDC, g_hFontSub ? g_hFontSub : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(255, 255, 255));
        TextOutW(memDC, 35, 356, L"Master Volume:", 14);
        TextOutW(memDC, 35, 394, L"Left (L) Volume:", 16);
        TextOutW(memDC, 35, 432, L"Right (R) Volume:", 17);

        int trackX1 = 145, trackX2 = 335;
        int trackW = trackX2 - trackX1;

        float mNorm = std::max(0.0f, std::min(1.0f, g_masterVolume.load() / 100.0f));
        RECT rTrackM_Bg = { trackX1, 360, trackX2, 366 };
        DrawRoundedRect(memDC, rTrackM_Bg, RGB(24, 30, 44), 6);
        if (mNorm > 0.0f) {
            RECT rTrackM_Fill = { trackX1, 360, trackX1 + (int)(mNorm * trackW), 366 };
            DrawRoundedRect(memDC, rTrackM_Fill, RGB(0, 229, 255), 6);
        }
        int kxM = trackX1 + (int)(mNorm * trackW);
        RECT rKnobM = { kxM - 7, 356, kxM + 7, 370 };
        DrawRoundedRect(memDC, rKnobM, RGB(255, 255, 255), 14);

        float gLNorm = std::max(0.0f, std::min(1.0f, g_gainL.load() / 100.0f));
        RECT rTrackL_Bg = { trackX1, 398, trackX2, 404 };
        DrawRoundedRect(memDC, rTrackL_Bg, RGB(24, 30, 44), 6);
        if (gLNorm > 0.0f) {
            RECT rTrackL_Fill = { trackX1, 398, trackX1 + (int)(gLNorm * trackW), 404 };
            DrawRoundedRect(memDC, rTrackL_Fill, RGB(0, 229, 255), 6);
        }
        int kxL = trackX1 + (int)(gLNorm * trackW);
        RECT rKnobL = { kxL - 7, 394, kxL + 7, 408 };
        DrawRoundedRect(memDC, rKnobL, RGB(255, 255, 255), 14);

        float gRNorm = std::max(0.0f, std::min(1.0f, g_gainR.load() / 100.0f));
        RECT rTrackR_Bg = { trackX1, 436, trackX2, 442 };
        DrawRoundedRect(memDC, rTrackR_Bg, RGB(24, 30, 44), 6);
        if (gRNorm > 0.0f) {
            RECT rTrackR_Fill = { trackX1, 436, trackX1 + (int)(gRNorm * trackW), 442 };
            DrawRoundedRect(memDC, rTrackR_Fill, RGB(0, 229, 255), 6);
        }
        int kxR = trackX1 + (int)(gRNorm * trackW);
        RECT rKnobR = { kxR - 7, 432, kxR + 7, 446 };
        DrawRoundedRect(memDC, rKnobR, RGB(255, 255, 255), 14);

        RECT btnMuteMaster = { 345, 352, 425, 374 };
        RECT btnMuteLeft   = { 345, 390, 425, 412 };
        RECT btnMuteRight  = { 345, 428, 425, 450 };

        bool isMutedM = g_isMuted.load();
        bool isMutedL = g_isMutedL.load();
        bool isMutedR = g_isMutedR.load();

        DrawPillButtonW(memDC, btnMuteMaster, isMutedM ? L"Muted" : L"Mute", isMutedM ? RGB(255, 82, 82) : RGB(34, 42, 60), isMutedM ? RGB(255, 255, 255) : RGB(0, 229, 255));
        DrawPillButtonW(memDC, btnMuteLeft,   isMutedL ? L"Muted" : L"Mute", isMutedL ? RGB(255, 82, 82) : RGB(34, 42, 60), isMutedL ? RGB(255, 255, 255) : RGB(0, 229, 255));
        DrawPillButtonW(memDC, btnMuteRight,  isMutedR ? L"Muted" : L"Mute", isMutedR ? RGB(255, 82, 82) : RGB(34, 42, 60), isMutedR ? RGB(255, 255, 255) : RGB(0, 229, 255));

        wchar_t strMaster[16], strL[16], strR[16];
        swprintf(strMaster, 16, L"%d%%", (int)g_masterVolume.load());
        swprintf(strL, 16, L"%d%%", (int)g_gainL.load());
        swprintf(strR, 16, L"%d%%", (int)g_gainR.load());

        SetTextColor(memDC, RGB(0, 229, 255));
        TextOutW(memDC, rcClient.right - 65, 356, strMaster, (int)wcslen(strMaster));
        TextOutW(memDC, rcClient.right - 65, 394, strL, (int)wcslen(strL));
        TextOutW(memDC, rcClient.right - 65, 432, strR, (int)wcslen(strR));

        SelectObject(memDC, g_hFontFooter ? g_hFontFooter : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(memDC, RGB(0, 229, 255));
        TextOutW(memDC, rcClient.right - 145, 480, L"About Developer", 15);

        // Draw active dropdown popup menu overlay at VERY END (Highest Z-Order)
        int openMenu = g_openDropdown.load();
        if (openMenu == 1 && c1Ip != "None") {
            RECT rMenuBg = { 248, 158, 382, 222 };
            DrawRoundedRect(memDC, rMenuBg, RGB(18, 22, 33), 6);
            RECT rOpt1 = { 250, 160, 380, 178 };
            RECT rOpt2 = { 250, 180, 380, 198 };
            RECT rOpt3 = { 250, 200, 380, 218 };
            ClientChannelMode ch1 = g_client1Channel.load();
            DrawPillButtonW(memDC, rOpt1, L"Left Channel (L)",  (ch1 == CLIENT_MODE_LEFT)   ? RGB(0, 229, 255) : RGB(34, 42, 60), (ch1 == CLIENT_MODE_LEFT)   ? RGB(18, 22, 33) : RGB(255, 255, 255));
            DrawPillButtonW(memDC, rOpt2, L"Right Channel (R)", (ch1 == CLIENT_MODE_RIGHT)  ? RGB(0, 229, 255) : RGB(34, 42, 60), (ch1 == CLIENT_MODE_RIGHT)  ? RGB(18, 22, 33) : RGB(255, 255, 255));
            DrawPillButtonW(memDC, rOpt3, L"Stereo (L+R)",      (ch1 == CLIENT_MODE_STEREO) ? RGB(0, 229, 255) : RGB(34, 42, 60), (ch1 == CLIENT_MODE_STEREO) ? RGB(18, 22, 33) : RGB(255, 255, 255));
        } else if (openMenu == 2 && c2Ip != "None") {
            RECT rMenuBg = { 248, 180, 382, 244 };
            DrawRoundedRect(memDC, rMenuBg, RGB(18, 22, 33), 6);
            RECT rOpt1 = { 250, 182, 380, 200 };
            RECT rOpt2 = { 250, 202, 380, 220 };
            RECT rOpt3 = { 250, 222, 380, 240 };
            ClientChannelMode ch2 = g_client2Channel.load();
            DrawPillButtonW(memDC, rOpt1, L"Left Channel (L)",  (ch2 == CLIENT_MODE_LEFT)   ? RGB(0, 229, 255) : RGB(34, 42, 60), (ch2 == CLIENT_MODE_LEFT)   ? RGB(18, 22, 33) : RGB(255, 255, 255));
            DrawPillButtonW(memDC, rOpt2, L"Right Channel (R)", (ch2 == CLIENT_MODE_RIGHT)  ? RGB(0, 229, 255) : RGB(34, 42, 60), (ch2 == CLIENT_MODE_RIGHT)  ? RGB(18, 22, 33) : RGB(255, 255, 255));
            DrawPillButtonW(memDC, rOpt3, L"Stereo (L+R)",      (ch2 == CLIENT_MODE_STEREO) ? RGB(0, 229, 255) : RGB(34, 42, 60), (ch2 == CLIENT_MODE_STEREO) ? RGB(18, 22, 33) : RGB(255, 255, 255));
        } else if (openMenu == 3) {
            std::lock_guard<std::mutex> lock(g_deviceMutex);
            int count = (int)g_audioDevices.size();
            if (count > 8) count = 8;
            RECT rMenuBg = { 148, 98, 482, 100 + count * 20 };
            DrawRoundedRect(memDC, rMenuBg, RGB(18, 22, 33), 6);
            int itemY = 100;
            int selIdx = g_selectedDeviceIndex.load();
            for (size_t k = 0; k < g_audioDevices.size() && k < 8; ++k) {
                RECT rOpt = { 150, itemY, 480, itemY + 18 };
                std::string devName = g_audioDevices[k].name;
                if (devName.length() > 34) devName = devName.substr(0, 31) + "...";
                std::wstring wDevName = Utf8ToWide(devName);
                DrawPillButtonW(memDC, rOpt, wDevName.c_str(), (selIdx == (int)k) ? RGB(0, 229, 255) : RGB(34, 42, 60), (selIdx == (int)k) ? RGB(18, 22, 33) : RGB(255, 255, 255));
                itemY += 20;
            }
        }

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
        SetBkColor(hdcStatic, RGB(15, 19, 28));
        if (!g_hbrStaticBg) {
            g_hbrStaticBg = CreateSolidBrush(RGB(15, 19, 28));
        }
        return (INT_PTR)g_hbrStaticBg;
    }

    case WM_DESTROY: {
        SaveServerConfig();
        g_running = false;
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        KillTimer(hwnd, 1);
        if (g_hbrStaticBg) {
            DeleteObject(g_hbrStaticBg);
            g_hbrStaticBg = NULL;
        }
        if (g_hbrClassBg) {
            DeleteObject(g_hbrClassBg);
            g_hbrClassBg = NULL;
        }
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    
    // Enable High DPI Awareness for crisp text and scaling
    typedef BOOL(WINAPI *SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32) {
        SetProcessDpiAwarenessContextProc pSetDpi = (SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetDpi) {
            pSetDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    CoInitialize(NULL);
    InitFonts();
    LoadServerConfig();
    EnsureFirewallRulesExist();

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

    std::thread adbThread(AutoRunAdbReverseLoop);
    adbThread.detach();

    g_hbrClassBg = CreateSolidBrush(RGB(18, 22, 33));

    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, WndProc, 0L, 0L, hInstance, LoadIcon(hInstance, MAKEINTRESOURCE(101)), LoadCursor(NULL, IDC_ARROW), g_hbrClassBg, NULL, L"YanichDeskSoundGUIClass", NULL };
    RegisterClassExW(&wc);

    InitFonts();

    std::wstring winTitle = L"Yanich DeskSound Server " + Utf8ToWide(APP_VERSION_TAG);
    HWND hwnd = CreateWindowExW(0, L"YanichDeskSoundGUIClass", winTitle.c_str(), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 100, 100, 520, 554, NULL, NULL, hInstance, NULL);
    g_hwndMain = hwnd;

    bool startSilent = (strstr(lpCmdLine, "-silent") != NULL || strstr(lpCmdLine, "-service") != NULL);
    ShowWindow(hwnd, startSilent ? SW_HIDE : nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_running = false;
    closesocket(listenSocket);
    WSACleanup();
    CleanupFonts();

    return (int)msg.wParam;
}