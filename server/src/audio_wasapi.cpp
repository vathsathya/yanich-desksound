#ifdef _WIN32

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "../include/audio_backend.h"
#include "../include/logger.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <thread>
#include <cmath>
#include <algorithm>

template <class T> void SafeReleaseWASAPI(T** ppT) {
    if (*ppT) { (*ppT)->Release(); *ppT = NULL; }
}

class AudioWasapi : public AudioBackend {
public:
    AudioWasapi() : m_running(false), m_peakL(0.0f), m_peakR(0.0f), m_bufferSize(1024), m_selectedIdx(0) {}
    ~AudioWasapi() override { StopCapture(); }

    bool Initialize() override {
        return true;
    }

    void StartCapture() override {
        if (m_running.load()) return;
        m_running.store(true);
        m_workerThread = std::thread(&AudioWasapi::Loop, this);
    }

    void StopCapture() override {
        if (!m_running.load()) return;
        m_running.store(false);
        if (m_workerThread.joinable()) {
            m_workerThread.join();
        }
    }

    std::vector<AudioDeviceInfo> EnumerateDevices() override {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<AudioDeviceInfo> list;

        CoInitialize(NULL);
        IMMDeviceEnumerator* pEnumerator = NULL;
        IMMDeviceCollection* pCollection = NULL;

        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator))) {
            list.push_back({ 0, "", "Default System Playback Device" });

            if (SUCCEEDED(pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection))) {
                UINT count = 0;
                pCollection->GetCount(&count);

                for (UINT i = 0; i < count; ++i) {
                    IMMDevice* pEndpoint = NULL;
                    if (SUCCEEDED(pCollection->Item(i, &pEndpoint))) {
                        LPWSTR pwszID = NULL;
                        pEndpoint->GetId(&pwszID);

                        IPropertyStore* pProps = NULL;
                        std::string devName = "Audio Endpoint";

                        if (SUCCEEDED(pEndpoint->OpenPropertyStore(STGM_READ, &pProps))) {
                            PROPVARIANT varName;
                            PropVariantInit(&varName);
                            if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &varName))) {
                                char nameBuf[256] = { 0 };
                                WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, nameBuf, 256, NULL, NULL);
                                devName = nameBuf;
                                PropVariantClear(&varName);
                            }
                            SafeReleaseWASAPI(&pProps);
                        }

                        char idBuf[512] = { 0 };
                        if (pwszID) {
                            WideCharToMultiByte(CP_UTF8, 0, pwszID, -1, idBuf, 512, NULL, NULL);
                            CoTaskMemFree(pwszID);
                        }

                        list.push_back({ (int)i + 1, idBuf, devName });
                        SafeReleaseWASAPI(&pEndpoint);
                    }
                }
                SafeReleaseWASAPI(&pCollection);
            }
            SafeReleaseWASAPI(&pEnumerator);
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
    }

    size_t GetBufferSize() const override {
        return m_bufferSize.load();
    }

    float GetPeakLevelL() const override { return m_peakL.load(); }
    float GetPeakLevelR() const override { return m_peakR.load(); }

