#define NOMINMAX
#include "../include/gui_app.h"
#include "../include/config_manager.h"
#include "../include/network_server.h"
#include "../include/logger.h"
#include "../thirdparty/imgui/imgui.h"

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
static void CopyToClipboard(const std::string& text) {
    if (!OpenClipboard(NULL)) return;
    EmptyClipboard();
    size_t bytes = text.length() + 1;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* ptr = GlobalLock(hMem);
        if (ptr) {
            memcpy(ptr, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_TEXT, hMem);
        }
    }
    CloseClipboard();
}
#else
static void CopyToClipboard(const std::string& text) {
    ImGui::SetClipboardText(text.c_str());
}
#endif

GuiApp& GuiApp::Instance() {
    static GuiApp instance;
    return instance;
}

void GuiApp::Initialize() {
    // --- Setup Clean Dark Obsidian & Slate ImGui Design System ---
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 5.0f;
    style.PopupRounding     = 6.0f;
    style.GrabRounding      = 5.0f;
    style.ItemSpacing       = ImVec2(8.0f, 7.0f);
    style.WindowPadding     = ImVec2(14.0f, 14.0f);
    style.FramePadding      = ImVec2(8.0f, 5.0f);
    style.ScrollbarRounding = 6.0f;
    style.ScrollbarSize     = 10.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]             = ImVec4(0.06f, 0.09f, 0.16f, 1.00f); // #0f172a Slate 900
    colors[ImGuiCol_ChildBg]              = ImVec4(0.10f, 0.14f, 0.22f, 1.00f); // #1a2334 Dark Slate Card
    colors[ImGuiCol_Border]               = ImVec4(0.20f, 0.27f, 0.38f, 1.00f); // #334155 Slate Border
    colors[ImGuiCol_Text]                 = ImVec4(0.97f, 0.98f, 0.99f, 1.00f); // #f8fafc Primary Text
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.58f, 0.64f, 0.72f, 1.00f); // #94a3b8 Secondary Text
    colors[ImGuiCol_FrameBg]              = ImVec4(0.16f, 0.21f, 0.30f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.29f, 0.40f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.26f, 0.34f, 0.46f, 1.00f);
    colors[ImGuiCol_Button]               = ImVec4(0.18f, 0.24f, 0.35f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.24f, 0.32f, 0.45f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.15f, 0.39f, 0.92f, 1.00f);
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.23f, 0.51f, 0.96f, 1.00f); // #3b82f6 Blue Slider
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.38f, 0.65f, 0.98f, 1.00f);
    colors[ImGuiCol_Header]               = ImVec4(0.18f, 0.24f, 0.35f, 1.00f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.24f, 0.32f, 0.45f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.15f, 0.39f, 0.92f, 1.00f);
}

