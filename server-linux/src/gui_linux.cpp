#include "gui_linux.h"
#include "audio_pulse.h"
#include "logger.h"
#include "config_manager.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>

extern PulseAudioRecorder g_audioRecorder;

GuiLinux& GuiLinux::Instance() {
    static GuiLinux instance;
    return instance;
}

#if HAS_GTK

static void SilentGtkLogHandler(const gchar* log_domain, GLogLevelFlags log_level, const gchar* message, gpointer user_data) {
    if (message) {
        if (strstr(message, "deprecated") || strstr(message, "gtk_widget_get_scale_factor") || strstr(message, "libayatana-appindicator")) {
            return; // Suppress third-party library deprecation log messages
        }
    }
    g_log_default_handler(log_domain, log_level, message, user_data);
}

static gboolean OnDeleteEvent(GtkWidget* widget, GdkEvent* event, gpointer data) {
    ServerConfig cfg = ConfigManager::Instance().GetConfig();
    if (cfg.minimizeToTray) {
        gtk_widget_hide(widget);
        return TRUE; // Do not destroy window
    }
    return FALSE; // Destroy window and exit
}

static void OnDestroy(GtkWidget* widget, gpointer data) {
    GuiLinux::Instance().Quit();
}

bool GuiLinux::Initialize(int argc, char** argv) {
    g_log_set_handler("Gtk", (GLogLevelFlags)(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING), SilentGtkLogHandler, NULL);
    g_log_set_handler("libayatana-appindicator", (GLogLevelFlags)(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING), SilentGtkLogHandler, NULL);

    if (!gtk_init_check(&argc, &argv)) {
        LOG_WARN("[GUI] Could not initialize GTK Display. Running in Headless/Console mode.");
        m_initialized.store(false);
        return false;
    }

    BuildUI();
    m_initialized.store(true);
    LOG_INFO("[GUI] GTK3 Desktop GUI Interface initialized (100% Native GNOME HIG Architecture).");
    return true;
}

void GuiLinux::AddLogMessage(const std::string& msg) {
    if (m_logBuffer.size() > 500) m_logBuffer.erase(m_logBuffer.begin());
    m_logBuffer.push_back(msg);
}

