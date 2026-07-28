#ifndef NETWORK_SERVER_H
#define NETWORK_SERVER_H

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <cstdint>

enum ClientChannelMode { CLIENT_MODE_STEREO = 0, CLIENT_MODE_LEFT = 1, CLIENT_MODE_RIGHT = 2 };

struct ClientSession {
    int id;
    std::string ip;
    ClientChannelMode channelMode;
#ifdef _WIN32
    uintptr_t socketFd;
#else
    int socketFd;
#endif
};

class NetworkServer {
public:
    static NetworkServer& Instance();

    bool Start(int port = 5000, int discoveryPort = 5001);
    void Stop();

    void BroadcastAudio(const float* samples, size_t frames, int channels, int sampleRate);
    void KickClient(int index);
    void SetClientChannelMode(int index, ClientChannelMode mode);
    void SwapClientChannels();

    std::vector<std::string> GetLocalIPs();
    std::vector<ClientSession> GetClients();
    bool IsActive() const { return m_active.load(); }
    void SetActive(bool active) { m_active.store(active); }

    float GetBitrateMbps() const { return m_bitrateMbps.load(); }
    int GetPacketsPerSec() const { return m_packetsPerSec.load(); }

private:
    NetworkServer();
    ~NetworkServer();

    void AcceptThread();
    void UdpDiscoveryThread();
    void AdbReverseThread();

    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_active{ true };
    int m_port{ 5000 };
    int m_discoveryPort{ 5001 };

    std::mutex m_clientMutex;
    std::vector<ClientSession> m_clients;
    std::vector<std::string> m_localIps;

    std::atomic<float> m_bitrateMbps{ 0.8f };
    std::atomic<int> m_packetsPerSec{ 0 };
    std::atomic<uint64_t> m_totalBytesSent{ 0 };

    std::thread m_acceptThread;
    std::thread m_udpThread;
    std::thread m_adbThread;

#ifdef _WIN32
    uintptr_t m_listenSocket;
    uintptr_t m_udpSocket{ (uintptr_t)-1 };
#else
    int m_listenSocket;
    int m_udpSocket{ -1 };
#endif
};

#endif // NETWORK_SERVER_H
