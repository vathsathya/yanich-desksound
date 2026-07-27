#ifndef AUDIO_BACKEND_H
#define AUDIO_BACKEND_H

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <functional>

struct AudioDeviceInfo {
    int index;
    std::string id;
    std::string name;
};

class AudioBackend {
public:
    virtual ~AudioBackend() = default;

    virtual bool Initialize() = 0;
    virtual void StartCapture() = 0;
    virtual void StopCapture() = 0;
    virtual std::vector<AudioDeviceInfo> EnumerateDevices() = 0;
    virtual void SelectDevice(int index) = 0;
    virtual void SetBufferSize(size_t samples) = 0;
    virtual size_t GetBufferSize() const = 0;

    virtual float GetPeakLevelL() const = 0;
    virtual float GetPeakLevelR() const = 0;

    void SetDataCallback(std::function<void(const float* data, size_t frames, int channels, int sampleRate)> cb) {
        m_dataCallback = cb;
    }

protected:
    std::function<void(const float* data, size_t frames, int channels, int sampleRate)> m_dataCallback;
};

AudioBackend* CreateAudioBackend();

#endif // AUDIO_BACKEND_H
