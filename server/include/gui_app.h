#ifndef GUI_APP_H
#define GUI_APP_H

#include "../include/audio_backend.h"

class GuiApp {
public:
    static GuiApp& Instance();

    void Initialize();
    void RenderUI(AudioBackend* audioBackend);

    bool ShowLogsModal() const { return m_showLogsModal; }
    void SetShowLogsModal(bool show) { m_showLogsModal = show; }

private:
    GuiApp() = default;
    bool m_showLogsModal{ false };
    bool m_showSettingsModal{ false };
    bool m_showAboutModal{ false };
};

#endif // GUI_APP_H