void GuiLinux::ShowLogDialog() {
    if (!m_initialized.load() || !m_mainWindow) return;

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Event Log History",
        GTK_WINDOW(m_mainWindow),
        GTK_DIALOG_MODAL,
        "Close", GTK_RESPONSE_CLOSE,
        NULL
    );
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 360);

    GtkWidget* contentArea = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* scrolledWin = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(scrolledWin), 8);

    GtkWidget* textView = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textView), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(textView), TRUE);

    GtkTextBuffer* textBuf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textView));
    std::string fullLog;
    for (const auto& l : m_logBuffer) {
        fullLog += l + "\n";
    }
    gtk_text_buffer_set_text(textBuf, fullLog.c_str(), -1);

    gtk_container_add(GTK_CONTAINER(scrolledWin), textView);
    gtk_box_pack_start(GTK_BOX(contentArea), scrolledWin, TRUE, TRUE, 0);

    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void GuiLinux::BuildUI() {
    m_mainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(m_mainWindow), 420, 580);
    gtk_window_set_resizable(GTK_WINDOW(m_mainWindow), FALSE);
    gtk_window_set_position(GTK_WINDOW(m_mainWindow), GTK_WIN_POS_CENTER);

    g_signal_connect(m_mainWindow, "delete-event", G_CALLBACK(OnDeleteEvent), NULL);
    g_signal_connect(m_mainWindow, "destroy", G_CALLBACK(OnDestroy), NULL);

    // --- 1. Perfectly Aligned Clean GTK HeaderBar with View Logs Button ---
    m_headerBar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(m_headerBar), TRUE);

    GtkWidget* headerTitleBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(headerTitleBox, 12);
    gtk_widget_set_margin_top(headerTitleBox, 6);
    gtk_widget_set_margin_bottom(headerTitleBox, 6);
    gtk_widget_set_valign(headerTitleBox, GTK_ALIGN_CENTER);

    GtkWidget* lblIcon = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lblIcon), "<span size='15000'>🔊</span>");
    gtk_widget_set_valign(lblIcon, GTK_ALIGN_CENTER);

    GtkWidget* textVBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(textVBox, GTK_ALIGN_CENTER);

    GtkWidget* lblHeaderTitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lblHeaderTitle), "<span weight='bold' size='11500' foreground='#0f172a'>Yanich DeskSound</span>");
    gtk_widget_set_halign(lblHeaderTitle, GTK_ALIGN_START);

    GtkWidget* lblHeaderSub = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lblHeaderSub), "<span size='8500' foreground='#64748b'>Linux Server v1.2.0</span>");
    gtk_widget_set_halign(lblHeaderSub, GTK_ALIGN_START);

    gtk_box_pack_start(GTK_BOX(textVBox), lblHeaderTitle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(textVBox), lblHeaderSub, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(headerTitleBox), lblIcon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(headerTitleBox), textVBox, FALSE, FALSE, 0);

    gtk_header_bar_pack_start(GTK_HEADER_BAR(m_headerBar), headerTitleBox);

    // View Logs button in HeaderBar
    m_btnViewLogs = gtk_button_new_with_label("Logs 📜");
    g_signal_connect(m_btnViewLogs, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer u) {
        GuiLinux::Instance().ShowLogDialog();
    }), NULL);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(m_headerBar), m_btnViewLogs);

    gtk_window_set_titlebar(GTK_WINDOW(m_mainWindow), m_headerBar);

    // Sharp Rectangular Clean CSS Design (Pixel-Perfect Clean GTK3 CSS)
    GtkCssProvider* provider = gtk_css_provider_new();
    const char* css = R"(
        list.inset-group {
            background-color: @theme_bg_color;
            border-radius: 0px;
            border: 1px solid alpha(@theme_fg_color, 0.12);
        }
        list.inset-group row {
            padding: 8px 12px;
            border-bottom: 1px solid alpha(@theme_fg_color, 0.06);
        }
        list.inset-group row:last-child {
            border-bottom: none;
        }
        .section-header {
            margin-top: 8px;
            margin-bottom: 4px;
            margin-left: 2px;
            font-size: 10px;
            font-weight: bold;
            color: #64748b;
        }
        .val-label {
            font-family: monospace;
            font-weight: bold;
            min-width: 38px;
        }
        scale trough {
            min-height: 6px;
            border-radius: 0px;
            border: none;
            box-shadow: none;
            outline: none;
            background-color: alpha(@theme_fg_color, 0.12);
        }
        scale highlight, scale contents trough highlight {
            background-color: #2563eb;
            border-radius: 0px;
            min-height: 6px;
            border: none;
            box-shadow: none;
            outline: none;
        }
        scale slider {
            border: none;
            border-radius: 0px;
            box-shadow: 0 1px 3px rgba(0, 0, 0, 0.2);
            outline: none;
        }
        levelbar trough {
            border: none;
            box-shadow: none;
            outline: none;
            padding: 0;
            background-color: alpha(@theme_fg_color, 0.08);
            border-radius: 0px;
            min-height: 26px;
        }
        levelbar block.filled {
            background-color: #10b981;
            border-radius: 0px;
            border: none;
            box-shadow: none;
            min-height: 26px;
        }
        levelbar block.empty {
            background-color: transparent;
            border: none;
            box-shadow: none;
            min-height: 26px;
        }
        button {
            border: none;
            box-shadow: none;
            outline: none;
            background-image: none;
            border-radius: 0px;
            padding: 4px 10px;
            background-color: alpha(@theme_fg_color, 0.06);
            transition: all 0.15s ease-in-out;
        }
        button:hover {
            background-color: alpha(@theme_fg_color, 0.12);
            border: none;
            box-shadow: none;
        }
        button:active {
            background-color: alpha(@theme_fg_color, 0.18);
            border: none;
            box-shadow: none;
        }
        button.btn-toggle-server {
            min-width: 26px;
            min-height: 26px;
            padding: 0px;
            margin: 0px;
            border: none;
            box-shadow: none;
            outline: none;
            border-radius: 0px;
            font-size: 15px;
            font-weight: bold;
        }
        button.destructive-action {
            background-color: #ef4444;
            color: #ffffff;
            border: none;
            box-shadow: none;
        }
        button.destructive-action:hover {
            background-color: #dc2626;
            border: none;
            box-shadow: none;
        }
        button.suggested-action {
            background-color: #2563eb;
            color: #ffffff;
            border: none;
            box-shadow: none;
        }
        button.suggested-action:hover {
            background-color: #1d4ed8;
            border: none;
            box-shadow: none;
        }
        button.btn-muted-active {
            background-color: #fef2f2;
            color: #dc2626;
            border: none;
            box-shadow: none;
            border-radius: 0px;
            font-weight: bold;
        }
        button.btn-kick {
            background-color: #fef2f2;
            color: #dc2626;
            border: none;
            box-shadow: none;
            padding: 3px 8px;
            font-size: 11px;
            border-radius: 0px;
        }
        button.btn-kick:hover {
            background-color: #fee2e2;
            border: none;
            box-shadow: none;
        }
        combobox button {
            border: none;
            box-shadow: none;
            outline: none;
            background-color: alpha(@theme_fg_color, 0.05);
            border-radius: 0px;
            padding: 4px 8px;
        }
        combobox button:hover {
            background-color: alpha(@theme_fg_color, 0.10);
            border: none;
            box-shadow: none;
        }
        .status-bar-container {
            border-top: 1px solid alpha(@theme_fg_color, 0.12);
            padding: 6px 12px;
            background-color: alpha(@theme_fg_color, 0.03);
        }
        .status-mode-lbl {
            font-size: 11px;
            font-weight: bold;
            color: #10b981;
        }
        .diag-badge {
            font-size: 11px;
            font-family: monospace;
            color: #2563eb;
            background-color: #eff6ff;
            padding: 2px 6px;
            border-radius: 0px;
        }
    )";

    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    GtkWidget* rootVBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(m_mainWindow), rootVBox);

    GtkWidget* mainVBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(mainVBox), 12);
    gtk_box_pack_start(GTK_BOX(rootVBox), mainVBox, TRUE, TRUE, 0);

    // --- 2. GROUP 1: Audio Controls (Native GtkListBox Group) ---
    GtkWidget* lblAudioGroupHeader = gtk_label_new("AUDIO CONTROLS");
    gtk_widget_set_halign(lblAudioGroupHeader, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblAudioGroupHeader), "section-header");
    gtk_box_pack_start(GTK_BOX(mainVBox), lblAudioGroupHeader, FALSE, FALSE, 0);

    GtkWidget* listAudio = gtk_list_box_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(listAudio), "inset-group");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(listAudio), GTK_SELECTION_NONE);

    // Row 0: Live Audio Peak Meter & Perfect Square Icon Toggle Button (⏹ / ▶)
    GtkWidget* rowPeak = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_valign(rowPeak, GTK_ALIGN_CENTER);

    GtkWidget* lblPeak = gtk_label_new("Audio Level:");
    gtk_widget_set_halign(lblPeak, GTK_ALIGN_START);
    gtk_widget_set_valign(lblPeak, GTK_ALIGN_CENTER);

    m_levelMeter = gtk_level_bar_new_for_interval(0.0, 1.0);
    gtk_level_bar_set_value(GTK_LEVEL_BAR(m_levelMeter), 0.0);
    gtk_widget_set_valign(m_levelMeter, GTK_ALIGN_CENTER);

    m_btnServerToggle = gtk_button_new_with_label("⏹");
    gtk_widget_set_size_request(m_btnServerToggle, 26, 26);
    gtk_widget_set_tooltip_text(m_btnServerToggle, "Stop Audio Server");
    gtk_style_context_add_class(gtk_widget_get_style_context(m_btnServerToggle), "btn-toggle-server");
    gtk_style_context_add_class(gtk_widget_get_style_context(m_btnServerToggle), "destructive-action");
    gtk_widget_set_valign(m_btnServerToggle, GTK_ALIGN_CENTER);

    g_signal_connect(m_btnServerToggle, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer user_data) {
        const char* label = gtk_button_get_label(btn);
        bool isRunning = (label && std::string(label) == "⏹");
        if (GuiLinux::Instance().m_onServerToggle) {
            GuiLinux::Instance().m_onServerToggle(!isRunning);
        }
    }), NULL);

    gtk_box_pack_start(GTK_BOX(rowPeak), lblPeak, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rowPeak), m_levelMeter, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(rowPeak), m_btnServerToggle, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(listAudio), rowPeak);

    // Row 1: Master Volume
    GtkWidget* rowMaster = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* lblMaster = gtk_label_new("Master:");
    gtk_widget_set_halign(lblMaster, GTK_ALIGN_START);
    m_scaleMaster = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(m_scaleMaster), FALSE);
    gtk_range_set_value(GTK_RANGE(m_scaleMaster), 100);

    m_lblMasterVal = gtk_label_new("100%");
    gtk_style_context_add_class(gtk_widget_get_style_context(m_lblMasterVal), "val-label");

    m_btnMuteMaster = gtk_button_new_with_label("Mute");
    gtk_box_pack_start(GTK_BOX(rowMaster), lblMaster, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rowMaster), m_scaleMaster, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(rowMaster), m_lblMasterVal, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rowMaster), m_btnMuteMaster, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(listAudio), rowMaster);

    // Row 2: Gain Left
    GtkWidget* rowGainL = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* lblGainL = gtk_label_new("Gain L:");
    gtk_widget_set_halign(lblGainL, GTK_ALIGN_START);
    m_scaleGainL = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(m_scaleGainL), FALSE);
    gtk_range_set_value(GTK_RANGE(m_scaleGainL), 100);

    m_lblGainLVal = gtk_label_new("100%");
    gtk_style_context_add_class(gtk_widget_get_style_context(m_lblGainLVal), "val-label");

    m_btnMuteL = gtk_button_new_with_label("Mute L");
    gtk_box_pack_start(GTK_BOX(rowGainL), lblGainL, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rowGainL), m_scaleGainL, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(rowGainL), m_lblGainLVal, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rowGainL), m_btnMuteL, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(listAudio), rowGainL);

    // Row 3: Gain Right
    GtkWidget* rowGainR = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* lblGainR = gtk_label_new("Gain R:");
    gtk_widget_set_halign(lblGainR, GTK_ALIGN_START);
    m_scaleGainR = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(m_scaleGainR), FALSE);
    gtk_range_set_value(GTK_RANGE(m_scaleGainR), 100);

    m_lblGainRVal = gtk_label_new("100%");
    gtk_style_context_add_class(gtk_widget_get_style_context(m_lblGainRVal), "val-label");

    m_btnMuteR = gtk_button_new_with_label("Mute R");
    gtk_box_pack_start(GTK_BOX(rowGainR), lblGainR, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rowGainR), m_scaleGainR, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(rowGainR), m_lblGainRVal, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rowGainR), m_btnMuteR, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(listAudio), rowGainR);

    // Row 4: Audio Source Device Dropdown
    GtkWidget* rowDev = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* lblDev = gtk_label_new("Audio Source:");
    gtk_widget_set_halign(lblDev, GTK_ALIGN_START);
    m_comboDevices = gtk_combo_box_text_new();

    auto devList = g_audioRecorder.EnumerateDevices();
    for (const auto& dev : devList) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m_comboDevices), dev.description.c_str());
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(m_comboDevices), 0);

    g_signal_connect(m_comboDevices, "changed", G_CALLBACK(+[](GtkComboBox* combo, gpointer user_data) {
        int idx = gtk_combo_box_get_active(combo);
        if (GuiLinux::Instance().m_onDeviceChanged) {
            GuiLinux::Instance().m_onDeviceChanged(idx);
        }
    }), NULL);

    gtk_box_pack_start(GTK_BOX(rowDev), lblDev, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rowDev), m_comboDevices, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(listAudio), rowDev);

    // Row 5: Buffer Size Selector (Ultra-Low Latency & Smooth Buffer Tuning)
    GtkWidget* rowBuffer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* lblBuf = gtk_label_new("Buffer Size:");
    gtk_widget_set_halign(lblBuf, GTK_ALIGN_START);
    m_comboBuffer = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m_comboBuffer), "128 samples (~2.6ms, Instant)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m_comboBuffer), "256 samples (~5.3ms, Low)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m_comboBuffer), "512 samples (~10.6ms, Fast)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m_comboBuffer), "1024 samples (~21.3ms, Smooth Default)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m_comboBuffer), "2048 samples (~42.6ms, Safe)");

    size_t curBuf = g_audioRecorder.GetBufferSize();
    int activeBufIdx = 3; // Default to 1024 samples (~21.3ms, Smooth Default)
    if (curBuf <= 128) activeBufIdx = 0;
    else if (curBuf <= 256) activeBufIdx = 1;
    else if (curBuf <= 512) activeBufIdx = 2;
    else if (curBuf <= 1024) activeBufIdx = 3;
    else activeBufIdx = 4;
    gtk_combo_box_set_active(GTK_COMBO_BOX(m_comboBuffer), activeBufIdx);

    g_signal_connect(m_comboBuffer, "changed", G_CALLBACK(+[](GtkComboBox* combo, gpointer user_data) {
        int idx = gtk_combo_box_get_active(combo);
        size_t newBuf = 1024;
        if (idx == 0) newBuf = 128;
        else if (idx == 1) newBuf = 256;
        else if (idx == 2) newBuf = 512;
        else if (idx == 3) newBuf = 1024;
        else if (idx == 4) newBuf = 2048;

        g_audioRecorder.SetBufferSize(newBuf);
        ServerConfig c = ConfigManager::Instance().GetConfig();
        c.bufferSize = newBuf;
        ConfigManager::Instance().UpdateConfig(c);
        ConfigManager::Instance().SaveConfig();

        if (GuiLinux::Instance().m_onBufferSizeChanged) {
            GuiLinux::Instance().m_onBufferSizeChanged(newBuf);
        }
    }), NULL);

    gtk_box_pack_start(GTK_BOX(rowBuffer), lblBuf, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rowBuffer), m_comboBuffer, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(listAudio), rowBuffer);

    gtk_box_pack_start(GTK_BOX(mainVBox), listAudio, FALSE, FALSE, 0);

    // Signal Attachments for Volume Changes
    auto onVolChangeLambda = +[](GtkWidget* w, gpointer data) {
        float master = (float)gtk_range_get_value(GTK_RANGE(GuiLinux::Instance().m_scaleMaster));
        float gainL = (float)gtk_range_get_value(GTK_RANGE(GuiLinux::Instance().m_scaleGainL));
        float gainR = (float)gtk_range_get_value(GTK_RANGE(GuiLinux::Instance().m_scaleGainR));

        gtk_label_set_text(GTK_LABEL(GuiLinux::Instance().m_lblMasterVal), (std::to_string((int)master) + "%").c_str());
        gtk_label_set_text(GTK_LABEL(GuiLinux::Instance().m_lblGainLVal), (std::to_string((int)gainL) + "%").c_str());
        gtk_label_set_text(GTK_LABEL(GuiLinux::Instance().m_lblGainRVal), (std::to_string((int)gainR) + "%").c_str());

        bool muted = (std::string(gtk_button_get_label(GTK_BUTTON(GuiLinux::Instance().m_btnMuteMaster))) == "Unmute");
        bool mutedL = (std::string(gtk_button_get_label(GTK_BUTTON(GuiLinux::Instance().m_btnMuteL))) == "Unmute L");
        bool mutedR = (std::string(gtk_button_get_label(GTK_BUTTON(GuiLinux::Instance().m_btnMuteR))) == "Unmute R");

        if (GuiLinux::Instance().m_onVolumeChanged) {
            GuiLinux::Instance().m_onVolumeChanged(master, gainL, gainR, muted, mutedL, mutedR);
        }
    };

    g_signal_connect(m_scaleMaster, "value-changed", G_CALLBACK(onVolChangeLambda), NULL);
    g_signal_connect(m_scaleGainL, "value-changed", G_CALLBACK(onVolChangeLambda), NULL);
    g_signal_connect(m_scaleGainR, "value-changed", G_CALLBACK(onVolChangeLambda), NULL);

    g_signal_connect(m_btnMuteMaster, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer udata) {
        bool isMuted = (std::string(gtk_button_get_label(btn)) == "Mute");
        gtk_button_set_label(btn, isMuted ? "Unmute" : "Mute");
        auto* ctx = gtk_widget_get_style_context(GTK_WIDGET(btn));
        if (isMuted) gtk_style_context_add_class(ctx, "btn-muted-active");
        else gtk_style_context_remove_class(ctx, "btn-muted-active");
        g_signal_emit_by_name(GuiLinux::Instance().m_scaleMaster, "value-changed");
    }), NULL);

    g_signal_connect(m_btnMuteL, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer udata) {
        bool isMuted = (std::string(gtk_button_get_label(btn)) == "Mute L");
        gtk_button_set_label(btn, isMuted ? "Unmute L" : "Mute L");
        auto* ctx = gtk_widget_get_style_context(GTK_WIDGET(btn));
        if (isMuted) gtk_style_context_add_class(ctx, "btn-muted-active");
        else gtk_style_context_remove_class(ctx, "btn-muted-active");
        g_signal_emit_by_name(GuiLinux::Instance().m_scaleMaster, "value-changed");
    }), NULL);

    g_signal_connect(m_btnMuteR, "clicked", G_CALLBACK(+[](GtkButton* btn, gpointer udata) {
        bool isMuted = (std::string(gtk_button_get_label(btn)) == "Mute R");
        gtk_button_set_label(btn, isMuted ? "Unmute R" : "Mute R");
        auto* ctx = gtk_widget_get_style_context(GTK_WIDGET(btn));
        if (isMuted) gtk_style_context_add_class(ctx, "btn-muted-active");
        else gtk_style_context_remove_class(ctx, "btn-muted-active");
        g_signal_emit_by_name(GuiLinux::Instance().m_scaleMaster, "value-changed");
    }), NULL);

    // --- 3. GROUP 2: Network & Clients (Native GtkListBox Group) ---
    GtkWidget* boxNetHeader = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* lblNetGroupHeader = gtk_label_new("NETWORK & CONNECTED CLIENTS");
    gtk_widget_set_halign(lblNetGroupHeader, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lblNetGroupHeader), "section-header");

    gtk_box_pack_start(GTK_BOX(boxNetHeader), lblNetGroupHeader, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(mainVBox), boxNetHeader, FALSE, FALSE, 0);

    GtkWidget* listNet = gtk_list_box_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(listNet), "inset-group");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(listNet), GTK_SELECTION_NONE);

    // Row: Primary IP Address
    GtkWidget* rowIp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* lblIpTag = gtk_label_new("Server IP:");
    gtk_widget_set_halign(lblIpTag, GTK_ALIGN_START);
    m_boxIpList = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    gtk_box_pack_start(GTK_BOX(rowIp), lblIpTag, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rowIp), m_boxIpList, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(listNet), rowIp);

    // Row: Connected Clients
    m_boxClientList = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_add(GTK_CONTAINER(listNet), m_boxClientList);

    gtk_box_pack_start(GTK_BOX(mainVBox), listNet, FALSE, FALSE, 0);

    // --- 4. Bottom System Settings Checkboxes ---
    ServerConfig cfg = ConfigManager::Instance().GetConfig();

    GtkWidget* boxCheckboxes = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    m_chkMinimizeToTray = gtk_check_button_new_with_label("Minimize to System Tray on close");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_chkMinimizeToTray), cfg.minimizeToTray);

    m_chkAutostart = gtk_check_button_new_with_label("Start automatically on system login");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m_chkAutostart), cfg.runOnStartup);
    g_signal_connect(m_chkAutostart, "toggled", G_CALLBACK(+[](GtkToggleButton* btn, gpointer udata) {
        bool enable = gtk_toggle_button_get_active(btn);
        ConfigManager::Instance().SetRunOnStartup(enable);
        ServerConfig c = ConfigManager::Instance().GetConfig();
        c.runOnStartup = enable;
        ConfigManager::Instance().UpdateConfig(c);
        ConfigManager::Instance().SaveConfig();
        if (GuiLinux::Instance().m_onAutostartToggled) {
            GuiLinux::Instance().m_onAutostartToggled(enable);
        }
    }), NULL);

    gtk_box_pack_start(GTK_BOX(boxCheckboxes), m_chkMinimizeToTray, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(boxCheckboxes), m_chkAutostart, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(mainVBox), boxCheckboxes, FALSE, FALSE, 0);

    // --- 5. Clean Bottom Status Bar ---
    m_statusBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_statusBar), "status-bar-container");

    m_lblStatusMode = gtk_label_new("⚡ Low Latency Audio Server Active");
    gtk_widget_set_halign(m_lblStatusMode, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_lblStatusMode), "status-mode-lbl");

    m_lblDiagnostics = gtk_label_new("Bitrate: ~1.5 Mbps | Buf: ~21.3ms | Total: ~33ms");
    gtk_widget_set_halign(m_lblDiagnostics, GTK_ALIGN_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(m_lblDiagnostics), "diag-badge");

    gtk_box_pack_start(GTK_BOX(m_statusBar), m_lblStatusMode, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(m_statusBar), m_lblDiagnostics, FALSE, FALSE, 0);

    gtk_box_pack_end(GTK_BOX(rootVBox), m_statusBar, FALSE, FALSE, 0);

    gtk_widget_show_all(m_mainWindow);
}

