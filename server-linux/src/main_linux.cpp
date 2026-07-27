#include "audio_pulse.h"
#include "config_manager.h"
#include "logger.h"
#include "tray_linux.h"
#include "gui_linux.h"

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <csignal>
#include <cstring>
#include <cstdlib>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <net/if.h>

PulseAudioRecorder g_audioRecorder;
std::atomic<bool> g_running{true};
std::atomic<bool> g_serverActive{true};
std::atomic<uint64_t> g_totalBytesSent{0};

struct ClientConnection {
    int socketFd;
    std::string ipAddress;
    std::string channelMode; // "STEREO", "LEFT", "RIGHT"
    uint32_t modeTag = 0;   // 0 = STEREO, 1 = LEFT, 2 = RIGHT
};

std::vector<ClientConnection> g_clientConnections;
std::mutex g_clientMutex;
int g_serverSockFd = -1;
int g_udpSockFd = -1;

// Ultra-fast zero-copy static audio transformation buffers
static thread_local std::vector<float> s_bufStereo;
static thread_local std::vector<float> s_bufLeft;
static thread_local std::vector<float> s_bufRight;
static thread_local std::vector<uint8_t> s_packetBuffer;

void SendDesktopNotification(const std::string& title, const std::string& msg) {
    std::string cmd = "notify-send \"" + title + "\" \"" + msg + "\" -i audio-volume-high >/dev/null 2>&1";
    int r = system(cmd.c_str());
    (void)r;
}

void StopServerAndExit() {
    static std::atomic<bool> isExiting{false};
    if (isExiting.exchange(true)) return;

    LOG_INFO("[Linux Server] Shutting down sockets and audio recorder...");
    g_running.store(false);
    g_serverActive.store(false);

    if (g_serverSockFd >= 0) {
        shutdown(g_serverSockFd, SHUT_RDWR);
        close(g_serverSockFd);
        g_serverSockFd = -1;
    }
    if (g_udpSockFd >= 0) {
        shutdown(g_udpSockFd, SHUT_RDWR);
        close(g_udpSockFd);
        g_udpSockFd = -1;
    }

    g_audioRecorder.StopCapture();

    {
        std::lock_guard<std::mutex> lock(g_clientMutex);
        for (auto& conn : g_clientConnections) {
            close(conn.socketFd);
        }
        g_clientConnections.clear();
    }
}

void SignalHandler(int signum) {
    LOG_INFO("[Linux Server] Received termination signal (" + std::to_string(signum) + "). Shutting down gracefully...");
    StopServerAndExit();
    std::exit(0);
}

std::vector<std::string> GetLocalIPAddresses() {
    std::vector<std::string> ips;
    ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == 0) {
        for (ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;

            char host[INET_ADDRSTRLEN];
            void* addr = &((sockaddr_in*)ifa->ifa_addr)->sin_addr;
            if (inet_ntop(AF_INET, addr, host, sizeof(host))) {
                ips.push_back(host);
            }
        }
        freeifaddrs(ifaddr);
    }
    if (ips.empty()) ips.push_back("127.0.0.1");
    return ips;
}

void ReevaluateAutoChannelModesLocked() {
    if (g_clientConnections.size() == 1) {
        g_clientConnections[0].modeTag = 0;
        g_clientConnections[0].channelMode = "STEREO";
    } else if (g_clientConnections.size() >= 2) {
        g_clientConnections[0].modeTag = 1;
        g_clientConnections[0].channelMode = "LEFT";

        g_clientConnections[1].modeTag = 2;
        g_clientConnections[1].channelMode = "RIGHT";
    }
}

void KickClient(size_t index) {
    std::lock_guard<std::mutex> lock(g_clientMutex);
    if (index < g_clientConnections.size()) {
        std::string ip = g_clientConnections[index].ipAddress;
        LOG_INFO("[Linux Server] Kicking client: " + ip);
        close(g_clientConnections[index].socketFd);
        g_clientConnections.erase(g_clientConnections.begin() + index);
        ReevaluateAutoChannelModesLocked();
        SendDesktopNotification("DeskSound Client Kicked", "Disconnected client: " + ip);
    }
}

