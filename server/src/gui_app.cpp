#define NOMINMAX
#include "../include/gui_app.h"
#include "../include/config_manager.h"
#include "../include/network_server.h"
#include "../include/logger.h"
#include "../include/DesignTokens.h"
#include "../include/custom_widgets.h"
#include "../thirdparty/imgui/imgui.h"

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>

using namespace DesignSystem;
using namespace DesignTokens;

GuiApp& GuiApp::Instance() {
    static GuiApp instance;
    return instance;
}

void GuiApp::Initialize() {
    // --- Setup Design System Tokens in Dear ImGui ---
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = CardRadius;
    style.ChildRounding     = CardRadius;
    style.FrameRounding     = FrameRadius;
    style.PopupRounding     = FrameRadius + 2.0f;
    style.GrabRounding      = FrameRadius - 2.0f;
    style.ItemSpacing       = ImVec2(ControlSpacing, ControlSpacing - 2.0f);
    style.WindowPadding     = ImVec2(CardPadding, CardPadding);
    style.FramePadding      = ImVec2(10.0f, 6.0f);
    style.ScrollbarRounding = FrameRadius;
    style.ScrollbarSize     = 10.0f;
    style.WindowBorderSize  = 0.0f;
    style.ChildBorderSize   = 1.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]             = BgMain;             // #0F172A
    colors[ImGuiCol_ChildBg]              = CardBg;             // #1E293B
    colors[ImGuiCol_Border]               = BorderColor;        // rgba(255,255,255,0.05)
    colors[ImGuiCol_Text]                 = TextPrimary;        // #F8FAFC
    colors[ImGuiCol_TextDisabled]         = TextSecondary;      // #94A3B8
    colors[ImGuiCol_FrameBg]              = CardElevated;       // #243247
    colors[ImGuiCol_FrameBgHovered]       = HoverGlow;
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.22f, 0.30f, 0.44f, 1.00f);
    colors[ImGuiCol_Button]               = CardElevated;
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.20f, 0.28f, 0.40f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = AccentPrimary;
    colors[ImGuiCol_SliderGrab]           = AccentPrimary;      // #22D3EE
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.38f, 0.88f, 0.98f, 1.00f);
    colors[ImGuiCol_Header]               = CardElevated;
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.20f, 0.28f, 0.40f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = AccentPrimary;
    colors[ImGuiCol_PopupBg]              = BgSecondary;
}