void GuiLinux::RunLoop() {
    if (m_initialized.load()) {
        gtk_main();
    }
}

void GuiLinux::Quit() {
    if (m_onQuit) m_onQuit();
    if (m_initialized.load()) {
        m_initialized.store(false);
        g_idle_add([](gpointer data) -> gboolean {
            gtk_main_quit();
            return G_SOURCE_REMOVE;
        }, NULL);
    }
}

void GuiLinux::ToggleVisibility() {
    if (!m_initialized.load() || !m_mainWindow) return;
    g_idle_add([](gpointer data) -> gboolean {
        if (GuiLinux::Instance().m_mainWindow) {
            if (gtk_widget_is_visible(GuiLinux::Instance().m_mainWindow)) {
                gtk_widget_hide(GuiLinux::Instance().m_mainWindow);
            } else {
                gtk_widget_show_all(GuiLinux::Instance().m_mainWindow);
                gtk_window_present(GTK_WINDOW(GuiLinux::Instance().m_mainWindow));
            }
        }
        return G_SOURCE_REMOVE;
    }, NULL);
}

bool GuiLinux::IsVisible() const {
    return m_initialized.load() && m_mainWindow && gtk_widget_is_visible(m_mainWindow);
}

void GuiLinux::UpdateAudioPeak(float level) {
    if (!m_initialized.load() || !m_levelMeter) return;
    g_idle_add([](gpointer data) -> gboolean {
        float peak = *static_cast<float*>(data);
        if (GuiLinux::Instance().m_levelMeter) {
            gtk_level_bar_set_value(GTK_LEVEL_BAR(GuiLinux::Instance().m_levelMeter), peak);
        }
        delete static_cast<float*>(data);
        return G_SOURCE_REMOVE;
    }, new float(level));
}

