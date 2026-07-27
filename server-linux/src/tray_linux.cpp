#include "tray_linux.h"
#include "logger.h"
#include <iostream>
#include <cstring>

#if HAS_GTK
#include <gtk/gtk.h>
#if __has_include(<libayatana-appindicator/app-indicator.h>)
#include <libayatana-appindicator/app-indicator.h>
#define HAS_APPINDICATOR 1
#elif __has_include(<libappindicator/app-indicator.h>)
#include <libappindicator/app-indicator.h>
#define HAS_APPINDICATOR 1
#endif
#endif

#if HAS_GTK
static std::function<void()> g_onToggleWindow;
static std::function<void()> g_onToggleServer;
static std::function<void()> g_onToggleMute;
static std::function<void()> g_onReloadConfig;
static std::function<void()> g_onQuit;

static GtkWidget* g_trayMenu = nullptr;
static GtkWidget* g_statusMenuItem = nullptr;
static GtkWidget* g_muteMenuItem = nullptr;
static GtkWidget* g_serverMenuItem = nullptr;
#endif

TrayLinux& TrayLinux::Instance() {
    static TrayLinux instance;
    return instance;
}

#if HAS_GTK

static void SilentTrayLogHandler(const gchar* log_domain, GLogLevelFlags log_level, const gchar* message, gpointer user_data) {
    if (message) {
        if (strstr(message, "deprecated") || strstr(message, "libayatana-appindicator")) {
            return; // Suppress third-party library deprecation log messages
        }
    }
    g_log_default_handler(log_domain, log_level, message, user_data);
}

static void OnMenuShowWindow(GtkWidget* widget, gpointer data) {
    if (g_onToggleWindow) g_onToggleWindow();
}

static void OnMenuToggleServer(GtkWidget* widget, gpointer data) {
    if (g_onToggleServer) g_onToggleServer();
}

static void OnMenuToggleMute(GtkWidget* widget, gpointer data) {
    if (g_onToggleMute) g_onToggleMute();
}

static void OnMenuReloadConfig(GtkWidget* widget, gpointer data) {
    if (g_onReloadConfig) g_onReloadConfig();
}

static void OnMenuQuit(GtkWidget* widget, gpointer data) {
    if (g_onQuit) g_onQuit();
}

#if !defined(HAS_APPINDICATOR)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
static void OnStatusIconPopup(GtkStatusIcon* status_icon, guint button, guint activate_time, gpointer user_data) {
    if (g_trayMenu) {
        gtk_menu_popup_at_pointer(GTK_MENU(g_trayMenu), nullptr);
    }
}

static void OnStatusIconActivate(GtkStatusIcon* status_icon, gpointer user_data) {
    if (g_onToggleWindow) g_onToggleWindow();
}
#pragma GCC diagnostic pop
#endif

bool TrayLinux::Initialize(
    std::function<void()> onToggleWindow,
    std::function<void()> onToggleServer,
    std::function<void()> onToggleMute,
    std::function<void()> onReloadConfig,
    std::function<void()> onQuit
) {
    g_log_set_handler("libayatana-appindicator", (GLogLevelFlags)(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING), SilentTrayLogHandler, NULL);

    g_onToggleWindow = onToggleWindow;
    g_onToggleServer = onToggleServer;
    g_onToggleMute = onToggleMute;
    g_onReloadConfig = onReloadConfig;
    g_onQuit = onQuit;

    if (!gtk_init_check(NULL, NULL)) {
        LOG_WARN("[Tray] GTK Display not available. Tray icon disabled.");
        m_available.store(false);
        return false;
    }

    // Build Context Menu
    g_trayMenu = gtk_menu_new();

    g_statusMenuItem = gtk_menu_item_new_with_label("🔊 Status: Active (0 Clients)");
    gtk_widget_set_sensitive(g_statusMenuItem, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(g_trayMenu), g_statusMenuItem);

    GtkWidget* sep1 = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(g_trayMenu), sep1);

    GtkWidget* itemShow = gtk_menu_item_new_with_label("🖥️ Show / Hide Window");
    g_signal_connect(itemShow, "activate", G_CALLBACK(OnMenuShowWindow), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(g_trayMenu), itemShow);

    g_serverMenuItem = gtk_menu_item_new_with_label("⏸️ Pause Audio Server");
    g_signal_connect(g_serverMenuItem, "activate", G_CALLBACK(OnMenuToggleServer), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(g_trayMenu), g_serverMenuItem);

    g_muteMenuItem = gtk_menu_item_new_with_label("🔇 Mute Master Audio");
    g_signal_connect(g_muteMenuItem, "activate", G_CALLBACK(OnMenuToggleMute), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(g_trayMenu), g_muteMenuItem);

    GtkWidget* itemReload = gtk_menu_item_new_with_label("🔄 Reload Configuration");
    g_signal_connect(itemReload, "activate", G_CALLBACK(OnMenuReloadConfig), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(g_trayMenu), itemReload);

    GtkWidget* sep2 = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(g_trayMenu), sep2);

    GtkWidget* itemQuit = gtk_menu_item_new_with_label("❌ Quit Server");
    g_signal_connect(itemQuit, "activate", G_CALLBACK(OnMenuQuit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(g_trayMenu), itemQuit);

    gtk_widget_show_all(g_trayMenu);

#if defined(HAS_APPINDICATOR)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    AppIndicator* indicator = app_indicator_new("yanich-desksound", "audio-volume-high", APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_menu(indicator, GTK_MENU(g_trayMenu));
    m_indicator = indicator;
#pragma GCC diagnostic pop
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    GtkStatusIcon* statusIcon = gtk_status_icon_new_from_icon_name("audio-volume-high");
    gtk_status_icon_set_tooltip_text(statusIcon, "Yanich DeskSound Linux Server");
    g_signal_connect(statusIcon, "popup-menu", G_CALLBACK(OnStatusIconPopup), NULL);
    g_signal_connect(statusIcon, "activate", G_CALLBACK(OnStatusIconActivate), NULL);
    gtk_status_icon_set_visible(statusIcon, TRUE);
    m_indicator = statusIcon;
#pragma GCC diagnostic pop
#endif

    m_available.store(true);
    LOG_INFO("[Tray] System Tray Icon (Ayatana AppIndicator) initialized successfully.");
    return true;
}