private:
    void Loop() {
        CoInitialize(NULL);
        while (m_running.load()) {
            IMMDeviceEnumerator* pEnumerator = NULL;
            IMMDevice* pDevice = NULL;
            IAudioClient* pAudioClient = NULL;
            IAudioCaptureClient* pCaptureClient = NULL;
            WAVEFORMATEX* pwfx = NULL;

            if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator))) {
                Sleep(500); continue;
            }

            int selIdx = m_selectedIdx.load();
            std::string selId = "";
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (selIdx > 0 && selIdx < (int)m_devices.size()) {
                    selId = m_devices[selIdx].id;
                }
            }

            if (selId.empty()) {
                if (FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice))) {
                    SafeReleaseWASAPI(&pEnumerator); Sleep(500); continue;
                }
            } else {
                wchar_t wId[512];
                MultiByteToWideChar(CP_UTF8, 0, selId.c_str(), -1, wId, 512);
                if (FAILED(pEnumerator->GetDevice(wId, &pDevice))) {
                    if (FAILED(pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice))) {
                        SafeReleaseWASAPI(&pEnumerator); Sleep(500); continue;
                    }
                }
            }

            if (FAILED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient))) {
                SafeReleaseWASAPI(&pDevice); SafeReleaseWASAPI(&pEnumerator); Sleep(500); continue;
            }

            if (FAILED(pAudioClient->GetMixFormat(&pwfx))) {
                SafeReleaseWASAPI(&pAudioClient); SafeReleaseWASAPI(&pDevice); SafeReleaseWASAPI(&pEnumerator); Sleep(500); continue;
            }

            if (FAILED(pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, pwfx, NULL))) {
                CoTaskMemFree(pwfx); SafeReleaseWASAPI(&pAudioClient); SafeReleaseWASAPI(&pDevice); SafeReleaseWASAPI(&pEnumerator); Sleep(500); continue;
            }

            if (FAILED(pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCaptureClient))) {
                CoTaskMemFree(pwfx); SafeReleaseWASAPI(&pAudioClient); SafeReleaseWASAPI(&pDevice); SafeReleaseWASAPI(&pEnumerator); Sleep(500); continue;
            }

            if (FAILED(pAudioClient->Start())) {
                CoTaskMemFree(pwfx); SafeReleaseWASAPI(&pCaptureClient); SafeReleaseWASAPI(&pAudioClient); SafeReleaseWASAPI(&pDevice); SafeReleaseWASAPI(&pEnumerator); Sleep(500); continue;
            }

            BYTE* pData;
            UINT32 numFramesAvailable;
            DWORD flags;

            while (m_running.load()) {
                Sleep(2);
                if (m_deviceChanged.load()) { m_deviceChanged.store(false); break; }
                if (FAILED(pCaptureClient->GetNextPacketSize(&numFramesAvailable))) break;

                while (numFramesAvailable > 0 && m_running.load()) {
                    if (SUCCEEDED(pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL)) && numFramesAvailable > 0) {
                        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                            m_peakL.store(0.0f);
                            m_peakR.store(0.0f);
                        } else if (pwfx && pwfx->nChannels >= 1) {
                            bool isFloat = (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
                            if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
                                WAVEFORMATEXTENSIBLE* pExt = (WAVEFORMATEXTENSIBLE*)pwfx;
                                if (pExt->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) isFloat = true;
                            }

                            static std::vector<float> samplesFloat;
                            samplesFloat.resize(numFramesAvailable * 2);

                            float maxL = 0.0f, maxR = 0.0f;
                            for (UINT32 i = 0; i < numFramesAvailable; ++i) {
                                const BYTE* pFrame = pData + (i * pwfx->nBlockAlign);
                                float sampleL = 0.0f, sampleR = 0.0f;

                                if (isFloat && pwfx->wBitsPerSample == 32) {
                                    const float* pF = (const float*)pFrame;
                                    sampleL = pF[0];
                                    sampleR = (pwfx->nChannels > 1) ? pF[1] : pF[0];
                                } else if (pwfx->wBitsPerSample == 16) {
                                    const int16_t* pI = (const int16_t*)pFrame;
                                    sampleL = pI[0] / 32768.0f;
                                    sampleR = (pwfx->nChannels > 1) ? (pI[1] / 32768.0f) : sampleL;
                                }

                                samplesFloat[i * 2] = sampleL;
                                samplesFloat[i * 2 + 1] = sampleR;

                                maxL = std::max(maxL, std::abs(sampleL));
                                maxR = std::max(maxR, std::abs(sampleR));
                            }

                            m_peakL.store(maxL);
                            m_peakR.store(maxR);

                            if (m_dataCallback) {
                                m_dataCallback(samplesFloat.data(), numFramesAvailable, 2, (int)pwfx->nSamplesPerSec);
                            }
                        }
                        pCaptureClient->ReleaseBuffer(numFramesAvailable);
                    }
                    if (FAILED(pCaptureClient->GetNextPacketSize(&numFramesAvailable))) break;
                }
            }

            pAudioClient->Stop();
            CoTaskMemFree(pwfx);
            SafeReleaseWASAPI(&pCaptureClient);
            SafeReleaseWASAPI(&pAudioClient);
            SafeReleaseWASAPI(&pDevice);
            SafeReleaseWASAPI(&pEnumerator);
        }
    }

    void InjectTestTone() override {
        std::thread([this]() {
            const int sampleRate = 48000;
            const float freq = 440.0f;
            std::vector<float> buffer(480 * 2);
            for (int p = 0; p < 10; ++p) {
                float maxL = 0.0f, maxR = 0.0f;
                for (size_t i = 0; i < 480; ++i) {
                    float t = (float)(p * 480 + i) / (float)sampleRate;
                    float env = std::sin(3.14159f * (float)(p * 480 + i) / 4800.0f);
                    float sample = std::sin(2.0f * 3.14159f * freq * t) * 0.70f * env;
                    buffer[i * 2] = sample;
                    buffer[i * 2 + 1] = sample;
                    maxL = (std::max)(maxL, std::abs(sample));
                    maxR = (std::max)(maxR, std::abs(sample));
                }
                m_peakL.store(maxL);
                m_peakR.store(maxR);
                if (m_dataCallback) {
                    m_dataCallback(buffer.data(), 480, 2, sampleRate);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }).detach();
    }

    std::atomic<bool> m_running;
    std::atomic<bool> m_deviceChanged{ false };
    std::atomic<int> m_selectedIdx;
    std::atomic<size_t> m_bufferSize;
    std::atomic<float> m_peakL;
    std::atomic<float> m_peakR;
    std::mutex m_mutex;
    std::thread m_workerThread;
    std::vector<AudioDeviceInfo> m_devices;
};

AudioBackend* CreateAudioBackend() {
    return new AudioWasapi();
}

#endif // _WIN32
