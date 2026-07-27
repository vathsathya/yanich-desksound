#ifndef AUDIO_PULSE_H
#define AUDIO_PULSE_H

#include <pulse/simple.h>
#include <pulse/error.h>
#include <pulse/pulseaudio.h>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>

struct PulseAudioDevice {
    std::string name;
    std::string description;
};

class PulseAudioRecorder {
public:
    PulseAudioRecorder();
    ~PulseAudioRecorder();

    bool StartCapture(std::function<void(const uint8_t*, size_t)> audioCallback, 
                      const std::string& deviceName = "@DEFAULT_MONITOR@", 
                      size_t targetBufferSize = 1024);
    void StopCapture();

    bool IsCapturing() const { return m_isCapturing; }

    void SetVolume(float masterVolPercent, float gainLPercent, float gainRPercent, bool muted, bool mutedL, bool mutedR);
    void SetBufferSize(size_t newBufferSize);
    float GetAudioPeakLevel() const { return m_peakLevel.load(); }
    size_t GetBufferSize() const { return m_targetBufferSize; }

    std::vector<PulseAudioDevice> EnumerateDevices();

private:
    void CaptureLoop();
    void ApplyVolumeAndMute(float* samples, size_t sampleCount);

    pa_simple* m_paSimple = nullptr;
    std::atomic<bool> m_isCapturing{false};
    std::thread m_captureThread;
    std::function<void(const uint8_t*, size_t)> m_callback;

    std::string m_currentDevice = "@DEFAULT_MONITOR@";
    size_t m_targetBufferSize = 1024;

    std::atomic<float> m_masterVolume{1.0f};
    std::atomic<float> m_gainL{1.0f};
    std::atomic<float> m_gainR{1.0f};
    std::atomic<bool> m_muted{false};
    std::atomic<bool> m_mutedL{false};
    std::atomic<bool> m_mutedR{false};

    std::atomic<float> m_peakLevel{0.0f};
};

#endif // AUDIO_PULSE_H
