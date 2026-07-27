#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <mutex>

struct ServerConfig {
    float masterVolume = 100.0f;
    float gainL = 100.0f;
    float gainR = 100.0f;
    bool isMuted = false;
    bool isMutedL = false;
    bool isMutedR = false;
    size_t bufferSize = 1024;
    int selectedDeviceIndex = 0;
    bool runOnStartup = false;
    bool minimizeToTray = true;
};

class ConfigManager {
public:
    static ConfigManager& Instance();

    ServerConfig GetConfig();
    void UpdateConfig(const ServerConfig& config);
    void SaveConfig();
    void LoadConfig();
    void SetRunOnStartup(bool enable);

private:
    ConfigManager() { LoadConfig(); }
    std::mutex m_mutex;
    ServerConfig m_config;
};

#endif // CONFIG_MANAGER_H