void GuiLinux::UpdateDiagnostics(float bitrateMbps, float bufLatencyMs, float totalEstLatencyMs) {
    if (!m_initialized.load() || !m_lblDiagnostics) return;
    struct DiagParams { float bitrate, bufLatency, totalLatency; };
    auto* params = new DiagParams{ bitrateMbps, bufLatencyMs, totalEstLatencyMs };

    g_idle_add([](gpointer data) -> gboolean {
        auto* p = static_cast<DiagParams*>(data);
        if (p && GuiLinux::Instance().m_lblDiagnostics) {
            std::stringstream ss;
            ss << "Bitrate: " << std::fixed << std::setprecision(1) << p->bitrate << " Mbps | Buf: ~" << std::fixed << std::setprecision(1) << p->bufLatency << "ms | Total: ~" << static_cast<int>(p->totalLatency) << "ms";
            gtk_label_set_text(GTK_LABEL(GuiLinux::Instance().m_lblDiagnostics), ss.str().c_str());
        }
        delete p;
        return G_SOURCE_REMOVE;
    }, params);
}

struct UpdateStatusParams {
    bool serverActive;
    std::vector<std::string> localIps;
    std::vector<ClientInfo> clients;
};

void GuiLinux::UpdateStatus(bool serverActive, const std::vector<std::string>& localIps, const std::vector<ClientInfo>& clients) {
    if (!m_initialized.load()) return;

    if (m_hasInitialStatus && 
        m_lastServerActive == serverActive && 
        m_lastLocalIps == localIps && 
        m_lastClientCount == clients.size()) {
        return;
    }

    m_hasInitialStatus = true;
    m_lastServerActive = serverActive;
    m_lastLocalIps = localIps;
    m_lastClientCount = clients.size();

    auto* params = new UpdateStatusParams{ serverActive, localIps, clients };
    g_idle_add([](gpointer data) -> gboolean {
        auto* p = static_cast<UpdateStatusParams*>(data);
        if (p && GuiLinux::Instance().m_initialized.load()) {
            if (GuiLinux::Instance().m_btnServerToggle) {
                gtk_button_set_label(GTK_BUTTON(GuiLinux::Instance().m_btnServerToggle), p->serverActive ? "⏹" : "▶");
                gtk_widget_set_tooltip_text(GuiLinux::Instance().m_btnServerToggle, p->serverActive ? "Stop Audio Server" : "Start Audio Server");
                auto* ctx = gtk_widget_get_style_context(GuiLinux::Instance().m_btnServerToggle);
                if (p->serverActive) {
                    gtk_style_context_remove_class(ctx, "suggested-action");
                    gtk_style_context_add_class(ctx, "destructive-action");
                } else {
                    gtk_style_context_remove_class(ctx, "destructive-action");
                    gtk_style_context_add_class(ctx, "suggested-action");
                }
            }

            // Update IP List Box
            if (GuiLinux::Instance().m_boxIpList) {
                GList* children = gtk_container_get_children(GTK_CONTAINER(GuiLinux::Instance().m_boxIpList));
                for (GList* iter = children; iter != NULL; iter = g_list_next(iter)) {
                    gtk_widget_destroy(GTK_WIDGET(iter->data));
                }
                g_list_free(children);

                std::vector<std::string> physicalIps;
                for (const auto& ip : p->localIps) {
                    if (ip.rfind("172.", 0) != 0 && ip.rfind("127.", 0) != 0) {
                        physicalIps.push_back(ip);
                    }
                }
                if (physicalIps.empty() && !p->localIps.empty()) physicalIps.push_back(p->localIps[0]);

                for (const auto& ip : physicalIps) {
                    std::string btnLabel = ip + "  📋";
                    GtkWidget* btnIp = gtk_button_new_with_label(btnLabel.c_str());

                    char* ipStrCopy = g_strdup(ip.c_str());
                    g_object_set_data_full(G_OBJECT(btnIp), "ip_data", ipStrCopy, g_free);
                    g_signal_connect(btnIp, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer udata) {
                        const char* ipStr = (const char*)g_object_get_data(G_OBJECT(b), "ip_data");
                        if (ipStr) {
                            GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
                            gtk_clipboard_set_text(clipboard, ipStr, -1);
                            gtk_button_set_label(b, "Copied to Clipboard! 📋");
                            g_timeout_add(1500, [](gpointer btnData) -> gboolean {
                                GtkButton* btn = GTK_BUTTON(btnData);
                                const char* origIp = (const char*)g_object_get_data(G_OBJECT(btn), "ip_data");
                                if (origIp) {
                                    gtk_button_set_label(btn, (std::string(origIp) + "  📋").c_str());
                                }
                                return G_SOURCE_REMOVE;
                            }, b);
                        }
                    }), NULL);
                    gtk_box_pack_start(GTK_BOX(GuiLinux::Instance().m_boxIpList), btnIp, FALSE, FALSE, 0);
                }
                gtk_widget_show_all(GuiLinux::Instance().m_boxIpList);
            }

            // Update Client List Box with Kick Button & Channel Mode Dropdown
            if (GuiLinux::Instance().m_boxClientList) {
                GList* children = gtk_container_get_children(GTK_CONTAINER(GuiLinux::Instance().m_boxClientList));
                for (GList* iter = children; iter != NULL; iter = g_list_next(iter)) {
                    gtk_widget_destroy(GTK_WIDGET(iter->data));
                }
                g_list_free(children);

                if (p->clients.empty()) {
                    GtkWidget* lblEmpty = gtk_label_new(NULL);
                    gtk_label_set_markup(GTK_LABEL(lblEmpty), "<span foreground='#94a3b8'>📱 No clients connected</span>");
                    gtk_widget_set_halign(lblEmpty, GTK_ALIGN_START);
                    gtk_box_pack_start(GTK_BOX(GuiLinux::Instance().m_boxClientList), lblEmpty, FALSE, FALSE, 0);
                } else {
                    for (size_t idx = 0; idx < p->clients.size(); ++idx) {
                        const auto& c = p->clients[idx];
                        GtkWidget* clientRowBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
                        std::string clientText = "📱 Client " + std::to_string(c.id) + ": " + c.ip;
                        GtkWidget* lblClient = gtk_label_new(clientText.c_str());
                        gtk_widget_set_halign(lblClient, GTK_ALIGN_START);

                        GtkWidget* comboMode = gtk_combo_box_text_new();
                        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboMode), "Stereo");
                        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboMode), "Left Channel");
                        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comboMode), "Right Channel");
                        
                        int activeIdx = 0;
                        if (c.channelMode == "LEFT") activeIdx = 1;
                        else if (c.channelMode == "RIGHT") activeIdx = 2;
                        gtk_combo_box_set_active(GTK_COMBO_BOX(comboMode), activeIdx);

                        g_object_set_data(G_OBJECT(comboMode), "client_index", (gpointer)(uintptr_t)idx);
                        g_signal_connect(comboMode, "changed", G_CALLBACK(+[](GtkComboBox* combo, gpointer u) {
                            int clientIndex = (int)(uintptr_t)g_object_get_data(G_OBJECT(combo), "client_index");
                            int modeTag = gtk_combo_box_get_active(combo);
                            if (GuiLinux::Instance().m_onClientModeChanged) {
                                GuiLinux::Instance().m_onClientModeChanged(clientIndex, modeTag);
                            }
                        }), NULL);

                        GtkWidget* btnKick = gtk_button_new_with_label("Kick ❌");
                        gtk_style_context_add_class(gtk_widget_get_style_context(btnKick), "btn-kick");
                        g_object_set_data(G_OBJECT(btnKick), "client_index", (gpointer)(uintptr_t)idx);
                        g_signal_connect(btnKick, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer u) {
                            int clientIndex = (int)(uintptr_t)g_object_get_data(G_OBJECT(b), "client_index");
                            if (GuiLinux::Instance().m_onKickClient) {
                                GuiLinux::Instance().m_onKickClient(clientIndex);
                            }
                        }), NULL);

                        gtk_box_pack_start(GTK_BOX(clientRowBox), lblClient, TRUE, TRUE, 0);
                        gtk_box_pack_start(GTK_BOX(clientRowBox), comboMode, FALSE, FALSE, 0);
                        gtk_box_pack_start(GTK_BOX(clientRowBox), btnKick, FALSE, FALSE, 0);
                        gtk_box_pack_start(GTK_BOX(GuiLinux::Instance().m_boxClientList), clientRowBox, FALSE, FALSE, 0);
                    }
                }
                gtk_widget_show_all(GuiLinux::Instance().m_boxClientList);
            }
        }
        delete p;
        return G_SOURCE_REMOVE;
    }, params);
}