struct TrayUpdateParams {
    bool serverActive;
    size_t clientCount;
    bool isMuted;
};

void TrayLinux::UpdateStatus(bool serverActive, size_t clientCount, bool isMuted) {
    if (!m_available.load() || !g_statusMenuItem) return;

    auto* params = new TrayUpdateParams{ serverActive, clientCount, isMuted };
    g_idle_add([](gpointer data) -> gboolean {
        auto* p = static_cast<TrayUpdateParams*>(data);
        if (p && TrayLinux::Instance().m_available.load()) {
            if (g_statusMenuItem) {
                std::string statusStr = (p->serverActive ? "🔊 Status: Active (" : "⏸️ Status: Stopped (") + std::to_string(p->clientCount) + " Clients)";
                gtk_menu_item_set_label(GTK_MENU_ITEM(g_statusMenuItem), statusStr.c_str());
            }

            if (g_serverMenuItem) {
                gtk_menu_item_set_label(GTK_MENU_ITEM(g_serverMenuItem), p->serverActive ? "⏸️ Pause Audio Server" : "▶️ Resume Audio Server");
            }

            if (g_muteMenuItem) {
                gtk_menu_item_set_label(GTK_MENU_ITEM(g_muteMenuItem), p->isMuted ? "🔊 Unmute Master Audio" : "🔇 Mute Master Audio");
            }

#if defined(HAS_APPINDICATOR)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            if (TrayLinux::Instance().m_indicator) {
                const char* iconName = (!p->serverActive || p->isMuted) ? "audio-volume-muted" : "audio-volume-high";
                app_indicator_set_icon_full(APP_INDICATOR(TrayLinux::Instance().m_indicator), iconName, "DeskSound");
            }
#pragma GCC diagnostic pop
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            if (TrayLinux::Instance().m_indicator) {
                const char* iconName = (!p->serverActive || p->isMuted) ? "audio-volume-muted" : "audio-volume-high";
                gtk_status_icon_set_from_icon_name(GTK_STATUS_ICON(TrayLinux::Instance().m_indicator), iconName);
            }
#pragma GCC diagnostic pop
#endif
        }
        delete p;
        return G_SOURCE_REMOVE;
    }, params);
}

void TrayLinux::Cleanup() {
    if (!m_available.load()) return;
#if !defined(HAS_APPINDICATOR)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    if (m_indicator) {
        g_object_unref(G_OBJECT(m_indicator));
        m_indicator = nullptr;
    }
#pragma GCC diagnostic pop
#endif
    m_available.store(false);
}

#else

bool TrayLinux::Initialize(
    std::function<void()> onToggleWindow,
    std::function<void()> onToggleServer,
    std::function<void()> onToggleMute,
    std::function<void()> onReloadConfig,
    std::function<void()> onQuit
) {
    LOG_INFO("[Tray] Compiled without GTK3 headers. Tray icon disabled.");
    m_available.store(false);
    return false;
}
void TrayLinux::UpdateStatus(bool active, size_t count, bool muted) {}
void TrayLinux::Cleanup() {}

#endif
