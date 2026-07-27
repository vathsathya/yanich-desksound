#include "audio_pulse.h"
#include "logger.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <chrono>
#include <cmath>
#include <algorithm>

PulseAudioRecorder::PulseAudioRecorder() {}

PulseAudioRecorder::~PulseAudioRecorder() {
    StopCapture();
}

static std::string TrimString(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

void PulseAudioRecorder::SetVolume(float masterVolPercent, float gainLPercent, float gainRPercent, bool muted, bool mutedL, bool mutedR) {
    m_masterVolume.store(std::clamp(masterVolPercent, 0.0f, 200.0f) / 100.0f);
    m_gainL.store(std::clamp(gainLPercent, 0.0f, 200.0f) / 100.0f);
    m_gainR.store(std::clamp(gainRPercent, 0.0f, 200.0f) / 100.0f);
    m_muted.store(muted);
    m_mutedL.store(mutedL);
    m_mutedR.store(mutedR);
}

void PulseAudioRecorder::SetBufferSize(size_t newBufferSize) {
    if (m_targetBufferSize == newBufferSize) return;
    LOG_INFO("[PulseAudio] Changing buffer size from " + std::to_string(m_targetBufferSize) + " to " + std::to_string(newBufferSize) + " samples...");
    m_targetBufferSize = newBufferSize;
    if (m_isCapturing) {
        auto cb = m_callback;
        std::string dev = m_currentDevice;
        StopCapture();
        StartCapture(cb, dev, newBufferSize);
    }
}

bool PulseAudioRecorder::StartCapture(std::function<void(const uint8_t*, size_t)> audioCallback,
                                      const std::string& deviceName,
                                      size_t targetBufferSize) {
    if (m_isCapturing) StopCapture();

    m_callback = audioCallback;
    m_currentDevice = deviceName.empty() ? "@DEFAULT_MONITOR@" : deviceName;
    m_targetBufferSize = targetBufferSize > 0 ? targetBufferSize : 1024; // Default Smooth 1024 samples (~21.3ms)

    static const pa_sample_spec ss = {
        .format = PA_SAMPLE_FLOAT32LE,
        .rate = 48000,
        .channels = 2
    };

    // Smooth low-latency PulseAudio hardware buffer attributes
    uint32_t fragBytes = static_cast<uint32_t>(m_targetBufferSize * sizeof(float) * 2);
    pa_buffer_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.maxlength = (uint32_t)-1;
    attr.tlength = (uint32_t)-1;
    attr.prebuf = (uint32_t)-1;
    attr.minreq = (uint32_t)-1;
    attr.fragsize = fragBytes;

    int error = 0;
    const char* devStr = (m_currentDevice == "@DEFAULT_MONITOR@") ? "@DEFAULT_MONITOR@" : m_currentDevice.c_str();

    LOG_INFO("Opening Smooth Low Latency PulseAudio stream on device: " + m_currentDevice + " (fragsize: " + std::to_string(fragBytes) + " bytes, buffer: " + std::to_string(m_targetBufferSize) + " samples)");

    m_paSimple = pa_simple_new(NULL, "Yanich DeskSound", PA_STREAM_RECORD, devStr, "Audio Loopback Capture", &ss, NULL, &attr, &error);
    if (!m_paSimple && m_currentDevice == "@DEFAULT_MONITOR@") {
        LOG_WARN("[PulseAudio] @DEFAULT_MONITOR@ fallback to default source...");
        m_paSimple = pa_simple_new(NULL, "Yanich DeskSound", PA_STREAM_RECORD, NULL, "Audio Loopback Capture", &ss, NULL, &attr, &error);
    }

    if (!m_paSimple) {
        LOG_ERROR("[PulseAudio] Error opening loopback stream: " + std::string(pa_strerror(error)));
        return false;
    }

    m_isCapturing = true;
    m_captureThread = std::thread(&PulseAudioRecorder::CaptureLoop, this);
    LOG_INFO("[PulseAudio] Crystal-clear audio capture loopback started at 48000Hz 32-bit Float Stereo.");
    return true;
}

void PulseAudioRecorder::StopCapture() {
    m_isCapturing = false;
    m_peakLevel.store(0.0f);

    // Join capture thread FIRST so CaptureLoop completes its current pa_simple_read cleanly
    if (m_captureThread.joinable()) {
        m_captureThread.join();
    }

    // Safely free PulseAudio handle on main thread AFTER capture loop thread has safely exited
    if (m_paSimple) {
        pa_simple_free(m_paSimple);
        m_paSimple = nullptr;
    }
}

void PulseAudioRecorder::ApplyVolumeAndMute(float* samples, size_t sampleCount) {
    bool isMuted = m_muted.load();
    bool isMutedL = m_mutedL.load();
    bool isMutedR = m_mutedR.load();
    float masterVol = m_masterVolume.load();
    float gainL = m_gainL.load() * masterVol;
    float gainR = m_gainR.load() * masterVol;

    if (isMuted) {
        std::memset(samples, 0, sampleCount * sizeof(float));
        return;
    }

    size_t frameCount = sampleCount / 2;
    float maxPeak = 0.0f;

    for (size_t i = 0; i < frameCount; ++i) {
        float left = samples[i * 2 + 0];
        float right = samples[i * 2 + 1];

        if (isMutedL) left = 0.0f;
        else left *= gainL;

        if (isMutedR) right = 0.0f;
        else right *= gainR;

        // Hard limiter to prevent digital clipping (>1.0f or <-1.0f)
        if (left > 1.0f) left = 1.0f;
        else if (left < -1.0f) left = -1.0f;

        if (right > 1.0f) right = 1.0f;
        else if (right < -1.0f) right = -1.0f;

        samples[i * 2 + 0] = left;
        samples[i * 2 + 1] = right;

        float absL = std::abs(left);
        float absR = std::abs(right);
        if (absL > maxPeak) maxPeak = absL;
        if (absR > maxPeak) maxPeak = absR;
    }

    m_peakLevel.store(maxPeak);
}

void PulseAudioRecorder::CaptureLoop() {
    size_t bufferSizeBytes = m_targetBufferSize * sizeof(float) * 2;
    std::vector<uint8_t> buffer(bufferSizeBytes);
    int error = 0;

    while (m_isCapturing) {
        if (!m_paSimple) break;
        if (pa_simple_read(m_paSimple, buffer.data(), bufferSizeBytes, &error) < 0) {
            if (m_isCapturing) {
                LOG_ERROR("[PulseAudio] Read error: " + std::string(pa_strerror(error)));
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            break;
        }

        if (!m_isCapturing) break;

        float* floatSamples = reinterpret_cast<float*>(buffer.data());
        size_t sampleCount = bufferSizeBytes / sizeof(float);

        ApplyVolumeAndMute(floatSamples, sampleCount);

        if (m_callback) {
            m_callback(buffer.data(), bufferSizeBytes);
        }
    }
}

std::vector<PulseAudioDevice> PulseAudioRecorder::EnumerateDevices() {
    std::vector<PulseAudioDevice> devices;
    devices.push_back({ "@DEFAULT_MONITOR@", "Default System Audio Loopback" });

    FILE* pipe = popen("pactl list sources 2>/dev/null", "r");
    if (!pipe) return devices;

    char buffer[512];
    std::string currentName, currentDesc;
    bool isMonitor = false;

    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        std::string line(buffer);
        line = TrimString(line);

        if (line.rfind("Name:", 0) == 0) {
            if (!currentName.empty() && isMonitor) {
                devices.push_back({ currentName, currentDesc.empty() ? currentName : currentDesc });
            }
            currentName = TrimString(line.substr(5));
            currentDesc.clear();
            isMonitor = (currentName.find(".monitor") != std::string::npos);
        } else if (line.rfind("Description:", 0) == 0) {
            currentDesc = TrimString(line.substr(12));
        }
    }
    pclose(pipe);

    if (!currentName.empty() && isMonitor) {
        devices.push_back({ currentName, currentDesc.empty() ? currentName : currentDesc });
    }

    return devices;
}
