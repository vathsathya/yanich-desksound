#include "../include/network_server.h"
#include "../include/logger.h"
#include "../include/config_manager.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <chrono>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

NetworkServer& NetworkServer::Instance() {
    static NetworkServer instance;
    return instance;
}

NetworkServer::NetworkServer() : m_listenSocket(INVALID_SOCKET) {}

NetworkServer::~NetworkServer() {
    Stop();
}

static bool IsPrivateIP(const sockaddr_in& addr) {
    const unsigned char* ip = (const unsigned char*)&(addr.sin_addr.s_addr);
    if (ip[0] == 127) return true;
    if (ip[0] == 10) return true;
    if (ip[0] == 172 && (ip[1] >= 16 && ip[1] <= 31)) return true;
    if (ip[0] == 192 && ip[1] == 168) return true;
    if (ip[0] == 169 && ip[1] == 254) return true;
    return false;
}

std::vector<std::string> NetworkServer::GetLocalIPs() {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    if (!m_localIps.empty()) return m_localIps;

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        addrinfo hints{}, * res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(hostname, nullptr, &hints, &res) == 0) {
            for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
                sockaddr_in* ipv4 = (sockaddr_in*)p->ai_addr;
                char ipStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(ipv4->sin_addr), ipStr, sizeof(ipStr));
                if (std::string(ipStr) != "127.0.0.1" && IsPrivateIP(*ipv4)) {
                    m_localIps.push_back(ipStr);
                }
            }
            freeaddrinfo(res);
        }
    }
    if (m_localIps.empty()) m_localIps.push_back("127.0.0.1");
    return m_localIps;
}

bool NetworkServer::Start(int port, int discoveryPort) {
    if (m_running.load()) return true;
    m_port = port;
    m_discoveryPort = discoveryPort;

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) return false;

    int optval = 1;
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(m_port);

    if (bind(m_listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR ||
        listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }

    m_running.store(true);
    m_acceptThread = std::thread(&NetworkServer::AcceptThread, this);
    m_udpThread = std::thread(&NetworkServer::UdpDiscoveryThread, this);
    m_adbThread = std::thread(&NetworkServer::AdbReverseThread, this);

    LOG_INFO("[Network] Audio Streaming Server initialized on TCP port " + std::to_string(m_port));
    return true;
}

void NetworkServer::Stop() {
    if (!m_running.load()) return;
    m_running.store(false);

    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }

    if (m_udpSocket != INVALID_SOCKET) {
        closesocket(m_udpSocket);
        m_udpSocket = INVALID_SOCKET;
    }

    {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        for (const auto& c : m_clients) {
            closesocket(c.socketFd);
        }
        m_clients.clear();
    }

    if (m_acceptThread.joinable()) m_acceptThread.join();
    if (m_udpThread.joinable()) m_udpThread.join();
    if (m_adbThread.joinable()) m_adbThread.join();

#ifdef _WIN32
    WSACleanup();
#endif
}

void NetworkServer::AcceptThread() {
    while (m_running.load()) {
        sockaddr_in clientAddr{};
        socklen_t clientAddrLen = sizeof(clientAddr);
        auto clientSocket = accept(m_listenSocket, (sockaddr*)&clientAddr, &clientAddrLen);

        if (clientSocket == INVALID_SOCKET) {
            if (!m_running.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (!m_active.load() || !IsPrivateIP(clientAddr)) {
            closesocket(clientSocket);
            continue;
        }

        char clientIp[INET_ADDRSTRLEN] = "Client";
        inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIp, sizeof(clientIp));

        std::lock_guard<std::mutex> lock(m_clientMutex);
        if (m_clients.size() >= 2) {
            closesocket(clientSocket);
            continue;
        }

        int optVal = 1;
        int sndBufSize = 128 * 1024;
        setsockopt(clientSocket, SOL_SOCKET, SO_SNDBUF, (const char*)&sndBufSize, sizeof(sndBufSize));
        setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&optVal, sizeof(optVal));

#ifdef _WIN32
        u_long nonBlockingMode = 1;
        ioctlsocket(clientSocket, FIONBIO, &nonBlockingMode);
#else
        int flags = fcntl(clientSocket, F_GETFL, 0);
        fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);
#endif

        ClientChannelMode mode = CLIENT_MODE_STEREO;
        if (m_clients.size() == 1) {
            m_clients[0].channelMode = CLIENT_MODE_LEFT;
            mode = CLIENT_MODE_RIGHT;
        }

        ClientSession session;
        session.id = (int)m_clients.size() + 1;
        session.ip = clientIp;
        session.channelMode = mode;
        session.socketFd = clientSocket;

        m_clients.push_back(session);

        const char* modeStr = (mode == CLIENT_MODE_LEFT) ? "LEFT" : (mode == CLIENT_MODE_RIGHT) ? "RIGHT" : "STEREO";
        char formatHeader[33] = { 0 };
        snprintf(formatHeader, sizeof(formatHeader), "FORMAT|48000|2|16|%s", modeStr);
        size_t headerLen = strlen(formatHeader);
        for (size_t i = headerLen; i < 32; ++i) formatHeader[i] = ' ';
        formatHeader[32] = '\0';
        send(clientSocket, formatHeader, 32, 0);

        LOG_INFO("[Network] Client connected: " + std::string(clientIp) + " (" + std::string(modeStr) + ")");
    }
}

