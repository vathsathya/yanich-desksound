#ifndef TRAY_LINUX_H
#define TRAY_LINUX_H

#include <string>
#include <functional>
#include <atomic>

#if __has_include(<gtk/gtk.h>)
#define HAS_GTK 1
#else
#define HAS_GTK 0
#endif

class TrayLinux {
public:
    static TrayLinux& Instance();

    bool Initialize(
        std::function<void()> onToggleWindow,
        std::function<void()> onToggleServer,
        std::function<void()> onToggleMute,
        std::function<void()> onReloadConfig,
        std::function<void()> onQuit
    );

    void UpdateStatus(bool serverActive, size_t clientCount, bool isMuted);
    void Cleanup();
    bool IsAvailable() const { return m_available; }

private:
    TrayLinux() = default;
    ~TrayLinux() = default;
    TrayLinux(const TrayLinux&) = delete;
    TrayLinux& operator=(const TrayLinux&) = delete;

    std::atomic<bool> m_available{false};
    void* m_indicator = nullptr; // AppIndicator or GtkStatusIcon pointer
};

#endif // TRAY_LINUX_H
