#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <mutex>

struct ServerConfig {
    int port = 5000;
    int discoveryPort = 5001;

    float masterVolume = 100.0f;
    float gainL = 100.0f;
    float gainR = 100.0f;
    bool muted = false;
    bool mutedL = false;
    bool mutedR = false;
    size_t bufferSize = 1024;
    int selectedDeviceIndex = 0;

    bool tcpNodelay = true;
    int sendBufSize = 65536;

    bool enableGui = true;
    bool enableTray = true;
    bool minimizeToTray = true;
    bool runOnStartup = false;
    std::string logLevel = "INFO";
};

class ConfigManager {
public:
    static ConfigManager& Instance();

    bool LoadConfig(const std::string& filepath = "desksound_server.ini");
    bool SaveConfig(const std::string& filepath = "desksound_server.ini");

    ServerConfig GetConfig();
    void UpdateConfig(const ServerConfig& cfg);
    void SetRunOnStartup(bool enable);

    std::string GetConfigPath() const { return m_filepath; }

private:
    ConfigManager() = default;
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    ServerConfig m_config;
    std::string m_filepath = "desksound_server.ini";
    std::mutex m_mutex;
};

#endif // CONFIG_MANAGER_H
