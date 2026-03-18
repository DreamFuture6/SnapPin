#pragma once

#include "common.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

struct ScreenRecordingOptions {
    RECT screenRect{};
    int fps = 24;
    bool recordSystemAudio = true;
    bool recordMicrophoneAudio = false;
    uint32_t videoBitrate = 0;
    std::filesystem::path outputPath;
};

enum class VideoExportQuality {
    Light,
    Standard,
    Original,
};

struct ScreenRecordingResult {
    bool success = false;
    std::wstring errorMessage;
    std::filesystem::path outputPath;
    int width = 0;
    int height = 0;
    LONGLONG duration100ns = 0;
};

class ScreenRecorder {
public:
    ScreenRecorder();
    ~ScreenRecorder();

    bool Start(const ScreenRecordingOptions& options);
    void Pause(bool paused);
    bool Stop(ScreenRecordingResult& outResult);
    void Cancel();

    bool IsRunning() const { return running_.load(std::memory_order_acquire); }
    bool IsPaused() const { return paused_.load(std::memory_order_acquire); }
    LONGLONG Duration100ns() const;
    const std::filesystem::path& OutputPath() const { return options_.outputPath; }
    RECT CaptureRect() const { return captureRect_; }

    static uint32_t RecommendedVideoBitrate(int width, int height, int fps, VideoExportQuality quality);
    static bool ExportRecording(const std::filesystem::path& inputPath,
                                const std::filesystem::path& outputPath,
                                VideoExportQuality quality,
                                std::wstring& errorMessage);

private:
    struct AudioSourceState;

    bool InitializeWriter(std::wstring& errorMessage);
    void ResetWriter();
    void VideoCaptureLoop();
    void AudioWriterLoop();
    void AudioCaptureLoop(AudioSourceState* state);
    bool InitializeAudioSources(std::wstring& errorMessage);
    void StopAudioSources();
    bool WriteVideoFrame(const uint8_t* bgraData, int width, int height, LONGLONG timestamp, LONGLONG duration);
    bool WriteAudioChunk(const int16_t* pcmData, UINT32 frameCount, LONGLONG timestamp, LONGLONG duration);

    ScreenRecordingOptions options_{};
    RECT captureRect_{};
    int captureWidth_ = 0;
    int captureHeight_ = 0;

    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<LONGLONG> videoFramesWritten_{0};
    std::atomic<LONGLONG> audioFramesWritten_{0};

    std::thread videoThread_;
    std::thread audioWriterThread_;

    mutable std::mutex stateMutex_;
    mutable std::mutex writerMutex_;
    std::condition_variable audioBufferCv_;

    struct WriterState;
    std::unique_ptr<WriterState> writer_;
    std::vector<std::unique_ptr<AudioSourceState>> audioSources_;

    std::wstring lastErrorMessage_;
};
