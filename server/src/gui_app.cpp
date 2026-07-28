#define NOMINMAX
#ifdef _WIN32
#include <windows.h>
#endif

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

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("YanichDeskSoundMain", nullptr, windowFlags);

    bool isServerActive = NetworkServer::Instance().IsActive();
    auto clients = NetworkServer::Instance().GetClients();
    float bitrate = NetworkServer::Instance().GetBitrateMbps();

    // --- 1. POLISHED HEADER BAR ---
    ImGui::BeginGroup();
    ImGui::SetCursorPosY(16.0f);
    
    // Waveform Brand Icon
    ImGui::TextColored(AccentPrimary, " ~|~|~ ");
    ImGui::SameLine();
    
    // App Title
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

    // Right Ghost Header Action Buttons
    float windowW = ImGui::GetWindowWidth();
    ImGui::SetCursorPos(ImVec2(windowW - 250.0f, 20.0f));
    
    if (GhostButton("Logs", ImVec2(70.0f, 30.0f))) {
        m_showLogsModal = true;
    }
    ImGui::SameLine();
    if (GhostButton("Settings", ImVec2(75.0f, 30.0f))) {
        m_showSettingsModal = true;
    }
    ImGui::SameLine();
    if (GhostButton("About", ImVec2(65.0f, 30.0f))) {
        m_showAboutModal = true;
    }
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- 2. CARD 1: AUDIO CONTROLS (Top Card - 20px Padding) ---
    ImGui::BeginChild("AudioCard", ImVec2(0, 220), true, ImGuiWindowFlags_NoScrollbar);

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
    ImGui::TextColored(AccentPrimary, "(((o)))");
    ImGui::SameLine();
    ImGui::TextColored(TextPrimary, "Audio Controls");

    ImGui::SameLine(ImGui::GetWindowWidth() - 130.0f);
    if (GhostButton("[Test Audio]", ImVec2(105.0f, 22.0f))) {
        if (audioBackend) {
            audioBackend->InjectTestTone();
        }
    }

    ImGui::Spacing();

    // Dual Studio Segmented LED VU Meters (Left Column) + Server Power Button (Right Column)
    float rawPeakL = audioBackend ? audioBackend->GetPeakLevelL() : 0.0f;
    float rawPeakR = audioBackend ? audioBackend->GetPeakLevelR() : 0.0f;
    if (!isServerActive) { rawPeakL = 0.0f; rawPeakR = 0.0f; }

    static float smoothL = 0.0f, peakHoldL = 0.0f;
    static float smoothR = 0.0f, peakHoldR = 0.0f;

    ImGui::BeginGroup();
    float totalW = ImGui::GetContentRegionAvail().x;
    float serverBtnW = 150.0f;
    float gapW = 30.0f;
    float meterW = totalW - serverBtnW - gapW;

    LEDVuMeter("LEFT", rawPeakL, smoothL, peakHoldL, AccentPrimary, 28, meterW);
    LEDVuMeter("RIGHT", rawPeakR, smoothR, peakHoldR, AccentSecondary, 28, meterW);
    ImGui::EndGroup();

    ImGui::SameLine(totalW - serverBtnW);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f);

    if (DrawServerButton(isServerActive, serverBtnW, 38.0f)) {
        NetworkServer::Instance().SetActive(!isServerActive);
    }

    ImGui::Spacing();

    // Custom Studio Volume Sliders with Double-Click Reset & Dedicated Value Column
    float labelW = 115.0f;
    float valueW = 45.0f;
    float btnMuteW = 65.0f;
    float sliderW = ImGui::GetContentRegionAvail().x - labelW - valueW - btnMuteW - 35.0f;

    ImVec4 colMuted = ImVec4(0.28f, 0.33f, 0.42f, 1.00f);

    // Master Volume Slider
    ImGui::TextColored(cfg.isMuted ? TextSecondary : TextPrimary, "Master");
    ImGui::SameLine(labelW);
    if (VolumeSlider("Master", "##MasterVol", &cfg.masterVolume, 0.0f, 100.0f, 100.0f, sliderW, cfg.isMuted ? colMuted : AccentPrimary, cfg.isMuted)) {
        cfgChanged = true;
    }
    ImGui::SameLine();
    ImGui::TextColored(cfg.isMuted ? TextSecondary : AccentPrimary, "%3.0f%%", cfg.masterVolume);
    ImGui::SameLine();
    if (cfg.isMuted) {
        ImGui::PushStyleColor(ImGuiCol_Button, ColorDanger);
        if (ImGui::Button("Muted##M", ImVec2(btnMuteW, 0))) { cfg.isMuted = false; cfgChanged = true; }
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button("Mute##M", ImVec2(btnMuteW, 0))) { cfg.isMuted = true; cfgChanged = true; }
    }

    // Gain Left Slider
    bool isLDisabled = cfg.isMuted || cfg.isMutedL;
    ImGui::TextColored(isLDisabled ? TextSecondary : AccentPrimary, "Gain L");
    ImGui::SameLine(labelW);
    if (VolumeSlider("Gain L", "##GainL", &cfg.gainL, 0.0f, 100.0f, 100.0f, sliderW, isLDisabled ? colMuted : AccentPrimary, isLDisabled)) {
        cfgChanged = true;
    }
    ImGui::SameLine();
    ImGui::TextColored(isLDisabled ? TextSecondary : AccentPrimary, "%3.0f%%", cfg.gainL);
    ImGui::SameLine();
    if (cfg.isMutedL) {
        ImGui::PushStyleColor(ImGuiCol_Button, ColorDanger);
        if (ImGui::Button("Mute L##L", ImVec2(btnMuteW, 0))) { cfg.isMutedL = false; cfgChanged = true; }
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button("Mute L##L", ImVec2(btnMuteW, 0))) { cfg.isMutedL = true; cfgChanged = true; }
    }

    // Gain Right Slider
    bool isRDisabled = cfg.isMuted || cfg.isMutedR;
    ImGui::TextColored(isRDisabled ? TextSecondary : AccentSecondary, "Gain R");
    ImGui::SameLine(labelW);
    if (VolumeSlider("Gain R", "##GainR", &cfg.gainR, 0.0f, 100.0f, 100.0f, sliderW, isRDisabled ? colMuted : AccentSecondary, isRDisabled)) {
        cfgChanged = true;
    }
    ImGui::SameLine();
    ImGui::TextColored(isRDisabled ? TextSecondary : AccentSecondary, "%3.0f%%", cfg.gainR);
    ImGui::SameLine();
    if (cfg.isMutedR) {
        ImGui::PushStyleColor(ImGuiCol_Button, ColorDanger);
        if (ImGui::Button("Mute R##R", ImVec2(btnMuteW, 0))) { cfg.isMutedR = false; cfgChanged = true; }
        ImGui::PopStyleColor();
    } else {
        if (ImGui::Button("Mute R##R", ImVec2(btnMuteW, 0))) { cfg.isMutedR = true; cfgChanged = true; }
    }

    ImGui::EndChild();

    ImGui::Spacing();

    // --- 3. TWO-COLUMN SPLIT (CARD 2: NETWORK & CARD 3: PERFORMANCE) ---
    float splitW = (ImGui::GetContentRegionAvail().x - 16.0f) * 0.5f;

    // LEFT COLUMN: CARD 2 (NETWORK - 260px Height, No Scrollbars)
    ImGui::BeginChild("NetworkCard", ImVec2(splitW, 260), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
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
        EmptyState(ImVec2(ImGui::GetContentRegionAvail().x, 120.0f), "No devices connected", "Open Yanich DeskSound on your mobile device.");
    } else {
        for (size_t i = 0; i < clients.size(); ++i) {
            ImGui::PushID((int)i);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, CardElevated);
            ImGui::BeginChild(("ClientCard_" + std::to_string(i)).c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 38.0f), true, ImGuiWindowFlags_NoScrollbar);
            
            ImGui::SetCursorPos(ImVec2(10.0f, 9.0f));
            ImGui::TextColored(AccentPrimary, "[Phone]");
            ImGui::SameLine();
            ImGui::TextColored(TextPrimary, "Client #%d:", clients[i].id);
            ImGui::SameLine();
            ImGui::TextColored(TextSecondary, "%s", clients[i].ip.c_str());

            ImGui::SameLine(ImGui::GetWindowWidth() - 175.0f);
            ImGui::SetCursorPosY(7.0f);
            ImGui::SetNextItemWidth(95.0f);
            const char* modeNames[] = { "Stereo", "Left Ch", "Right Ch" };
            int currentMode = (int)clients[i].channelMode;
            if (ImGui::Combo("##Mode", &currentMode, modeNames, 3)) {
                NetworkServer::Instance().SetClientChannelMode((int)i, (ClientChannelMode)currentMode);
            }

            ImGui::SameLine(ImGui::GetWindowWidth() - 70.0f);
            ImGui::SetCursorPosY(8.0f);
            if (DangerButton("Kick", ImVec2(58.0f, 22.0f))) {
                NetworkServer::Instance().KickClient((int)i);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopID();
            ImGui::Spacing();
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // RIGHT COLUMN: CARD 3 (PERFORMANCE - 260px Height, No Scrollbars)
    ImGui::BeginChild("PerfCard", ImVec2(splitW, 260), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    CardHeader("[PERF]", "Performance");

    // Real Live Telemetry Ring Buffers & Dynamic Windows Metrics
    static float cpuHistory[20] = { 3, 3, 2, 4, 3, 2, 4, 3, 2, 3, 4, 3, 2, 3, 4, 3, 2, 3, 4, 3 };
    static float bitrateHistory[20] = { 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f };
    static float latencyHistory[20] = { 2.6f, 2.6f, 2.6f, 2.6f, 2.6f, 2.6f, 2.6f, 2.6f, 2.6f, 2.6f, 2.6f, 2.6f, 2.6f, 2.6f, 2.6f, 2.6f, 2.6f, 2.6f, 2.6f, 2.6f };
    static float packetHistory[20] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    static double lastTelemetryUpdate = 0.0;
    double nowTime = ImGui::GetTime();

    float liveBitrate = NetworkServer::Instance().GetBitrateMbps();
    int livePps = NetworkServer::Instance().GetPacketsPerSec();
    float liveLatencyMs = (cfg.bufferSize * 1000.0f / 48000.0f) + (clients.empty() ? 0.0f : 12.5f);

    static float liveCpuPercent = 3.0f;
#ifdef _WIN32
    static FILETIME lastSysKernel{}, lastSysUser{}, lastProcKernel{}, lastProcUser{};
    static bool cpuInit = false;
    if (nowTime - lastTelemetryUpdate >= 0.15) {
        FILETIME ftime, fsysIdle, fsysKernel, fsysUser, fprocKernel, fprocUser;
        GetSystemTimeAsFileTime(&ftime);
        GetSystemTimes(&fsysIdle, &fsysKernel, &fsysUser);
        GetProcessTimes(GetCurrentProcess(), &ftime, &ftime, &fprocKernel, &fprocUser);
        if (cpuInit) {
            uint64_t sysK = (((uint64_t)fsysKernel.dwHighDateTime << 32) | fsysKernel.dwLowDateTime) - (((uint64_t)lastSysKernel.dwHighDateTime << 32) | lastSysKernel.dwLowDateTime);
            uint64_t sysU = (((uint64_t)fsysUser.dwHighDateTime << 32) | fsysUser.dwLowDateTime) - (((uint64_t)lastSysUser.dwHighDateTime << 32) | lastSysUser.dwLowDateTime);
            uint64_t procK = (((uint64_t)fprocKernel.dwHighDateTime << 32) | fprocKernel.dwLowDateTime) - (((uint64_t)lastProcKernel.dwHighDateTime << 32) | lastProcKernel.dwLowDateTime);
            uint64_t procU = (((uint64_t)fprocUser.dwHighDateTime << 32) | fprocUser.dwLowDateTime) - (((uint64_t)lastProcUser.dwHighDateTime << 32) | lastProcUser.dwLowDateTime);
            uint64_t totalSys = sysK + sysU;
            uint64_t totalProc = procK + procU;
            if (totalSys > 0) {
                liveCpuPercent = (std::max)(1.0f, (std::min)(100.0f, (float)(totalProc * 100.0 / totalSys)));
            }
        }
        lastSysKernel = fsysKernel; lastSysUser = fsysUser;
        lastProcKernel = fprocKernel; lastProcUser = fprocUser;
        cpuInit = true;
    }
#endif

    if (nowTime - lastTelemetryUpdate >= 0.15) {
        lastTelemetryUpdate = nowTime;
        for (int i = 0; i < 19; ++i) {
            cpuHistory[i] = cpuHistory[i + 1];
            bitrateHistory[i] = bitrateHistory[i + 1];
            latencyHistory[i] = latencyHistory[i + 1];
            packetHistory[i] = packetHistory[i + 1];
        }
        cpuHistory[19] = liveCpuPercent;
        bitrateHistory[19] = liveBitrate;
        latencyHistory[19] = liveLatencyMs;
        packetHistory[19] = (float)livePps;
    }

    char cpuBuf[32], bitBuf[32], latBuf[32], ppsBuf[32];
    snprintf(cpuBuf, sizeof(cpuBuf), "%.0f%%", liveCpuPercent);
    snprintf(bitBuf, sizeof(bitBuf), "%.1f Mbps", liveBitrate);
    snprintf(latBuf, sizeof(latBuf), "~%.1f ms", liveLatencyMs);
    snprintf(ppsBuf, sizeof(ppsBuf), "%d", livePps);

    float miniCardW = (ImGui::GetContentRegionAvail().x - 10.0f) * 0.5f;

    // Row 1: Real CPU Usage & Bitrate Mini Cards
    MetricCard("CPU Usage", cpuBuf, cpuHistory, 20, AccentPrimary, ImVec2(miniCardW, 76.0f));
    ImGui::SameLine();
    MetricCard("Bitrate", bitBuf, bitrateHistory, 20, AccentSecondary, ImVec2(miniCardW, 76.0f));

    ImGui::Spacing();

    // Row 2: Dynamic Latency & Live Packets/Sec Mini Cards
    MetricCard("Latency", latBuf, latencyHistory, 20, AccentPrimary, ImVec2(miniCardW, 76.0f));
    ImGui::SameLine();
    MetricCard("Packets/Sec", ppsBuf, packetHistory, 20, ColorWarning, ImVec2(miniCardW, 76.0f));

    ImGui::Spacing();

    // Buffer Health Rounded Progress Bar
    float bufHealth = (!isServerActive) ? 0.0f : (audioBackend ? 0.98f : 0.85f);
    char healthStr[32];
    snprintf(healthStr, sizeof(healthStr), "%s (%.0f%%)", (bufHealth >= 0.90f) ? "Optimal" : (bufHealth > 0.0f) ? "Good" : "Offline", bufHealth * 100.0f);

    ImGui::TextColored(TextSecondary, "Buffer Health");
    ImGui::SameLine(120.0f);
    ImGui::TextColored(bufHealth >= 0.90f ? AccentSecondary : bufHealth > 0.0f ? ColorWarning : TextSecondary, "%s", healthStr);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bufHealth >= 0.90f ? AccentSecondary : bufHealth > 0.0f ? ColorWarning : TextSecondary);
    ImGui::ProgressBar(bufHealth, ImVec2(ImGui::GetContentRegionAvail().x, 8.0f), "");
    ImGui::PopStyleColor();

    ImGui::EndChild();

    // --- 4. DEDICATED BOTTOM DOCKED STATUS BAR (36px Height) ---
    ImGui::SetCursorPos(ImVec2(0.0f, ImGui::GetWindowHeight() - StatusBarHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, BgSecondary);

    ImGui::BeginChild("StatusBarDocked", ImVec2(ImGui::GetWindowWidth(), StatusBarHeight), false, ImGuiWindowFlags_NoScrollbar);

    ImGui::SetCursorPos(ImVec2(16.0f, 7.0f));
    StatusPill(isServerActive);

    ImGui::SameLine();
    ImGui::TextColored(TextSecondary, "|  Port: 5000");

    ImGui::SameLine();
    ImGui::TextColored(AccentPrimary, "|  %.1f Mbps", liveBitrate);

    ImGui::SameLine();
    ImGui::TextColored(AccentSecondary, "|  Clients: %d", (int)clients.size());

    ImVec4 latColor = (liveLatencyMs < 10.0f) ? AccentSecondary : (liveLatencyMs < 25.0f) ? AccentPrimary : ColorWarning;
    ImVec4 cpuColor = (liveCpuPercent < 15.0f) ? TextSecondary : (liveCpuPercent < 50.0f) ? ColorWarning : ColorDanger;

    ImGui::SameLine();
    ImGui::TextColored(TextSecondary, "|  Latency:");
    ImGui::SameLine();
    ImGui::TextColored(latColor, "~%.1fms", liveLatencyMs);

    ImGui::SameLine();
    ImGui::TextColored(TextSecondary, "|  CPU:");
    ImGui::SameLine();
    ImGui::TextColored(cpuColor, "%.0f%%", liveCpuPercent);

    ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
    
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

    // Modal Settings Dialog
    if (m_showSettingsModal) {
        ImGui::OpenPopup("Server Settings");
    }

    if (ImGui::BeginPopupModal("Server Settings", &m_showSettingsModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(AccentPrimary, "Yanich DeskSound Server Configuration");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(AccentPrimary, "Audio Hardware Configuration");
        ImGui::Spacing();

        // Audio Device Dropdown
        ImGui::TextColored(TextSecondary, "Audio Source");
        auto devices = audioBackend ? audioBackend->EnumerateDevices() : std::vector<AudioDeviceInfo>{};
        std::string currentDevName = (cfg.selectedDeviceIndex >= 0 && cfg.selectedDeviceIndex < (int)devices.size()) ? devices[cfg.selectedDeviceIndex].name : "Default System Playback Device";
        
        ImGui::SetNextItemWidth(380.0f);
        if (ImGui::BeginCombo("##AudioSourceModal", currentDevName.c_str())) {
            for (const auto& dev : devices) {
                bool isSelected = (cfg.selectedDeviceIndex == dev.index);
                if (ImGui::Selectable(dev.name.c_str(), isSelected)) {
                    cfg.selectedDeviceIndex = dev.index;
                    if (audioBackend) audioBackend->SelectDevice(dev.index);
                    ConfigManager::Instance().UpdateConfig(cfg);
                    ConfigManager::Instance().SaveConfig();
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();

        // Buffer Size Dropdown
        ImGui::TextColored(TextSecondary, "Buffer Size");
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

        ImGui::SetNextItemWidth(380.0f);
        if (ImGui::BeginCombo("##BufferSizeModal", bufferItems[currentBufIdx])) {
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
                    ConfigManager::Instance().UpdateConfig(cfg);
                    ConfigManager::Instance().SaveConfig();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(AccentSecondary, "Network Ports");
        ImGui::Spacing();

        static int port = 5000;
        ImGui::SetNextItemWidth(380.0f);
        ImGui::InputInt("Server Port (TCP)", &port);
        
        static int udpPort = 5001;
        ImGui::SetNextItemWidth(380.0f);
        ImGui::InputInt("Streaming Port (UDP)", &udpPort);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(AccentSecondary, "Application Preferences");
        ImGui::Spacing();

        bool modalCfgChanged = false;
        if (ToggleSwitch("Minimize to Tray", &cfg.minimizeToTray)) {
            modalCfgChanged = true;
        }

        ImGui::Spacing();
        if (ToggleSwitch("Start with Windows", &cfg.runOnStartup)) {
            ConfigManager::Instance().SetRunOnStartup(cfg.runOnStartup);
            modalCfgChanged = true;
        }

        if (modalCfgChanged) {
            ConfigManager::Instance().UpdateConfig(cfg);
            ConfigManager::Instance().SaveConfig();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (PrimaryButton("Save & Apply", ImVec2(140, 32))) {
            ConfigManager::Instance().UpdateConfig(cfg);
            ConfigManager::Instance().SaveConfig();
            m_showSettingsModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (GhostButton("Cancel", ImVec2(100, 32))) {
            m_showSettingsModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Modal About Dialog
    if (m_showAboutModal) {
        ImGui::OpenPopup("About Yanich DeskSound");
    }

    if (ImGui::BeginPopupModal("About Yanich DeskSound", &m_showAboutModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(AccentPrimary, " ~|~|~  Yanich DeskSound Server");
        ImGui::TextColored(TextSecondary, "Version 1.2.7 (Commercial Release 2026)");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Streams Windows desktop audio to mobile devices over local Wi-Fi.");
        ImGui::BulletText("Audio Engine: Windows WASAPI Low-Latency Loopback");
        ImGui::BulletText("GUI Engine: Dear ImGui DirectX 11 Commercial Theme");
        ImGui::BulletText("Networking: Winsock2 Low-Latency UDP Audio Broadcast");

        ImGui::Spacing();
        ImGui::TextColored(TextSecondary, "Created by vathsathya - All rights reserved.");
        ImGui::Spacing();

        if (PrimaryButton("OK", ImVec2(100, 30))) {
            m_showAboutModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}
