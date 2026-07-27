#ifndef _WIN32

#include "../include/audio_backend.h"
#include "../include/logger.h"
#include <pulse/simple.h>
#include <pulse/error.h>
#include <thread>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>

class AudioPulse : public AudioBackend {
public:
    AudioPulse() : m_running(false), m_paSimple(nullptr), m_bufferSize(1024), m_peakL(0.0f), m_peakR(0.0f), m_selectedIdx(0) {}
    ~AudioPulse() override { StopCapture(); }

    bool Initialize() override {
        return true;
    }

    void StartCapture() override {
        if (m_running.load()) return;
        m_running.store(true);
        m_workerThread = std::thread(&AudioPulse::Loop, this);
    }

    void StopCapture() override {
        m_running.store(false);
        m_peakL.store(0.0f);
        m_peakR.store(0.0f);

        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }

        if (m_paSimple) {
            pa_simple_free(m_paSimple);
            m_paSimple = nullptr;
        }
    }

    std::vector<AudioDeviceInfo> EnumerateDevices() override {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<AudioDeviceInfo> list;
        list.push_back({ 0, "@DEFAULT_MONITOR@", "Default System Audio Loopback" });

        FILE* pipe = popen("pactl list sources 2>/dev/null", "r");
        if (pipe) {
            char buffer[512];
            std::string currentName, currentDesc;
            bool isMonitor = false;
            int idx = 1;

            while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
                std::string line(buffer);
                size_t first = line.find_first_not_of(" \t\r\n");
                if (first != std::string::npos) line = line.substr(first);

                if (line.rfind("Name:", 0) == 0) {
                    if (!currentName.empty() && isMonitor) {
                        list.push_back({ idx++, currentName, currentDesc.empty() ? currentName : currentDesc });
                    }
                    currentName = line.substr(5);
                    size_t p1 = currentName.find_first_not_of(" \t\r\n");
                    size_t p2 = currentName.find_last_not_of(" \t\r\n");
                    if (p1 != std::string::npos && p2 != std::string::npos) currentName = currentName.substr(p1, p2 - p1 + 1);

                    currentDesc.clear();
                    isMonitor = (currentName.find(".monitor") != std::string::npos);
                } else if (line.rfind("Description:", 0) == 0) {
                    currentDesc = line.substr(12);
                    size_t p1 = currentDesc.find_first_not_of(" \t\r\n");
                    size_t p2 = currentDesc.find_last_not_of(" \t\r\n");
                    if (p1 != std::string::npos && p2 != std::string::npos) currentDesc = currentDesc.substr(p1, p2 - p1 + 1);
                }
            }
            pclose(pipe);

            if (!currentName.empty() && isMonitor) {
                list.push_back({ idx++, currentName, currentDesc.empty() ? currentName : currentDesc });
            }
        }

        m_devices = list;
        return list;
    }

    void SelectDevice(int index) override {
        m_selectedIdx.store(index);
        m_deviceChanged.store(true);
    }

    void SetBufferSize(size_t samples) override {
        m_bufferSize.store(samples);
        m_deviceChanged.store(true);
    }

    size_t GetBufferSize() const override {
        return m_bufferSize.load();
    }

    float GetPeakLevelL() const override { return m_peakL.load(); }
    float GetPeakLevelR() const override { return m_peakR.load(); }

private:
    void Loop() {
        while (m_running.load()) {
            std::string devName = "@DEFAULT_MONITOR@";
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                int selIdx = m_selectedIdx.load();
                if (selIdx >= 0 && selIdx < (int)m_devices.size()) {
                    devName = m_devices[selIdx].id;
                }
            }

            size_t targetBuf = m_bufferSize.load();
            static const pa_sample_spec ss = {
                .format = PA_SAMPLE_FLOAT32LE,
                .rate = 48000,
                .channels = 2
            };

            uint32_t fragBytes = static_cast<uint32_t>(targetBuf * sizeof(float) * 2);
            pa_buffer_attr attr;
            memset(&attr, 0, sizeof(attr));
            attr.maxlength = (uint32_t)-1;
            attr.tlength = (uint32_t)-1;
            attr.prebuf = (uint32_t)-1;
            attr.minreq = (uint32_t)-1;
            attr.fragsize = fragBytes;

            int error = 0;
            const char* devStr = (devName == "@DEFAULT_MONITOR@") ? "@DEFAULT_MONITOR@" : devName.c_str();
            m_paSimple = pa_simple_new(NULL, "Yanich DeskSound", PA_STREAM_RECORD, devStr, "Audio Loopback Capture", &ss, NULL, &attr, &error);

            if (!m_paSimple) {
                m_paSimple = pa_simple_new(NULL, "Yanich DeskSound", PA_STREAM_RECORD, NULL, "Audio Loopback Capture", &ss, NULL, &attr, &error);
            }

            if (!m_paSimple) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }

            size_t bufferSizeBytes = targetBuf * sizeof(float) * 2;
            std::vector<float> floatBuf(targetBuf * 2);

            while (m_running.load()) {
                if (m_deviceChanged.load()) {
                    m_deviceChanged.store(false);
                    break;
                }

                if (pa_simple_read(m_paSimple, floatBuf.data(), bufferSizeBytes, &error) < 0) {
                    break;
                }

                float maxL = 0.0f, maxR = 0.0f;
                size_t frames = targetBuf;

                for (size_t i = 0; i < frames; ++i) {
                    float sampleL = floatBuf[i * 2 + 0];
                    float sampleR = floatBuf[i * 2 + 1];
                    maxL = std::max(maxL, std::abs(sampleL));
                    maxR = std::max(maxR, std::abs(sampleR));
                }

                m_peakL.store(maxL);
                m_peakR.store(maxR);

                if (m_dataCallback) {
                    m_dataCallback(floatBuf.data(), frames, 2, 48000);
                }
            }

            if (m_paSimple) {
                pa_simple_free(m_paSimple);
                m_paSimple = nullptr;
            }
        }
    }

    std::atomic<bool> m_running;
    std::atomic<bool> m_deviceChanged{ false };
    pa_simple* m_paSimple;
    std::atomic<size_t> m_bufferSize;
    std::atomic<int> m_selectedIdx;
    std::atomic<float> m_peakL;
    std::atomic<float> m_peakR;
    std::mutex m_mutex;
    std::thread m_workerThread;
    std::vector<AudioDeviceInfo> m_devices;
};

AudioBackend* CreateAudioBackend() {
    return new AudioPulse();
}

#endif // !_WIN32
