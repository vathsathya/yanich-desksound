#ifndef AUDIO_PULSE_H
#define AUDIO_PULSE_H

#include <pulse/simple.h>
#include <pulse/error.h>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>

class PulseAudioRecorder {
public:
    PulseAudioRecorder();
    ~PulseAudioRecorder();

    bool StartCapture(std::function<void(const uint8_t*, size_t)> audioCallback);
    void StopCapture();

    bool IsCapturing() const { return m_isCapturing; }

private:
    void CaptureLoop();

    pa_simple* m_paSimple = nullptr;
    std::atomic<bool> m_isCapturing{false};
    std::thread m_captureThread;
    std::function<void(const uint8_t*, size_t)> m_callback;
};

#endif // AUDIO_PULSE_H
