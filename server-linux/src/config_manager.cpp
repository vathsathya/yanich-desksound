#include "config_manager.h"
#include "logger.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

static std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

ConfigManager& ConfigManager::Instance() {
    static ConfigManager instance;
    return instance;
}

ServerConfig ConfigManager::GetConfig() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

void ConfigManager::UpdateConfig(const ServerConfig& cfg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = cfg;
}

void ConfigManager::SetRunOnStartup(bool enable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.runOnStartup = enable;

    const char* homeDir = std::getenv("HOME");
    if (!homeDir) return;

    std::string autostartDir = std::string(homeDir) + "/.config/autostart";
    std::string desktopFilePath = autostartDir + "/yanich-desksound.desktop";

    if (enable) {
        mkdir((std::string(homeDir) + "/.config").c_str(), 0755);
        mkdir(autostartDir.c_str(), 0755);

        char exePath[1024];
        ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        std::string binaryPath = (len > 0) ? std::string(exePath, len) : "yanich-desksound";

        std::ofstream file(desktopFilePath);
        if (file.is_open()) {
            file << "[Desktop Entry]\n";
            file << "Type=Application\n";
            file << "Name=Yanich DeskSound Server\n";
            file << "Comment=Stream high quality audio between desktop and mobile\n";
            file << "Exec=" << binaryPath << "\n";
            file << "Icon=audio-volume-high\n";
            file << "Terminal=false\n";
            file << "Categories=Audio;Utility;\n";
            file << "X-GNOME-Autostart-enabled=true\n";
            file.close();
            LOG_INFO("[Config] Created autostart entry: " + desktopFilePath);
        }
    } else {
        std::remove(desktopFilePath.c_str());
        LOG_INFO("[Config] Removed autostart entry: " + desktopFilePath);
    }
}

bool ConfigManager::LoadConfig(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_filepath = filepath;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_WARN("Config file not found: " + filepath + ". Using default settings.");
        return false;
    }

    std::string line;
    std::string currentSection;

    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            currentSection = Trim(line.substr(1, line.length() - 2));
            continue;
        }

        size_t eqPos = line.find('=');
        if (eqPos != std::string::npos) {
            std::string key = Trim(line.substr(0, eqPos));
            std::string val = Trim(line.substr(eqPos + 1));

            if (currentSection == "Server") {
                if (key == "port" || key == "Port") m_config.port = std::stoi(val);
                else if (key == "discovery_port" || key == "DiscoveryPort") m_config.discoveryPort = std::stoi(val);
            } else if (currentSection == "Audio") {
                if (key == "master_volume" || key == "MasterVolume") m_config.masterVolume = std::stof(val);
                else if (key == "gain_l" || key == "GainL") m_config.gainL = std::stof(val);
                else if (key == "gain_r" || key == "GainR") m_config.gainR = std::stof(val);
                else if (key == "muted" || key == "Muted") m_config.muted = (std::stoi(val) != 0);
                else if (key == "muted_l" || key == "MutedL") m_config.mutedL = (std::stoi(val) != 0);
                else if (key == "muted_r" || key == "MutedR") m_config.mutedR = (std::stoi(val) != 0);
                else if (key == "buffer_size" || key == "BufferSize") m_config.bufferSize = std::stoul(val);
                else if (key == "selected_device" || key == "SelectedDeviceIndex") m_config.selectedDeviceIndex = std::stoi(val);
            } else if (currentSection == "Network") {
                if (key == "tcp_nodelay" || key == "TCP_NODELAY") m_config.tcpNodelay = (std::stoi(val) != 0);
                else if (key == "send_buf_size" || key == "SendBufSize") m_config.sendBufSize = std::stoi(val);
            } else if (currentSection == "System") {
                if (key == "enable_gui" || key == "EnableGUI") m_config.enableGui = (std::stoi(val) != 0);
                else if (key == "enable_tray" || key == "EnableTray") m_config.enableTray = (std::stoi(val) != 0);
                else if (key == "minimize_to_tray" || key == "MinimizeToTray") m_config.minimizeToTray = (std::stoi(val) != 0);
                else if (key == "run_on_startup" || key == "RunOnStartup") m_config.runOnStartup = (std::stoi(val) != 0);
                else if (key == "log_level" || key == "LogLevel") m_config.logLevel = val;
            }
        }
    }

    LOG_INFO("Loaded config successfully from " + filepath);
    return true;
}

bool ConfigManager::SaveConfig(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string path = filepath.empty() ? m_filepath : filepath;

    std::ofstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Failed to write config file: " + path);
        return false;
    }

    file << "[Server]\n";
    file << "Port=" << m_config.port << "\n";
    file << "DiscoveryPort=" << m_config.discoveryPort << "\n\n";

    file << "[Audio]\n";
    file << "MasterVolume=" << m_config.masterVolume << "\n";
    file << "GainL=" << m_config.gainL << "\n";
    file << "GainR=" << m_config.gainR << "\n";
    file << "Muted=" << (m_config.muted ? 1 : 0) << "\n";
    file << "MutedL=" << (m_config.mutedL ? 1 : 0) << "\n";
    file << "MutedR=" << (m_config.mutedR ? 1 : 0) << "\n";
    file << "BufferSize=" << m_config.bufferSize << "\n";
    file << "SelectedDeviceIndex=" << m_config.selectedDeviceIndex << "\n\n";

    file << "[Network]\n";
    file << "TCP_NODELAY=" << (m_config.tcpNodelay ? 1 : 0) << "\n";
    file << "SendBufSize=" << m_config.sendBufSize << "\n\n";

    file << "[System]\n";
    file << "EnableGUI=" << (m_config.enableGui ? 1 : 0) << "\n";
    file << "EnableTray=" << (m_config.enableTray ? 1 : 0) << "\n";
    file << "MinimizeToTray=" << (m_config.minimizeToTray ? 1 : 0) << "\n";
    file << "RunOnStartup=" << (m_config.runOnStartup ? 1 : 0) << "\n";
    file << "LogLevel=" << m_config.logLevel << "\n";

    file.close();
    LOG_INFO("Saved config successfully to " + path);
    return true;
}