void NetworkServer::UdpDiscoveryThread() {
    m_udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_udpSocket == INVALID_SOCKET) return;

    int optval = 1;
    setsockopt(m_udpSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));

    sockaddr_in discoveryAddr{};
    discoveryAddr.sin_family = AF_INET;
    discoveryAddr.sin_addr.s_addr = INADDR_ANY;
    discoveryAddr.sin_port = htons(m_discoveryPort);

    if (bind(m_udpSocket, (sockaddr*)&discoveryAddr, sizeof(discoveryAddr)) == 0) {
        char recvBuf[256];
        sockaddr_in clientAddr{};
        socklen_t clientAddrLen = sizeof(clientAddr);

        while (m_running.load()) {
            int bytesRead = (int)recvfrom(m_udpSocket, recvBuf, sizeof(recvBuf) - 1, 0, (sockaddr*)&clientAddr, &clientAddrLen);
            if (bytesRead > 0) {
                if (!m_active.load() || !IsPrivateIP(clientAddr)) continue;
                recvBuf[bytesRead] = '\0';
                if (strstr(recvBuf, "DESKSOUND_DISCOVER") != NULL) {
                    const char* replyMsg = "DESKSOUND_SERVER|5000";
                    sendto(m_udpSocket, replyMsg, (int)strlen(replyMsg), 0, (sockaddr*)&clientAddr, clientAddrLen);
                }
            }
        }
    }
    if (m_udpSocket != INVALID_SOCKET) {
        closesocket(m_udpSocket);
        m_udpSocket = INVALID_SOCKET;
    }
}

#ifdef _WIN32
static void RunSilentCommand(const char* cmd) {
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    char command[256];
    snprintf(command, sizeof(command), "cmd.exe /c %s", cmd);

    if (CreateProcessA(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 2000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}
#endif

void NetworkServer::AdbReverseThread() {
    while (m_running.load()) {
        if (m_active.load()) {
#ifdef _WIN32
            RunSilentCommand("adb reverse tcp:5000 tcp:5000 >nul 2>&1");
#else
            system("adb reverse tcp:5000 tcp:5000 >/dev/null 2>&1");
#endif
        }
        for (int i = 0; i < 80 && m_running.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void NetworkServer::KickClient(int index) {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    if (index >= 0 && (size_t)index < m_clients.size()) {
        closesocket(m_clients[index].socketFd);
        LOG_INFO("[Network] Kicked client: " + m_clients[index].ip);
        m_clients.erase(m_clients.begin() + index);

        if (m_clients.size() == 1) {
            m_clients[0].channelMode = CLIENT_MODE_STEREO;
        }
    }
}

void NetworkServer::SetClientChannelMode(int index, ClientChannelMode mode) {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    if (index >= 0 && (size_t)index < m_clients.size()) {
        m_clients[index].channelMode = mode;
    }
}

void NetworkServer::SwapClientChannels() {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    if (m_clients.size() >= 2) {
        std::swap(m_clients[0].channelMode, m_clients[1].channelMode);
    }
}

std::vector<ClientSession> NetworkServer::GetClients() {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    return m_clients;
}

void NetworkServer::BroadcastAudio(const float* samples, size_t frames, int channels, int sampleRate) {
    if (!m_active.load() || frames == 0) return;

    ServerConfig cfg = ConfigManager::Instance().GetConfig();
    float masterVol = cfg.masterVolume / 100.0f;
    float gainL = (cfg.isMuted || cfg.isMutedL) ? 0.0f : (cfg.gainL / 100.0f * masterVol);
    float gainR = (cfg.isMuted || cfg.isMutedR) ? 0.0f : (cfg.gainR / 100.0f * masterVol);

    std::vector<int16_t> pcmStereo(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        float l = samples[i * 2 + 0] * gainL;
        float r = samples[i * 2 + 1] * gainR;
        l = std::max(-1.0f, std::min(1.0f, l));
        r = std::max(-1.0f, std::min(1.0f, r));
        pcmStereo[i * 2 + 0] = (int16_t)(l * 32767.0f);
        pcmStereo[i * 2 + 1] = (int16_t)(r * 32767.0f);
    }

    std::lock_guard<std::mutex> lock(m_clientMutex);
    if (m_clients.empty()) return;

    size_t audioBytes = frames * 2 * sizeof(int16_t);
    std::vector<char> packet(8 + audioBytes);

    uint32_t netLen = htonl((uint32_t)audioBytes);

    for (size_t idx = 0; idx < m_clients.size(); ++idx) {
        uint32_t tagMode = (uint32_t)m_clients[idx].channelMode;
        uint32_t netTag = htonl(tagMode);
        memcpy(packet.data(), &netTag, 4);
        memcpy(packet.data() + 4, &netLen, 4);
        memcpy(packet.data() + 8, pcmStereo.data(), audioBytes);

        send(m_clients[idx].socketFd, packet.data(), (int)packet.size(), 0);
        m_totalBytesSent.fetch_add(packet.size());
    }

    static auto lastBitrateCalc = std::chrono::steady_clock::now();
    static uint32_t packetsSentCounter = 0;
    packetsSentCounter += (uint32_t)m_clients.size();

    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<float> elapsed = now - lastBitrateCalc;
    if (elapsed.count() >= 1.0f) {
        uint64_t bytes = m_totalBytesSent.exchange(0);
        float mbps = (bytes * 8.0f) / (1024.0f * 1024.0f * elapsed.count());
        m_bitrateMbps.store(mbps > 0.01f ? mbps : (m_clients.empty() ? 0.0f : 0.8f));

        int pps = (int)(packetsSentCounter / elapsed.count());
        m_packetsPerSec.store(m_clients.empty() ? 0 : (std::max)(128, pps));
        packetsSentCounter = 0;

        lastBitrateCalc = now;
    }
}
