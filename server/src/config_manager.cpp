#include "../include/config_manager.h"
#include <fstream>
#include <sstream>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

ConfigManager& ConfigManager::Instance() {
    static ConfigManager instance;
    return instance;
}

ServerConfig ConfigManager::GetConfig() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

void ConfigManager::UpdateConfig(const ServerConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
}

void ConfigManager::SaveConfig() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ofstream file("desksound_server.ini");
    if (file.is_open()) {
        file << "[Server]\n";
        file << "MasterVolume=" << m_config.masterVolume << "\n";
        file << "GainL=" << m_config.gainL << "\n";
        file << "GainR=" << m_config.gainR << "\n";
        file << "Muted=" << (m_config.isMuted ? 1 : 0) << "\n";
        file << "MutedL=" << (m_config.isMutedL ? 1 : 0) << "\n";
        file << "MutedR=" << (m_config.isMutedR ? 1 : 0) << "\n";
        file << "BufferSize=" << m_config.bufferSize << "\n";
        file << "DeviceIndex=" << m_config.selectedDeviceIndex << "\n";
        file << "MinimizeToTray=" << (m_config.minimizeToTray ? 1 : 0) << "\n";
        file << "RunOnStartup=" << (m_config.runOnStartup ? 1 : 0) << "\n";
    }
}

void ConfigManager::LoadConfig() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ifstream file("desksound_server.ini");
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);

            if (key == "MasterVolume") m_config.masterVolume = std::stof(val);
            else if (key == "GainL") m_config.gainL = std::stof(val);
            else if (key == "GainR") m_config.gainR = std::stof(val);
            else if (key == "Muted") m_config.isMuted = (std::stoi(val) != 0);
            else if (key == "MutedL") m_config.isMutedL = (std::stoi(val) != 0);
            else if (key == "MutedR") m_config.isMutedR = (std::stoi(val) != 0);
            else if (key == "BufferSize") m_config.bufferSize = std::stoul(val);
            else if (key == "DeviceIndex") m_config.selectedDeviceIndex = std::stoi(val);
            else if (key == "MinimizeToTray") m_config.minimizeToTray = (std::stoi(val) != 0);
            else if (key == "RunOnStartup") m_config.runOnStartup = (std::stoi(val) != 0);
        }
    }
}

void ConfigManager::SetRunOnStartup(bool enable) {
#ifdef _WIN32
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(NULL, path, MAX_PATH);
            std::wstring cmd = L"\"" + std::wstring(path) + L"\" -silent";
            RegSetValueExW(hKey, L"YanichDeskSoundServer", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)(cmd.length() + 1) * sizeof(wchar_t));
        } else {
            RegDeleteValueW(hKey, L"YanichDeskSoundServer");
        }
        RegCloseKey(hKey);
    }
#endif
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.runOnStartup = enable;
}
