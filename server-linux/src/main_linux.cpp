#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "audio_pulse.h"

#define PORT 5000
#define DISCOVERY_PORT 5001

std::atomic<bool> g_running{true};
std::vector<int> g_clientSockets;
std::mutex g_clientMutex;

void UDPDiscoveryServer() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return;

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(DISCOVERY_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(sock);
        return;
    }

    std::cout << "[Linux Server] UDP Discovery Listener active on port " << DISCOVERY_PORT << "..." << std::endl;

    char buffer[256];
    sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);

    while (g_running) {
        ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&clientAddr, &addrLen);
        if (len > 0) {
            buffer[len] = '\0';
            if (strstr(buffer, "DESKSOUND_DISCOVERY")) {
                std::string response = "DESKSOUND_SERVER_PORT:5000";
                sendto(sock, response.c_str(), response.length(), 0, (sockaddr*)&clientAddr, addrLen);
            }
        }
    }
    close(sock);
}

void BroadcastAudioChunk(const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(g_clientMutex);
    for (auto it = g_clientSockets.begin(); it != g_clientSockets.end(); ) {
        ssize_t sent = send(*it, data, size, MSG_NOSIGNAL);
        if (sent < 0) {
            close(*it);
            it = g_clientSockets.erase(it);
        } else {
            ++it;
        }
    }
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << " 🔊 Yanich DeskSound Linux Server v1.2.1" << std::endl;
    std::cout << "==================================================" << std::endl;

    std::thread discoveryThread(UDPDiscoveryServer);

    PulseAudioRecorder recorder;
    if (!recorder.StartCapture([](const uint8_t* data, size_t size) {
        BroadcastAudioChunk(data, size);
    })) {
        std::cerr << "[Linux Server] Failed to initialize PulseAudio capture." << std::endl;
    }

    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        std::cerr << "[Linux Server] Failed to create TCP socket." << std::endl;
        g_running = false;
        if (discoveryThread.joinable()) discoveryThread.join();
        return 1;
    }

    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "[Linux Server] TCP Bind failed on port " << PORT << std::endl;
        g_running = false;
        if (discoveryThread.joinable()) discoveryThread.join();
        close(serverSock);
        return 1;
    }

    listen(serverSock, 5);
    std::cout << "[Linux Server] TCP Audio Server active on port " << PORT << "..." << std::endl;

    while (g_running) {
        sockaddr_in clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);
        int clientSock = accept(serverSock, (sockaddr*)&clientAddr, &addrLen);
        if (clientSock >= 0) {
            char clientIp[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, INET_ADDRSTRLEN);
            std::cout << "[Linux Server] Client Connected: " << clientIp << std::endl;
            
            std::lock_guard<std::mutex> lock(g_clientMutex);
            g_clientSockets.push_back(clientSock);
        }
    }

    g_running = false;
    if (discoveryThread.joinable()) discoveryThread.join();
    close(serverSock);
    return 0;
}
