#pragma once

#include "common.h"

#include <mfplay.h>

class VideoPreviewPlayer : public IMFPMediaPlayerCallback {
public:
    enum : WPARAM {
        EventReady = 1,
        EventEnded = 2,
        EventError = 3,
    };

    explicit VideoPreviewPlayer(HWND ownerHwnd);
    ~VideoPreviewPlayer();

    bool Open(HWND videoHost, const std::filesystem::path& filePath, std::wstring& errorMessage);
    void Close();
    void Play();
    void Pause();
    void TogglePlayPause();
    void SetRate(float rate);
    float Rate() const;
    void SeekToTime(LONGLONG position100ns);
    void SeekPreviewFrame(LONGLONG position100ns);
    void SeekToRatio(double ratio);
    void PrimeFirstFrame(LONGLONG position100ns = 0);
    LONGLONG Duration100ns() const;
    LONGLONG Position100ns() const;
    bool IsPlaying() const { return playing_; }
    bool IsReady() const { return ready_; }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* eventHeader) override;

private:
    HWND ownerHwnd_ = nullptr;
    HWND videoHost_ = nullptr;
    IMFPMediaPlayer* player_ = nullptr;
    std::atomic<ULONG> refCount_{1};
    std::atomic<bool> ready_{false};
    std::atomic<bool> playing_{false};
    std::atomic<float> rate_{1.0f};
    mutable std::atomic<LONGLONG> lastKnownPosition100ns_{0};
    mutable std::atomic<LONGLONG> lastKnownDuration100ns_{0};
};