void GuiApp::RenderUI(AudioBackend* audioBackend) {
    ServerConfig cfg = ConfigManager::Instance().GetConfig();
    bool cfgChanged = false;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    // Root Main Window Flags - Only main window allows vertical scrolling when needed
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("YanichDeskSoundMain", nullptr, windowFlags);

    bool isServerActive = NetworkServer::Instance().IsActive();
    auto clients = NetworkServer::Instance().GetClients();
    float bitrate = NetworkServer::Instance().GetBitrateMbps();

    // --- 1. HEADER BAR ---
    ImGui::BeginGroup();
    ImGui::SetCursorPosY(16.0f);
    
    // Waveform Brand Icon
    ImGui::TextColored(AccentPrimary, " ~|~|~ ");
    ImGui::SameLine();
    
    // App Title (26px Hierarchy)
    ImGui::SetWindowFontScale(1.25f);
    ImGui::TextColored(TextPrimary, "Yanich DeskSound");
    ImGui::SetWindowFontScale(1.0f);

    ImGui::SameLine();
    ImGui::SetCursorPosY(18.0f);
    VersionBadge("v1.2.7");

    // Subtitle & Status Badge
    ImGui::SetCursorPos(ImVec2(OuterMargin, 44.0f));
    ImGui::TextColored(TextSecondary, "Desktop Audio Streaming Server");
    ImGui::SameLine();
    ImGui::SetCursorPosY(44.0f);
    StatusDotBadge(isServerActive);

    // Right Ghost Action Buttons
    float windowW = ImGui::GetWindowWidth();
    ImGui::SetCursorPos(ImVec2(windowW - 250.0f, 20.0f));
    
    if (GhostButton("Logs", ImVec2(70.0f, 30.0f))) {
        m_showLogsModal = true;
    }
    ImGui::SameLine();
    if (GhostButton("Settings", ImVec2(75.0f, 30.0f))) {
        // Settings Action
    }
    ImGui::SameLine();
    if (GhostButton("About", ImVec2(65.0f, 30.0f))) {
        // About Action
    }
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- 2. CARD 1: AUDIO CONTROLS (height: auto, zero fixed-height, zero internal scrollbar) ---
    BeginCard("AudioCard");
    
    CardHeader("(((o)))", "Audio Controls");

    // Dual Studio Segmented LED VU Meters (Left Cyan, Right Emerald)
    float rawPeakL = audioBackend ? audioBackend->GetPeakLevelL() : 0.0f;
    float rawPeakR = audioBackend ? audioBackend->GetPeakLevelR() : 0.0f;
    if (!isServerActive) { rawPeakL = 0.0f; rawPeakR = 0.0f; }

    static float smoothL = 0.0f, peakHoldL = 0.0f;
    static float smoothR = 0.0f, peakHoldR = 0.0f;

    LEDVuMeter("LEFT", rawPeakL, smoothL, peakHoldL, AccentPrimary, 36);
    LEDVuMeter("RIGHT", rawPeakR, smoothR, peakHoldR, AccentSecondary, 36);

    ImGui::Spacing();

    // Commercial 42px Server Power Button
    if (DrawServerButton(isServerActive)) {
        NetworkServer::Instance().SetActive(!isServerActive);
    }

    ImGui::Spacing();

    // Audio Sliders with Double-Click Reset
    float labelW = 90.0f;
    float btnMuteW = 65.0f;
    float sliderW = ImGui::GetContentRegionAvail().x - labelW - btnMuteW - 80.0f;

    // Master Volume Slider
    ImGui::TextColored(TextPrimary, "Master");
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(sliderW);
    if (ModernSlider("Master", "##MasterVol", &cfg.masterVolume, 0.0f, 100.0f, 100.0f, "%.0f%%")) {
        cfgChanged = true;
    }
    ImGui::SameLine();
    if (cfg.isMuted) {
        ImGui::PushStyleColor(ImGuiCol_Button, ColorDanger);
        if (ImGui::Button("Muted##M", ImVec2(btnMuteW, 0))) { cfg.isMuted = false; cfgChanged = true; }
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button("Mute##M", ImVec2(btnMuteW, 0))) { cfg.isMuted = true; cfgChanged = true; }
    }

    // Gain Left Slider
    ImGui::TextColored(AccentPrimary, "Gain L");
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(sliderW);
    if (ModernSlider("Gain L", "##GainL", &cfg.gainL, 0.0f, 100.0f, 100.0f, "%.0f%%")) {
        cfgChanged = true;
    }
    ImGui::SameLine();
    if (cfg.isMutedL) {
        ImGui::PushStyleColor(ImGuiCol_Button, ColorDanger);
        if (ImGui::Button("Mute L##L", ImVec2(btnMuteW, 0))) { cfg.isMutedL = false; cfgChanged = true; }
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button("Mute L##L", ImVec2(btnMuteW, 0))) { cfg.isMutedL = true; cfgChanged = true; }
    }

    // Gain Right Slider
    ImGui::TextColored(AccentSecondary, "Gain R");
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(sliderW);
    if (ModernSlider("Gain R", "##GainR", &cfg.gainR, 0.0f, 100.0f, 100.0f, "%.0f%%")) {
        cfgChanged = true;
    }
    ImGui::SameLine();
    if (cfg.isMutedR) {
        ImGui::PushStyleColor(ImGuiCol_Button, ColorDanger);
        if (ImGui::Button("Mute R##R", ImVec2(btnMuteW, 0))) { cfg.isMutedR = false; cfgChanged = true; }
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button("Mute R##R", ImVec2(btnMuteW, 0))) { cfg.isMutedR = true; cfgChanged = true; }
    }

    // Audio Device Dropdown
    ImGui::TextColored(TextSecondary, "Audio Source");
    ImGui::SameLine(labelW);
    auto devices = audioBackend ? audioBackend->EnumerateDevices() : std::vector<AudioDeviceInfo>{};
    std::string currentDevName = (cfg.selectedDeviceIndex >= 0 && cfg.selectedDeviceIndex < (int)devices.size()) ? devices[cfg.selectedDeviceIndex].name : "Default System Playback Device";
    
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

    // Buffer Size Dropdown
    ImGui::TextColored(TextSecondary, "Buffer Size");
    ImGui::SameLine(labelW);
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

    EndCard();

    // --- 3. RESPONSIVE GRID: CARD 2 (NETWORK) & CARD 3 (PERFORMANCE) ---
    float availW = ImGui::GetContentRegionAvail().x;
    bool isWideLayout = (availW >= 780.0f);

    float card23W = isWideLayout ? (availW - 16.0f) * 0.5f : availW;

    // LEFT / TOP: CARD 2 (NETWORK - height: auto)
    BeginCard("NetworkCard", card23W);
    CardHeader("[NET]", "Network");

    ImGui::TextColored(TextSecondary, "Server IP");
    auto ips = NetworkServer::Instance().GetLocalIPs();
    for (size_t idx = 0; idx < ips.size() && idx < 2; ++idx) {
        IPChip(("ip_" + std::to_string(idx)).c_str(), ips[idx]);
        ImGui::SameLine();
    }
    ImGui::NewLine();

    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(TextSecondary, "Connected Clients");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 30.0f);
    ImGui::TextColored(AccentPrimary, "%d", (int)clients.size());

    if (clients.empty()) {
        EmptyState(ImVec2(ImGui::GetContentRegionAvail().x - CardPadding * 2.0f, 140.0f), "No devices connected", "Open Yanich DeskSound on your mobile device.");
    } else {
        for (size_t i = 0; i < clients.size(); ++i) {
            ImGui::Text("[Phone] Client #%d: %s", clients[i].id, clients[i].ip.c_str());
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 65.0f);
            std::string kickId = "Kick##" + std::to_string(i);
            if (ImGui::Button(kickId.c_str(), ImVec2(60.0f, 22.0f))) {
                NetworkServer::Instance().KickClient((int)i);
            }
        }
    }

    EndCard();

    if (isWideLayout) {
        ImGui::SameLine();
    } else {
        ImGui::Spacing();
    }

    // RIGHT / BOTTOM: CARD 3 (PERFORMANCE - height: auto)
    BeginCard("PerfCard", card23W);
    CardHeader("[PERF]", "Performance");

    static float cpuHistory[20] = { 2, 3, 4, 3, 2, 4, 5, 3, 2, 3, 4, 3, 2, 3, 4, 5, 3, 2, 3, 4 };
    static float bitrateHistory[20] = { 0.7f, 0.8f, 0.8f, 0.9f, 0.8f, 0.8f, 0.7f, 0.8f, 0.9f, 0.8f, 0.8f, 0.8f, 0.7f, 0.8f, 0.8f, 0.9f, 0.8f, 0.8f, 0.7f, 0.8f };
    static float latencyHistory[20] = { 21, 21, 22, 21, 20, 21, 21, 22, 21, 21, 20, 21, 21, 22, 21, 21, 20, 21, 21, 21 };
    static float packetHistory[20] = { 128, 128, 130, 128, 125, 128, 128, 130, 128, 128, 125, 128, 128, 130, 128, 128, 125, 128, 128, 128 };

    float miniCardW = (ImGui::GetContentRegionAvail().x - 10.0f - CardPadding * 2.0f) * 0.5f;

    // Row 1: CPU Usage & Bitrate Mini Cards
    MetricCard("CPU Usage", "3%", cpuHistory, 20, AccentPrimary, ImVec2(miniCardW, 82.0f));
    ImGui::SameLine();
    char bitBuf[32];
    snprintf(bitBuf, sizeof(bitBuf), "%.1f Mbps", bitrate);
    MetricCard("Bitrate", bitBuf, bitrateHistory, 20, AccentSecondary, ImVec2(miniCardW, 82.0f));

    ImGui::Spacing();

    // Row 2: Latency & Packets/sec Mini Cards
    MetricCard("Latency", "~21.3 ms", latencyHistory, 20, AccentPrimary, ImVec2(miniCardW, 82.0f));
    ImGui::SameLine();
    MetricCard("Packets/Sec", "128", packetHistory, 20, ColorWarning, ImVec2(miniCardW, 82.0f));

    ImGui::Spacing();

    // Buffer Health Rounded Progress Bar
    ImGui::TextColored(TextSecondary, "Buffer Health");
    ImGui::SameLine(120.0f);
    ImGui::TextColored(AccentSecondary, "Good (92%%)");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, AccentSecondary);
    ImGui::ProgressBar(0.92f, ImVec2(ImGui::GetContentRegionAvail().x - CardPadding * 2.0f, 10.0f), "");
    ImGui::PopStyleColor();

    EndCard();

    ImGui::Spacing();

    // --- 4. BOTTOM OPTIONS (WINDOWS 11 TOGGLES) ---
    ImGui::SetCursorPosX(OuterMargin);
    if (ToggleSwitch("Minimize to Tray", &cfg.minimizeToTray)) {
        cfgChanged = true;
    }

    ImGui::SameLine(230.0f);
    if (ToggleSwitch("Start with Windows", &cfg.runOnStartup)) {
        ConfigManager::Instance().SetRunOnStartup(cfg.runOnStartup);
        cfgChanged = true;
    }

    if (cfgChanged) {
        ConfigManager::Instance().UpdateConfig(cfg);
        ConfigManager::Instance().SaveConfig();
    }

    // --- 5. DEDICATED BOTTOM DOCKED STATUS BAR (36px Height) ---
    ImGui::SetCursorPos(ImVec2(0.0f, ImGui::GetWindowHeight() - StatusBarHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, BgSecondary);

    ImGuiWindowFlags statusFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("StatusBarDocked", ImVec2(ImGui::GetWindowWidth(), StatusBarHeight), false, statusFlags);

    ImGui::SetCursorPos(ImVec2(16.0f, 7.0f));
    StatusPill(isServerActive);

    ImGui::SameLine();
    ImGui::TextColored(TextSecondary, "|  Port: 5000");

    ImGui::SameLine();
    ImGui::TextColored(AccentPrimary, "|  %.1f Mbps", bitrate);

    ImGui::SameLine();
    ImGui::TextColored(AccentSecondary, "|  Clients: %d", (int)clients.size());

    ImGui::SameLine();
    ImGui::TextColored(TextSecondary, "|  Latency: ~21.3ms");

    ImGui::SameLine();
    ImGui::TextColored(TextSecondary, "|  CPU: 3%%");

    ImGui::SameLine(ImGui::GetWindowWidth() - 140.0f);
    
    // Live Uptime Ticker
    static auto startTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - startTime).count();
    int hrs = elapsed / 3600;
    int mins = (elapsed % 3600) / 60;
    int secs = elapsed % 60;
    char upBuf[32];
    snprintf(upBuf, sizeof(upBuf), "Uptime: %02d:%02d:%02d", hrs, mins, secs);

    ImGui::TextColored(TextSecondary, "%s", upBuf);

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // Modal Event Log History Dialog
    if (m_showLogsModal) {
        ImGui::OpenPopup("Event Log History");
    }

    if (ImGui::BeginPopupModal("Event Log History", &m_showLogsModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::BeginChild("LogRegion", ImVec2(550, 320), true);
        auto logs = GetLogHistoryVector();
        for (const auto& line : logs) {
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::EndChild();

        if (ImGui::Button("Copy Logs", ImVec2(120, 0))) {
            std::wstring logsW = GetLogHistory();
            std::string logsA(logsW.begin(), logsW.end());
            ImGui::SetClipboardText(logsA.c_str());
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
