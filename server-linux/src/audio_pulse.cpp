#include "audio_pulse.h"
#include <iostream>

PulseAudioRecorder::PulseAudioRecorder() {}

PulseAudioRecorder::~PulseAudioRecorder() {
    StopCapture();
}

bool PulseAudioRecorder::StartCapture(std::function<void(const uint8_t*, size_t)> audioCallback) {
    if (m_isCapturing) return true;

    m_callback = audioCallback;
    static const pa_sample_spec ss = {
        .format = PA_SAMPLE_FLOAT32LE,
        .rate = 48000,
        .channels = 2
    };

    int error = 0;
    // Capture from default monitor sink (system audio output loopback)
    m_paSimple = pa_simple_new(NULL, "Yanich DeskSound", PA_STREAM_RECORD, "@DEFAULT_MONITOR@", "Audio Loopback Capture", &ss, NULL, NULL, &error);
    if (!m_paSimple) {
        std::cerr << "[PulseAudio] Info: @DEFAULT_MONITOR@ fallback to default source..." << std::endl;
        m_paSimple = pa_simple_new(NULL, "Yanich DeskSound", PA_STREAM_RECORD, NULL, "Audio Loopback Capture", &ss, NULL, NULL, &error);
    }
    if (!m_paSimple) {
        std::cerr << "[PulseAudio] Error opening loopback stream: " << pa_strerror(error) << std::endl;
        return false;
    }

    m_isCapturing = true;
    m_captureThread = std::thread(&PulseAudioRecorder::CaptureLoop, this);
    std::cout << "[PulseAudio] Capture loopback started at 48000Hz 32-bit Float Stereo." << std::endl;
    return true;
}

void PulseAudioRecorder::StopCapture() {
    m_isCapturing = false;
    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }
    if (m_paSimple) {
        pa_simple_free(m_paSimple);
        m_paSimple = nullptr;
    }
}

void PulseAudioRecorder::CaptureLoop() {
    uint8_t buffer[4096];
    int error = 0;

    while (m_isCapturing) {
        if (pa_simple_read(m_paSimple, buffer, sizeof(buffer), &error) < 0) {
            std::cerr << "[PulseAudio] Read error: " << pa_strerror(error) << std::endl;
            break;
        }
        if (m_callback) {
            m_callback(buffer, sizeof(buffer));
        }
    }
}
