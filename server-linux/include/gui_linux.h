#ifndef GUI_LINUX_H
#define GUI_LINUX_H

#include <string>
#include <vector>
#include <functional>
#include <atomic>

#if __has_include(<gtk/gtk.h>)
#define HAS_GTK 1
#include <gtk/gtk.h>
#else
#define HAS_GTK 0
typedef void GtkWidget;
#endif

struct ClientInfo {
    int id;
    std::string ip;
    std::string channelMode; // STEREO, LEFT, RIGHT
};

class GuiLinux {
public:
    static GuiLinux& Instance();

    bool Initialize(int argc, char** argv);
    void RunLoop();
    void Quit();

    void UpdateStatus(bool serverActive, const std::vector<std::string>& localIps, const std::vector<ClientInfo>& clients);
    void UpdateAudioSettings(float masterVol, float gainL, float gainR, bool muted, bool mutedL, bool mutedR);
    void UpdateAudioPeak(float level);
    void UpdateDiagnostics(float bitrateMbps, float bufLatencyMs, float totalEstLatencyMs);
    void AddLogMessage(const std::string& msg);
    void ShowLogDialog();

    void SetOnServerToggle(std::function<void(bool active)> callback) { m_onServerToggle = callback; }
    void SetOnVolumeChanged(std::function<void(float master, float gainL, float gainR, bool muted, bool mutedL, bool mutedR)> callback) { m_onVolumeChanged = callback; }
    void SetOnDeviceChanged(std::function<void(int index)> callback) { m_onDeviceChanged = callback; }
    void SetOnBufferSizeChanged(std::function<void(size_t bufferSize)> callback) { m_onBufferSizeChanged = callback; }
    void SetOnKickClient(std::function<void(int clientIndex)> callback) { m_onKickClient = callback; }
    void SetOnClientModeChanged(std::function<void(int clientIndex, int modeTag)> callback) { m_onClientModeChanged = callback; }
    void SetOnAutostartToggled(std::function<void(bool enable)> callback) { m_onAutostartToggled = callback; }
    void SetOnQuit(std::function<void()> callback) { m_onQuit = callback; }

    void ToggleVisibility();
    bool IsVisible() const;
    bool HasGTK() const { return HAS_GTK != 0; }

private:
    GuiLinux() = default;
    ~GuiLinux() = default;
    GuiLinux(const GuiLinux&) = delete;
    GuiLinux& operator=(const GuiLinux&) = delete;

    void BuildUI();

    GtkWidget* m_mainWindow = nullptr;
    GtkWidget* m_headerBar = nullptr;
    GtkWidget* m_statusBar = nullptr;
    GtkWidget* m_lblStatusMode = nullptr;
    GtkWidget* m_btnServerToggle = nullptr;
    GtkWidget* m_btnViewLogs = nullptr;

    GtkWidget* m_levelMeter = nullptr;

    GtkWidget* m_scaleMaster = nullptr;
    GtkWidget* m_scaleGainL = nullptr;
    GtkWidget* m_scaleGainR = nullptr;

    GtkWidget* m_lblMasterVal = nullptr;
    GtkWidget* m_lblGainLVal = nullptr;
    GtkWidget* m_lblGainRVal = nullptr;

    GtkWidget* m_btnMuteMaster = nullptr;
    GtkWidget* m_btnMuteL = nullptr;
    GtkWidget* m_btnMuteR = nullptr;

    GtkWidget* m_comboDevices = nullptr;
    GtkWidget* m_comboBuffer = nullptr;
    GtkWidget* m_lblDiagnostics = nullptr;

    GtkWidget* m_boxIpList = nullptr;
    GtkWidget* m_boxClientList = nullptr;

    GtkWidget* m_chkMinimizeToTray = nullptr;
    GtkWidget* m_chkAutostart = nullptr;

    std::atomic<bool> m_initialized{false};

    // UI Change Tracking
    bool m_lastServerActive = false;
    bool m_hasInitialStatus = false;
    std::vector<std::string> m_lastLocalIps;
    size_t m_lastClientCount = 0;
    std::vector<std::string> m_logBuffer;

    std::function<void(bool active)> m_onServerToggle;
    std::function<void(float master, float gainL, float gainR, bool muted, bool mutedL, bool mutedR)> m_onVolumeChanged;
    std::function<void(int index)> m_onDeviceChanged;
    std::function<void(size_t bufferSize)> m_onBufferSizeChanged;
    std::function<void(int clientIndex)> m_onKickClient;
    std::function<void(int clientIndex, int modeTag)> m_onClientModeChanged;
    std::function<void(bool enable)> m_onAutostartToggled;
    std::function<void()> m_onQuit;
};

#endif // GUI_LINUX_H