void GuiApp::RenderUI(AudioBackend* audioBackend) {
    ServerConfig cfg = ConfigManager::Instance().GetConfig();
    bool cfgChanged = false;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("YanichDeskSoundMain", nullptr, windowFlags);

    // --- 1. Top Header Bar & Live Metrics ---
    ImGui::TextColored(ImVec4(0.97f, 0.98f, 0.99f, 1.00f), "Yanich DeskSound");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.58f, 0.64f, 0.72f, 1.00f), "v1.2.7");

    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 90.0f);
    if (ImGui::Button("Logs 📜", ImVec2(76.0f, 24.0f))) {
        m_showLogsModal = true;
    }

    // Top Metrics Badges Bar
    bool isServerActive = NetworkServer::Instance().IsActive();
    auto clients = NetworkServer::Instance().GetClients();
    float bitrate = NetworkServer::Instance().GetBitrateMbps();

    if (isServerActive) {
        ImGui::TextColored(ImVec4(0.06f, 0.73f, 0.49f, 1.00f), "● RUNNING (Port 5000)");
    } else {
        ImGui::TextColored(ImVec4(0.94f, 0.27f, 0.27f, 1.00f), "○ STOPPED");
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.23f, 0.51f, 0.96f, 1.00f), "|  ⚡ %.1f Mbps", bitrate);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.55f, 0.36f, 0.96f, 1.00f), "|  👥 Clients: %d", (int)clients.size());

    ImGui::Separator();
    ImGui::Spacing();

    // --- 2. GROUP 1: AUDIO CONTROLS & DUAL VU METERS ---
    ImGui::TextColored(ImVec4(0.48f, 0.56f, 0.68f, 1.00f), "AUDIO CONTROLS & DUAL VU METERS");
    ImGui::BeginChild("AudioCard", ImVec2(0, 245), true, ImGuiWindowFlags_None);

    // Dual Stereo VU LED Level Meters (Left & Right)
    float peakL = audioBackend ? audioBackend->GetPeakLevelL() : 0.0f;
    float peakR = audioBackend ? audioBackend->GetPeakLevelR() : 0.0f;
    if (!isServerActive) { peakL = 0.0f; peakR = 0.0f; }

    float barW = ImGui::GetContentRegionAvail().x - 70.0f;

    // Left Channel VU Meter Bar
    ImGui::Text("L");
    ImGui::SameLine(25.0f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.02f, 0.71f, 0.83f, 1.00f)); // Cyan #06b6d4
    ImGui::ProgressBar((std::min)(1.0f, peakL * 2.2f), ImVec2(barW, 14.0f), "");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.58f, 0.64f, 0.72f, 1.00f), "%d%%", (int)(peakL * 100.0f));

    // Right Channel VU Meter Bar
    ImGui::Text("R");
    ImGui::SameLine(25.0f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.06f, 0.73f, 0.49f, 1.00f)); // Emerald #10b981
    ImGui::ProgressBar((std::min)(1.0f, peakR * 2.2f), ImVec2(barW, 14.0f), "");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.58f, 0.64f, 0.72f, 1.00f), "%d%%", (int)(peakR * 100.0f));

    ImGui::Spacing();

    // Server Active Toggle Button
    if (isServerActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.94f, 0.27f, 0.27f, 1.00f));
        if (ImGui::Button("⏹ STOP SERVER", ImVec2(120.0f, 24.0f))) {
            NetworkServer::Instance().SetActive(false);
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.73f, 0.49f, 1.00f));
        if (ImGui::Button("▶ START SERVER", ImVec2(120.0f, 24.0f))) {
            NetworkServer::Instance().SetActive(true);
        }
        ImGui::PopStyleColor();
    }

    // Row 1: Master Volume Slider
    ImGui::Text("Master:");
    ImGui::SameLine(90.0f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 140.0f);
    if (ImGui::SliderFloat("##MasterVol", &cfg.masterVolume, 0.0f, 100.0f, "%.0f%%")) {
        cfgChanged = true;
    }
    ImGui::SameLine();
    if (cfg.isMuted) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.94f, 0.27f, 0.27f, 1.00f));
        if (ImGui::Button("🔇 Muted##M", ImVec2(75.0f, 0))) { cfg.isMuted = false; cfgChanged = true; }
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button("🔊 Mute##M", ImVec2(75.0f, 0))) { cfg.isMuted = true; cfgChanged = true; }
    }

    // Row 2: Gain Left Slider
    ImGui::Text("Gain L:");
    ImGui::SameLine(90.0f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 140.0f);
    if (ImGui::SliderFloat("##GainL", &cfg.gainL, 0.0f, 100.0f, "%.0f%%")) {
        cfgChanged = true;
    }
    ImGui::SameLine();
    if (cfg.isMutedL) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.94f, 0.27f, 0.27f, 1.00f));
        if (ImGui::Button("🔇 Mute L", ImVec2(75.0f, 0))) { cfg.isMutedL = false; cfgChanged = true; }
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button("🔊 Mute L", ImVec2(75.0f, 0))) { cfg.isMutedL = true; cfgChanged = true; }
    }

    // Row 3: Gain Right Slider
    ImGui::Text("Gain R:");
    ImGui::SameLine(90.0f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 140.0f);
    if (ImGui::SliderFloat("##GainR", &cfg.gainR, 0.0f, 100.0f, "%.0f%%")) {
        cfgChanged = true;
    }
    ImGui::SameLine();
    if (cfg.isMutedR) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.94f, 0.27f, 0.27f, 1.00f));
        if (ImGui::Button("🔇 Mute R", ImVec2(75.0f, 0))) { cfg.isMutedR = false; cfgChanged = true; }
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button("🔊 Mute R", ImVec2(75.0f, 0))) { cfg.isMutedR = true; cfgChanged = true; }
    }

    // Row 4: Audio Source Device Dropdown
    ImGui::Text("Audio Source:");
    ImGui::SameLine(110.0f);
    auto devices = audioBackend ? audioBackend->EnumerateDevices() : std::vector<AudioDeviceInfo>{};
    std::string currentDevName = (cfg.selectedDeviceIndex >= 0 && cfg.selectedDeviceIndex < (int)devices.size()) ? devices[cfg.selectedDeviceIndex].name : "Default Playback Device";
    
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginCombo("##AudioSource", currentDevName.c_str())) {
        for (const auto& dev : devices) {
            bool isSelected = (cfg.selectedDeviceIndex == dev.index);
            if (ImGui::Selectable(dev.name.c_str(), isSelected)) {
                cfg.selectedDeviceIndex = dev.index;
                if (audioBackend) audioBackend->SelectDevice(dev.index);
                cfgChanged = true;
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // Row 5: Buffer Size Selector Dropdown
    ImGui::Text("Buffer Size:");
    ImGui::SameLine(110.0f);
    const char* bufferItems[] = {
        "128 samples (~2.6ms, Instant)",
        "256 samples (~5.3ms, Low)",
        "512 samples (~10.6ms, Fast)",
        "1024 samples (~21.3ms, Smooth Default)",
        "2048 samples (~42.6ms, Safe)"
    };
    int currentBufIdx = 3;
    if (cfg.bufferSize <= 128) currentBufIdx = 0;
    else if (cfg.bufferSize <= 256) currentBufIdx = 1;
    else if (cfg.bufferSize <= 512) currentBufIdx = 2;
    else if (cfg.bufferSize <= 1024) currentBufIdx = 3;
    else currentBufIdx = 4;

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginCombo("##BufferSize", bufferItems[currentBufIdx])) {
        for (int i = 0; i < 5; ++i) {
            bool isSel = (currentBufIdx == i);
            if (ImGui::Selectable(bufferItems[i], isSel)) {
                size_t newBuf = 1024;
                if (i == 0) newBuf = 128;
                else if (i == 1) newBuf = 256;
                else if (i == 2) newBuf = 512;
                else if (i == 3) newBuf = 1024;
                else if (i == 4) newBuf = 2048;

                cfg.bufferSize = newBuf;
                if (audioBackend) audioBackend->SetBufferSize(newBuf);
                cfgChanged = true;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::EndChild();

    // --- 3. GROUP 2: NETWORK & CONNECTED CLIENTS ---
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.48f, 0.56f, 0.68f, 1.00f), "NETWORK & CONNECTED CLIENTS");
    ImGui::BeginChild("NetCard", ImVec2(0, 105), true, ImGuiWindowFlags_None);

    ImGui::Text("Server IP:");
    ImGui::SameLine();
    auto ips = NetworkServer::Instance().GetLocalIPs();
    static int copiedIdx = -1;
    for (size_t idx = 0; idx < ips.size() && idx < 3; ++idx) {
        std::string btnLabel = ((int)idx == copiedIdx) ? "Copied!" : ips[idx];
        if (ImGui::Button(btnLabel.c_str())) {
            CopyToClipboard(ips[idx]);
            copiedIdx = (int)idx;
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    if (clients.empty()) {
        ImGui::TextColored(ImVec4(0.58f, 0.64f, 0.72f, 1.00f), "No active clients connected. Open DeskSound app on phone.");
    } else {
        for (size_t i = 0; i < clients.size(); ++i) {
            ImGui::Text("Client #%d: %s", clients[i].id, clients[i].ip.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 140.0f);

            const char* modes[] = { "L+R", "L", "R" };
            ImGui::SetNextItemWidth(65.0f);
            std::string comboId = "##Mode" + std::to_string(i);
            if (ImGui::BeginCombo(comboId.c_str(), modes[(int)clients[i].channelMode])) {
                for (int m = 0; m < 3; ++m) {
                    if (ImGui::Selectable(modes[m], (int)clients[i].channelMode == m)) {
                        NetworkServer::Instance().SetClientChannelMode((int)i, (ClientChannelMode)m);
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.94f, 0.27f, 0.27f, 1.00f));
            std::string kickId = "Kick ✕##" + std::to_string(i);
            if (ImGui::Button(kickId.c_str(), ImVec2(60.0f, 0))) {
                NetworkServer::Instance().KickClient((int)i);
            }
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndChild();

    // --- 4. Bottom System Checkboxes & Status Bar ---
    ImGui::Spacing();
    if (ImGui::Checkbox("Minimize to System Tray on close", &cfg.minimizeToTray)) {
        cfgChanged = true;
    }
    if (ImGui::Checkbox("Start automatically on system login", &cfg.runOnStartup)) {
        ConfigManager::Instance().SetRunOnStartup(cfg.runOnStartup);
        cfgChanged = true;
    }

    if (cfgChanged) {
        ConfigManager::Instance().UpdateConfig(cfg);
        ConfigManager::Instance().SaveConfig();
    }

    // Modal Event Log History Dialog
    if (m_showLogsModal) {
        ImGui::OpenPopup("Event Log History");
    }

    if (ImGui::BeginPopupModal("Event Log History", &m_showLogsModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::BeginChild("LogRegion", ImVec2(500, 320), true);
        auto logs = GetLogHistoryVector();
        for (const auto& line : logs) {
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::EndChild();

        if (ImGui::Button("Copy Logs", ImVec2(120, 0))) {
            std::wstring logsW = GetLogHistory();
            std::string logsA(logsW.begin(), logsW.end());
            CopyToClipboard(logsA);
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Logs", ImVec2(120, 0))) {
            ClearLogHistory();
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100.0f);
        if (ImGui::Button("Close", ImVec2(100, 0))) {
            m_showLogsModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}