void GuiLinux::UpdateAudioSettings(float masterVol, float gainL, float gainR, bool muted, bool mutedL, bool mutedR) {
    if (!m_initialized.load()) return;

    struct AudioParams { float masterVol, gainL, gainR; bool muted, mutedL, mutedR; };
    auto* params = new AudioParams{ masterVol, gainL, gainR, muted, mutedL, mutedR };

    g_idle_add([](gpointer data) -> gboolean {
        auto* p = static_cast<AudioParams*>(data);
        if (p && GuiLinux::Instance().m_initialized.load()) {
            if (GuiLinux::Instance().m_scaleMaster) gtk_range_set_value(GTK_RANGE(GuiLinux::Instance().m_scaleMaster), p->masterVol);
            if (GuiLinux::Instance().m_scaleGainL) gtk_range_set_value(GTK_RANGE(GuiLinux::Instance().m_scaleGainL), p->gainL);
            if (GuiLinux::Instance().m_scaleGainR) gtk_range_set_value(GTK_RANGE(GuiLinux::Instance().m_scaleGainR), p->gainR);

            if (GuiLinux::Instance().m_btnMuteMaster) gtk_button_set_label(GTK_BUTTON(GuiLinux::Instance().m_btnMuteMaster), p->muted ? "Unmute" : "Mute");
            if (GuiLinux::Instance().m_btnMuteL) gtk_button_set_label(GTK_BUTTON(GuiLinux::Instance().m_btnMuteL), p->mutedL ? "Unmute L" : "Mute L");
            if (GuiLinux::Instance().m_btnMuteR) gtk_button_set_label(GTK_BUTTON(GuiLinux::Instance().m_btnMuteR), p->mutedR ? "Unmute R" : "Mute R");
        }
        delete p;
        return G_SOURCE_REMOVE;
    }, params);
}

#else

bool GuiLinux::Initialize(int argc, char** argv) {
    LOG_INFO("[GUI] Compiled without GTK3 headers. Running in Console Mode.");
    return false;
}
void GuiLinux::RunLoop() {}
void GuiLinux::Quit() { if (m_onQuit) m_onQuit(); }
void GuiLinux::ToggleVisibility() {}
bool GuiLinux::IsVisible() const { return false; }
void GuiLinux::UpdateStatus(bool active, const std::vector<std::string>& ips, const std::vector<ClientInfo>& clients) {}
void GuiLinux::UpdateAudioSettings(float master, float gL, float gR, bool m, bool mL, bool mR) {}
void GuiLinux::UpdateAudioPeak(float level) {}
void GuiLinux::UpdateDiagnostics(float b, float bufL, float totL) {}
void GuiLinux::AddLogMessage(const std::string& msg) {}
void GuiLinux::ShowLogDialog() {}
void GuiLinux::BuildUI() {}

#endif