void AutoRunAdbReverseLoop() {
    LOG_INFO("[USB ADB] Starting ADB Reverse Port Forwarding Background Thread...");
    while (g_running) {
        int r1 = system("adb reverse tcp:5000 tcp:5000 >/dev/null 2>&1");
        int r2 = system("adb reverse tcp:5001 tcp:5001 >/dev/null 2>&1");
        (void)r1; (void)r2;
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

void CheckForUpdatesAsync() {
    std::thread([]() {
        LOG_INFO("[Update Checker] Checking GitHub releases for Yanich DeskSound updates...");
        int res = system("curl -s -m 5 https://api.github.com/repos/vathsathya/yanich-desksound/releases/latest > /tmp/desksound_ver.json 2>/dev/null");
        if (res == 0) {
            LOG_INFO("[Update Checker] Checked GitHub releases successfully.");
        }
    }).detach();
}

int main(int argc, char** argv) {
    // Ignore SIGPIPE to prevent broken pipe crashes
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    ConfigManager::Instance().LoadConfig();
    ServerConfig cfg = ConfigManager::Instance().GetConfig();
    if (cfg.bufferSize == 0) {
        cfg.bufferSize = 1024; // Default crystal-clear 1024 samples (~21.3ms)
    }

    LOG_INFO("==================================================");
    LOG_INFO(" 🔊 Yanich DeskSound Linux Server v1.2.1");
    LOG_INFO("==================================================");

    // Initialize PulseAudio Capture
    g_audioRecorder.SetVolume(cfg.masterVolume, cfg.gainL, cfg.gainR, cfg.muted, cfg.mutedL, cfg.mutedR);
    bool audioStarted = g_audioRecorder.StartCapture([cfg](const uint8_t* pcmData, size_t sizeBytes) {
        if (!g_serverActive.load()) return;
        std::lock_guard<std::mutex> lock(g_clientMutex);
        if (g_clientConnections.empty()) return;

        const float* floatSamples = reinterpret_cast<const float*>(pcmData);
        size_t floatCount = sizeBytes / sizeof(float);
        size_t frameCount = floatCount / 2;

        if (s_bufStereo.size() < floatCount) s_bufStereo.resize(floatCount);
        if (s_bufLeft.size() < floatCount) s_bufLeft.resize(floatCount);
        if (s_bufRight.size() < floatCount) s_bufRight.resize(floatCount);

        for (size_t i = 0; i < frameCount; ++i) {
            float sL = floatSamples[i * 2 + 0];
            float sR = floatSamples[i * 2 + 1];

            s_bufStereo[i * 2 + 0] = sL;
            s_bufStereo[i * 2 + 1] = sR;

            s_bufLeft[i * 2 + 0] = sL;
            s_bufLeft[i * 2 + 1] = sL;

            s_bufRight[i * 2 + 0] = sR;
            s_bufRight[i * 2 + 1] = sR;
        }

        uint32_t netLen = htonl(static_cast<uint32_t>(sizeBytes));
        size_t packetSize = 8 + sizeBytes;
        if (s_packetBuffer.size() < packetSize) s_packetBuffer.resize(packetSize);

        for (auto it = g_clientConnections.begin(); it != g_clientConnections.end(); ) {
            uint32_t netTag = htonl(it->modeTag);

            const float* srcBuf = s_bufStereo.data();
            if (it->modeTag == 1) srcBuf = s_bufLeft.data();
            else if (it->modeTag == 2) srcBuf = s_bufRight.data();

            std::memcpy(s_packetBuffer.data(), &netTag, 4);
            std::memcpy(s_packetBuffer.data() + 4, &netLen, 4);
            std::memcpy(s_packetBuffer.data() + 8, srcBuf, sizeBytes);

            ssize_t sent = send(it->socketFd, s_packetBuffer.data(), packetSize, MSG_NOSIGNAL);
            if (sent <= 0) {
                LOG_WARN("[Linux Server] Client disconnected: " + it->ipAddress);
                SendDesktopNotification("DeskSound Client Disconnected", "Client " + it->ipAddress + " disconnected.");
                close(it->socketFd);
                it = g_clientConnections.erase(it);
                ReevaluateAutoChannelModesLocked();
            } else {
                g_totalBytesSent.fetch_add(sent);
                ++it;
            }
        }
    }, "@DEFAULT_MONITOR@", cfg.bufferSize);

    if (!audioStarted) {
        LOG_ERROR("[Linux Server] Failed to initialize PulseAudio recorder.");
    }

    // Initialize TCP Server Socket
    g_serverSockFd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_serverSockFd < 0) {
        LOG_ERROR("[Linux Server] Failed to create TCP socket.");
        return 1;
    }

    int reuse = 1;
    setsockopt(g_serverSockFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(cfg.port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(g_serverSockFd, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        LOG_ERROR("[Linux Server] TCP Bind failed on port " + std::to_string(cfg.port));
    } else if (listen(g_serverSockFd, 5) < 0) {
        LOG_ERROR("[Linux Server] TCP Listen failed on port " + std::to_string(cfg.port));
    } else {
        LOG_INFO("[Linux Server] TCP Audio Server listening on port " + std::to_string(cfg.port));
    }

    // USB ADB Auto Reverse Thread
    std::thread adbThread(AutoRunAdbReverseLoop);

    // Update Checker
    CheckForUpdatesAsync();

    // UDP Discovery Thread (Port 5001)
    std::thread udpThread([cfg]() {
        g_udpSockFd = socket(AF_INET, SOCK_DGRAM, 0);
        if (g_udpSockFd < 0) return;

        int reuse = 1;
        setsockopt(g_udpSockFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        int broadcastEnable = 1;
        setsockopt(g_udpSockFd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

        sockaddr_in udpAddr{};
        udpAddr.sin_family = AF_INET;
        udpAddr.sin_port = htons(cfg.discoveryPort);
        udpAddr.sin_addr.s_addr = INADDR_ANY;

        if (bind(g_udpSockFd, (sockaddr*)&udpAddr, sizeof(udpAddr)) == 0) {
            LOG_INFO("[UDP Discovery] Listener active on port " + std::to_string(cfg.discoveryPort) + "...");
            char buf[256];
            while (g_running) {
                sockaddr_in clientAddr{};
                socklen_t addrLen = sizeof(clientAddr);
                ssize_t len = recvfrom(g_udpSockFd, buf, sizeof(buf) - 1, 0, (sockaddr*)&clientAddr, &addrLen);
                if (len > 0) {
                    buf[len] = '\0';
                    if (strstr(buf, "DESKSOUND_DISCOVER") || strstr(buf, "DISCOVER")) {
                        char clientIp[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, INET_ADDRSTRLEN);
                        LOG_INFO("[UDP Discovery] Discovery request received from: " + std::string(clientIp));

                        std::string resp = "DESKSOUND_SERVER|" + std::to_string(cfg.port);
                        sendto(g_udpSockFd, resp.c_str(), resp.length(), 0, (sockaddr*)&clientAddr, addrLen);
                    }
                }
            }
        }
        if (g_udpSockFd >= 0) {
            close(g_udpSockFd);
            g_udpSockFd = -1;
        }
    });

    // Accept Thread
    std::thread acceptThread([cfg]() {
        while (g_running) {
            if (g_serverSockFd < 0) break;
            sockaddr_in clientAddr{};
            socklen_t addrLen = sizeof(clientAddr);
            int clientSock = accept(g_serverSockFd, (sockaddr*)&clientAddr, &addrLen);
            if (clientSock >= 0) {
                // Ultra-smooth socket options (matching Windows Server)
                int nodelay = 1;
                setsockopt(clientSock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

#ifdef TCP_QUICKACK
                int quickack = 1;
                setsockopt(clientSock, IPPROTO_TCP, TCP_QUICKACK, &quickack, sizeof(quickack));
#endif

                int tos = 0x10; // IPTOS_LOWDELAY
                setsockopt(clientSock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));

                int sndBuf = 128 * 1024; // 128KB send buffer prevents Wi-Fi packet drops
                setsockopt(clientSock, SOL_SOCKET, SO_SNDBUF, &sndBuf, sizeof(sndBuf));

                char clientIp[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, INET_ADDRSTRLEN);
                LOG_INFO("[Linux Server] Client Connected: " + std::string(clientIp));
                SendDesktopNotification("DeskSound Client Connected", "New client connected: " + std::string(clientIp));

                std::lock_guard<std::mutex> lock(g_clientMutex);
                ClientConnection conn;
                conn.socketFd = clientSock;
                conn.ipAddress = clientIp;
                conn.channelMode = "STEREO";
                conn.modeTag = 0;
                g_clientConnections.push_back(conn);

                // Auto Left / Right mode split when 2 clients connect
                ReevaluateAutoChannelModesLocked();

                // Find assigned mode string for this client
                std::string initModeStr = g_clientConnections.back().channelMode;

                // Send 32-byte format header expected by DeskSound client: FORMAT|48000|2|32|MODE
                char formatHeader[33] = {0};
                snprintf(formatHeader, sizeof(formatHeader), "FORMAT|48000|2|32|%s", initModeStr.c_str());
                size_t headerLen = strlen(formatHeader);
                for (size_t i = headerLen; i < 32; ++i) formatHeader[i] = ' ';
                formatHeader[32] = '\0';
                send(clientSock, formatHeader, 32, MSG_NOSIGNAL);
            }
        }
    });

    // Initialize System Tray Icon if enabled
    if (cfg.enableTray) {
        TrayLinux::Instance().Initialize(
            []() { GuiLinux::Instance().ToggleVisibility(); },
            []() { 
                g_serverActive.store(!g_serverActive.load());
                LOG_INFO(g_serverActive.load() ? "Server Resumed" : "Server Paused");
            },
            []() {
                ServerConfig c = ConfigManager::Instance().GetConfig();
                c.muted = !c.muted;
                ConfigManager::Instance().UpdateConfig(c);
                g_audioRecorder.SetVolume(c.masterVolume, c.gainL, c.gainR, c.muted, c.mutedL, c.mutedR);
            },
            []() { ConfigManager::Instance().LoadConfig(); },
            []() { 
                StopServerAndExit();
                std::exit(0);
            }
        );
    }

    // Initialize GTK3 Desktop GUI if enabled
    bool guiRunning = false;
    if (cfg.enableGui) {
        guiRunning = GuiLinux::Instance().Initialize(argc, argv);
        if (guiRunning) {
            GuiLinux::Instance().SetOnServerToggle([](bool active) {
                g_serverActive.store(active);
            });
            GuiLinux::Instance().SetOnVolumeChanged([](float master, float gainL, float gainR, bool muted, bool mutedL, bool mutedR) {
                g_audioRecorder.SetVolume(master, gainL, gainR, muted, mutedL, mutedR);
                ServerConfig c = ConfigManager::Instance().GetConfig();
                c.masterVolume = master;
                c.gainL = gainL;
                c.gainR = gainR;
                c.muted = muted;
                c.mutedL = mutedL;
                c.mutedR = mutedR;
                ConfigManager::Instance().UpdateConfig(c);
                ConfigManager::Instance().SaveConfig();
            });
            GuiLinux::Instance().SetOnKickClient([](int clientIndex) {
                KickClient(static_cast<size_t>(clientIndex));
            });
            GuiLinux::Instance().SetOnClientModeChanged([](int clientIndex, int modeTag) {
                std::lock_guard<std::mutex> lock(g_clientMutex);
                if (clientIndex >= 0 && static_cast<size_t>(clientIndex) < g_clientConnections.size()) {
                    g_clientConnections[clientIndex].modeTag = static_cast<uint32_t>(modeTag);
                    if (modeTag == 1) g_clientConnections[clientIndex].channelMode = "LEFT";
                    else if (modeTag == 2) g_clientConnections[clientIndex].channelMode = "RIGHT";
                    else g_clientConnections[clientIndex].channelMode = "STEREO";
                    LOG_INFO("[Linux Server] Client " + std::to_string(clientIndex + 1) + " mode set to: " + g_clientConnections[clientIndex].channelMode);
                }
            });
            GuiLinux::Instance().SetOnQuit([]() {
                LOG_INFO("[Linux Server] GUI window close / Quit triggered.");
                StopServerAndExit();
                std::exit(0);
            });
        }
    }

    // Audio Peak Level Thread (50ms)
    std::thread peakThread([]() {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (GuiLinux::Instance().IsVisible()) {
                GuiLinux::Instance().UpdateAudioPeak(g_audioRecorder.GetAudioPeakLevel());
            }
        }
    });

    // Diagnostics & Status sync timer thread (Every 1 second)
    std::thread statusThread([cfg]() {
        uint64_t prevBytes = 0;
        auto prevTime = std::chrono::steady_clock::now();

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            auto now = std::chrono::steady_clock::now();
            double elapsedSec = std::chrono::duration<double>(now - prevTime).count();
            uint64_t curBytes = g_totalBytesSent.load();

            double bytesDiff = (curBytes >= prevBytes) ? (curBytes - prevBytes) : 0;
            double bitrateMbps = (elapsedSec > 0) ? ((bytesDiff * 8.0) / (elapsedSec * 1000000.0)) : 0.0;

            prevBytes = curBytes;
            prevTime = now;

            size_t bufSamples = g_audioRecorder.GetBufferSize();
            float bufLatencyMs = (static_cast<float>(bufSamples) / 48000.0f) * 1000.0f;
            float totalEstLatencyMs = bufLatencyMs + 12.0f; // Server buffer + ~12ms (Network & Android HAL)

            if (cfg.enableGui) {
                GuiLinux::Instance().UpdateDiagnostics(static_cast<float>(bitrateMbps), bufLatencyMs, totalEstLatencyMs);
            }

            std::vector<std::string> localIps = GetLocalIPAddresses();
            std::vector<ClientInfo> clientInfos;
            {
                std::lock_guard<std::mutex> lock(g_clientMutex);
                for (size_t i = 0; i < g_clientConnections.size(); ++i) {
                    clientInfos.push_back(ClientInfo{ static_cast<int>(i + 1), g_clientConnections[i].ipAddress, g_clientConnections[i].channelMode });
                }
            }

            bool active = g_serverActive.load();
            if (cfg.enableGui) {
                GuiLinux::Instance().UpdateStatus(active, localIps, clientInfos);
            }
            if (cfg.enableTray) {
                ServerConfig curCfg = ConfigManager::Instance().GetConfig();
                TrayLinux::Instance().UpdateStatus(active, clientInfos.size(), curCfg.muted);
            }
        }
    });

    LOG_INFO("[Linux Server] Crystal-clear low latency server running smoothly. Press Ctrl+C to stop.");

    // Run GUI Loop on main thread if initialized, otherwise block main thread
    if (guiRunning) {
        GuiLinux::Instance().RunLoop();
    } else {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // Cleanup
    LOG_INFO("[Linux Server] Cleaning up resources...");
    StopServerAndExit();

    if (adbThread.joinable()) adbThread.detach();
    if (udpThread.joinable()) udpThread.detach();
    if (acceptThread.joinable()) acceptThread.detach();
    if (peakThread.joinable()) peakThread.detach();
    if (statusThread.joinable()) statusThread.detach();

    if (cfg.enableTray) {
        TrayLinux::Instance().Cleanup();
    }

    LOG_INFO("[Linux Server] Shutdown complete. Goodbye!");
    std::exit(0);
}
