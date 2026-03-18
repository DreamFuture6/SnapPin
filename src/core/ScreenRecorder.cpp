
#include "core/ScreenRecorder.h"

#include "core/FfmpegExporter.h"
#include "core/Logger.h"
#include "core/MediaFoundationUtil.h"
#include "core/ScreenCaptureUtil.h"
#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT32 kOutputAudioSampleRate = 48000;
constexpr UINT32 kOutputAudioChannels = 2;
constexpr UINT32 kAudioChunkFrames = 960;
constexpr UINT32 kAudioBitsPerSample = 16;
constexpr UINT32 kAudioBytesPerFrame = (kOutputAudioChannels * kAudioBitsPerSample) / 8;
constexpr LONGLONG kOneSecond100ns = 10'000'000LL;

struct ScopedCoInit {
    ScopedCoInit() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ScopedCoInit() { if (SUCCEEDED(hr)) { CoUninitialize(); } }
    HRESULT hr = S_OK;
};

struct CoTaskMemDeleter {
    void operator()(WAVEFORMATEX* ptr) const { if (ptr) { CoTaskMemFree(ptr); } }
};

std::wstring HrToText(HRESULT hr)
{
    return MediaFoundationUtil::HResultToText(hr);
}

bool IsWaveFormatFloat(const WAVEFORMATEX& format)
{
    if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        return true;
    }
    if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && format.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(&format);
        return extensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }
    return false;
}

bool IsWaveFormatPcm(const WAVEFORMATEX& format)
{
    if (format.wFormatTag == WAVE_FORMAT_PCM) {
        return true;
    }
    if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && format.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(&format);
        return extensible->SubFormat == KSDATAFORMAT_SUBTYPE_PCM;
    }
    return false;
}

float ReadSampleAsFloat(const uint8_t* frameBytes, const WAVEFORMATEX& format, UINT32 channelIndex)
{
    const UINT32 channels = std::max<UINT32>(1, format.nChannels);
    const UINT32 bitsPerSample = std::max<UINT32>(8, format.wBitsPerSample);
    const UINT32 bytesPerSample = bitsPerSample / 8;
    const uint8_t* sample = frameBytes + std::min(channelIndex, channels - 1) * bytesPerSample;

    if (IsWaveFormatFloat(format) && bitsPerSample == 32) {
        return *reinterpret_cast<const float*>(sample);
    }

    if (IsWaveFormatPcm(format)) {
        switch (bitsPerSample) {
        case 16:
            return static_cast<float>(*reinterpret_cast<const int16_t*>(sample)) / 32768.0f;
        case 24: {
            int32_t value = (static_cast<int32_t>(sample[0]) |
                             (static_cast<int32_t>(sample[1]) << 8) |
                             (static_cast<int32_t>(sample[2]) << 16));
            if ((value & 0x800000) != 0) {
                value |= ~0xFFFFFF;
            }
            return static_cast<float>(value) / 8388608.0f;
        }
        case 32:
            return static_cast<float>(*reinterpret_cast<const int32_t*>(sample)) / 2147483648.0f;
        case 8:
            return (static_cast<float>(*sample) - 128.0f) / 128.0f;
        default:
            break;
        }
    }

    return 0.0f;
}

void ConvertPacketToStereoFloat(const BYTE* data, UINT32 frameCount, const WAVEFORMATEX& format, DWORD flags, std::vector<float>& outStereo)
{
    outStereo.clear();
    outStereo.resize(static_cast<size_t>(frameCount) * 2, 0.0f);
    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || !data) {
        return;
    }

    const UINT32 blockAlign = std::max<UINT32>(format.nBlockAlign, (format.nChannels * format.wBitsPerSample) / 8);
    for (UINT32 i = 0; i < frameCount; ++i) {
        const uint8_t* frameBytes = data + static_cast<size_t>(i) * blockAlign;
        float left = 0.0f;
        float right = 0.0f;
        if (format.nChannels == 1) {
            left = right = ReadSampleAsFloat(frameBytes, format, 0);
        } else {
            left = ReadSampleAsFloat(frameBytes, format, 0);
            right = ReadSampleAsFloat(frameBytes, format, 1);
        }
        outStereo[static_cast<size_t>(i) * 2] = std::clamp(left, -1.0f, 1.0f);
        outStereo[static_cast<size_t>(i) * 2 + 1] = std::clamp(right, -1.0f, 1.0f);
    }
}

uint32_t ClampBitrate(uint64_t bitrate, uint64_t minBitrate, uint64_t maxBitrate)
{
    return static_cast<uint32_t>(std::clamp<uint64_t>(bitrate, minBitrate, maxBitrate));
}

bool ConfigureAacOutputType(IMFMediaType* mediaType)
{
    if (!mediaType) {
        return false;
    }
    return SUCCEEDED(mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) &&
           SUCCEEDED(mediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC)) &&
           SUCCEEDED(mediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kAudioBitsPerSample)) &&
           SUCCEEDED(mediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kOutputAudioSampleRate)) &&
           SUCCEEDED(mediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kOutputAudioChannels)) &&
           SUCCEEDED(mediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000)) &&
           SUCCEEDED(mediaType->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0)) &&
           SUCCEEDED(mediaType->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29));
}

bool ConfigurePcmInputType(IMFMediaType* mediaType)
{
    if (!mediaType) {
        return false;
    }
    return SUCCEEDED(mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) &&
           SUCCEEDED(mediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) &&
           SUCCEEDED(mediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kAudioBitsPerSample)) &&
           SUCCEEDED(mediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kOutputAudioSampleRate)) &&
           SUCCEEDED(mediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kOutputAudioChannels)) &&
           SUCCEEDED(mediaType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, kAudioBytesPerFrame)) &&
           SUCCEEDED(mediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kOutputAudioSampleRate * kAudioBytesPerFrame)) &&
           SUCCEEDED(mediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
}

bool ConfigureVideoOutputType(IMFMediaType* mediaType, int width, int height, int fps, uint32_t bitrate)
{
    if (!mediaType) {
        return false;
    }
    return SUCCEEDED(mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) &&
           SUCCEEDED(mediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264)) &&
           SUCCEEDED(mediaType->SetUINT32(MF_MT_AVG_BITRATE, bitrate)) &&
           SUCCEEDED(mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) &&
           SUCCEEDED(MFSetAttributeSize(mediaType, MF_MT_FRAME_SIZE, static_cast<UINT32>(width), static_cast<UINT32>(height))) &&
           SUCCEEDED(MFSetAttributeRatio(mediaType, MF_MT_FRAME_RATE, static_cast<UINT32>(fps), 1)) &&
           SUCCEEDED(MFSetAttributeRatio(mediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
}

bool ConfigureVideoInputType(IMFMediaType* mediaType, int width, int height, int fps, const GUID& subtype)
{
    if (!mediaType) {
        return false;
    }
    if (FAILED(mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
        FAILED(mediaType->SetGUID(MF_MT_SUBTYPE, subtype)) ||
        FAILED(mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive)) ||
        FAILED(MFSetAttributeSize(mediaType, MF_MT_FRAME_SIZE, static_cast<UINT32>(width), static_cast<UINT32>(height))) ||
        FAILED(MFSetAttributeRatio(mediaType, MF_MT_FRAME_RATE, static_cast<UINT32>(fps), 1)) ||
        FAILED(MFSetAttributeRatio(mediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1))) {
        return false;
    }

    if ((subtype == MFVideoFormat_RGB32 || subtype == MFVideoFormat_ARGB32) &&
        FAILED(mediaType->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(width * 4)))) {
        return false;
    }
    return SUCCEEDED(mediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
}

bool ConfigureVideoInputType(IMFMediaType* mediaType, int width, int height, int fps)
{
    return ConfigureVideoInputType(mediaType, width, height, fps, MFVideoFormat_RGB32);
}

} // namespace

struct ScreenRecorder::WriterState {
    ComPtr<IMFSinkWriter> sinkWriter;
    DWORD videoStreamIndex = static_cast<DWORD>(-1);
    DWORD audioStreamIndex = static_cast<DWORD>(-1);
    bool hasAudio = false;
};

struct ScreenRecorder::AudioSourceState {
    enum class Kind {
        SystemLoopback,
        Microphone,
    };

    explicit AudioSourceState(Kind sourceKind)
        : kind(sourceKind)
    {
    }

    Kind kind;
    std::thread thread;
    std::mutex mutex;
    std::deque<float> stereoSamples;
    std::vector<float> pendingInput;
    double resampleCursor = 0.0;
    UINT32 sourceSampleRate = 0;
    std::wstring errorMessage;
    std::atomic<bool> finished{false};
};

ScreenRecorder::ScreenRecorder() = default;

ScreenRecorder::~ScreenRecorder()
{
    Cancel();
}

uint32_t ScreenRecorder::RecommendedVideoBitrate(int width, int height, int fps, VideoExportQuality quality)
{
    const uint64_t pixelsPerSecond = static_cast<uint64_t>(std::max(1, width)) *
                                     static_cast<uint64_t>(std::max(1, height)) *
                                     static_cast<uint64_t>(std::max(1, fps));
    switch (quality) {
    case VideoExportQuality::Light:
        // Light 模式仍需可读性，避免极低码率导致雪花/残影。
        return ClampBitrate(pixelsPerSecond / 90, 250'000ULL, 1'800'000ULL);
    case VideoExportQuality::Standard:
        return ClampBitrate(pixelsPerSecond / 45, 600'000ULL, 3'500'000ULL);
    case VideoExportQuality::Original:
    default:
        return ClampBitrate(pixelsPerSecond / 5, 2'500'000ULL, 120'000'000ULL);
    }
}

bool ScreenRecorder::Start(const ScreenRecordingOptions& options)
{
    if (running_.load(std::memory_order_acquire)) {
        return false;
    }
    if (!IsRectValid(options.screenRect) || options.outputPath.empty()) {
        lastErrorMessage_ = L"Invalid recording area or output path.";
        return false;
    }
    if (!MediaFoundationUtil::EnsureStartup()) {
        lastErrorMessage_ = L"Media Foundation startup failed.";
        return false;
    }

    options_ = options;
    captureRect_ = NormalizeRect(options.screenRect);
    captureWidth_ = RectWidth(captureRect_);
    captureHeight_ = RectHeight(captureRect_);
    if ((captureWidth_ & 1) != 0) {
        --captureWidth_;
        --captureRect_.right;
    }
    if ((captureHeight_ & 1) != 0) {
        --captureHeight_;
        --captureRect_.bottom;
    }
    if (captureWidth_ < 2 || captureHeight_ < 2) {
        lastErrorMessage_ = L"Recording area is too small.";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(options_.outputPath.parent_path(), ec);

    videoFramesWritten_.store(0, std::memory_order_release);
    audioFramesWritten_.store(0, std::memory_order_release);
    stopRequested_.store(false, std::memory_order_release);
    paused_.store(false, std::memory_order_release);
    lastErrorMessage_.clear();

    std::wstring errorMessage;
    if (!InitializeWriter(errorMessage)) {
        lastErrorMessage_ = errorMessage;
        ResetWriter();
        return false;
    }
    if (!InitializeAudioSources(errorMessage)) {
        lastErrorMessage_ = errorMessage;
        StopAudioSources();
        ResetWriter();
        return false;
    }

    running_.store(true, std::memory_order_release);
    videoThread_ = std::thread(&ScreenRecorder::VideoCaptureLoop, this);
    if (writer_ && writer_->hasAudio) {
        audioWriterThread_ = std::thread(&ScreenRecorder::AudioWriterLoop, this);
    }
    return true;
}

void ScreenRecorder::Pause(bool paused)
{
    paused_.store(paused, std::memory_order_release);
    audioBufferCv_.notify_all();
}

bool ScreenRecorder::Stop(ScreenRecordingResult& outResult)
{
    outResult = {};
    if (!writer_) {
        outResult.errorMessage = lastErrorMessage_;
        return false;
    }

    stopRequested_.store(true, std::memory_order_release);
    paused_.store(false, std::memory_order_release);
    audioBufferCv_.notify_all();

    if (videoThread_.joinable()) {
        videoThread_.join();
    }
    StopAudioSources();
    if (audioWriterThread_.joinable()) {
        audioWriterThread_.join();
    }

    bool finalizeOk = true;
    {
        std::scoped_lock lock(writerMutex_);
        if (writer_ && writer_->sinkWriter) {
            const HRESULT hr = writer_->sinkWriter->Finalize();
            finalizeOk = SUCCEEDED(hr);
            if (!finalizeOk) {
                lastErrorMessage_ = L"Finalize recording failed: " + HrToText(hr);
            }
        }
    }

    running_.store(false, std::memory_order_release);
    outResult.success = finalizeOk && lastErrorMessage_.empty();
    outResult.errorMessage = lastErrorMessage_;
    outResult.outputPath = options_.outputPath;
    outResult.width = captureWidth_;
    outResult.height = captureHeight_;
    outResult.duration100ns = Duration100ns();

    ResetWriter();
    return outResult.success;
}

void ScreenRecorder::Cancel()
{
    if (!writer_ && !running_.load(std::memory_order_acquire)) {
        return;
    }
    ScreenRecordingResult ignored;
    Stop(ignored);
    std::error_code ec;
    if (!options_.outputPath.empty()) {
        std::filesystem::remove(options_.outputPath, ec);
    }
}

LONGLONG ScreenRecorder::Duration100ns() const
{
    const LONGLONG videoDuration = (videoFramesWritten_.load(std::memory_order_acquire) * kOneSecond100ns) /
                                   std::max(1, options_.fps);
    const LONGLONG audioDuration = (audioFramesWritten_.load(std::memory_order_acquire) * kOneSecond100ns) /
                                   static_cast<LONGLONG>(kOutputAudioSampleRate);
    return std::max(videoDuration, audioDuration);
}

bool ScreenRecorder::InitializeWriter(std::wstring& errorMessage)
{
    writer_ = std::make_unique<WriterState>();

    ComPtr<IMFAttributes> attributes;
    MFCreateAttributes(&attributes, 2);
    if (attributes) {
        attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
    }

    const HRESULT createHr = MFCreateSinkWriterFromURL(options_.outputPath.c_str(), nullptr, attributes.Get(), &writer_->sinkWriter);
    if (FAILED(createHr) || !writer_->sinkWriter) {
        errorMessage = L"Create sink writer failed: " + HrToText(createHr);
        return false;
    }

    const uint32_t videoBitrate = options_.videoBitrate != 0
        ? options_.videoBitrate
        : RecommendedVideoBitrate(captureWidth_, captureHeight_, std::max(5, options_.fps), VideoExportQuality::Original);

    ComPtr<IMFMediaType> videoOutType;
    ComPtr<IMFMediaType> videoInType;
    MFCreateMediaType(&videoOutType);
    MFCreateMediaType(&videoInType);
    if (!ConfigureVideoOutputType(videoOutType.Get(), captureWidth_, captureHeight_, std::max(5, options_.fps), videoBitrate) ||
        !ConfigureVideoInputType(videoInType.Get(), captureWidth_, captureHeight_, std::max(5, options_.fps))) {
        errorMessage = L"Configure video media types failed.";
        return false;
    }

    HRESULT hr = writer_->sinkWriter->AddStream(videoOutType.Get(), &writer_->videoStreamIndex);
    if (FAILED(hr)) {
        errorMessage = L"Add video stream failed: " + HrToText(hr);
        return false;
    }
    hr = writer_->sinkWriter->SetInputMediaType(writer_->videoStreamIndex, videoInType.Get(), nullptr);
    if (FAILED(hr)) {
        errorMessage = L"Set video input type failed: " + HrToText(hr);
        return false;
    }

    if (options_.recordSystemAudio || options_.recordMicrophoneAudio) {
        ComPtr<IMFMediaType> audioOutType;
        ComPtr<IMFMediaType> audioInType;
        MFCreateMediaType(&audioOutType);
        MFCreateMediaType(&audioInType);
        if (!ConfigureAacOutputType(audioOutType.Get()) || !ConfigurePcmInputType(audioInType.Get())) {
            errorMessage = L"Configure audio media types failed.";
            return false;
        }

        hr = writer_->sinkWriter->AddStream(audioOutType.Get(), &writer_->audioStreamIndex);
        if (FAILED(hr)) {
            errorMessage = L"Add audio stream failed: " + HrToText(hr);
            return false;
        }
        hr = writer_->sinkWriter->SetInputMediaType(writer_->audioStreamIndex, audioInType.Get(), nullptr);
        if (FAILED(hr)) {
            errorMessage = L"Set audio input type failed: " + HrToText(hr);
            return false;
        }
        writer_->hasAudio = true;
    }

    hr = writer_->sinkWriter->BeginWriting();
    if (FAILED(hr)) {
        errorMessage = L"BeginWriting failed: " + HrToText(hr);
        return false;
    }
    return true;
}

void ScreenRecorder::ResetWriter()
{
    writer_.reset();
    audioSources_.clear();
    options_ = {};
    captureRect_ = RECT{};
    captureWidth_ = 0;
    captureHeight_ = 0;
}

bool ScreenRecorder::InitializeAudioSources(std::wstring& errorMessage)
{
    audioSources_.clear();
    if (!(options_.recordSystemAudio || options_.recordMicrophoneAudio)) {
        return true;
    }

    if (options_.recordSystemAudio) {
        audioSources_.push_back(std::make_unique<AudioSourceState>(AudioSourceState::Kind::SystemLoopback));
    }
    if (options_.recordMicrophoneAudio) {
        audioSources_.push_back(std::make_unique<AudioSourceState>(AudioSourceState::Kind::Microphone));
    }

    for (auto& source : audioSources_) {
        source->thread = std::thread(&ScreenRecorder::AudioCaptureLoop, this, source.get());
    }

    errorMessage.clear();
    return true;
}

void ScreenRecorder::StopAudioSources()
{
    for (auto& source : audioSources_) {
        if (source && source->thread.joinable()) {
            source->thread.join();
        }
    }
}

void ScreenRecorder::VideoCaptureLoop()
{
    ScopedCoInit coInit;
    if (FAILED(coInit.hr) && coInit.hr != RPC_E_CHANGED_MODE) {
        lastErrorMessage_ = L"Video thread COM initialization failed.";
        return;
    }

    const int fps = std::max(5, options_.fps);
    const auto frameInterval = std::chrono::microseconds(1'000'000 / fps);
    auto nextTick = std::chrono::steady_clock::now();
    Image lastFrame;

    while (!stopRequested_.load(std::memory_order_acquire)) {
        if (paused_.load(std::memory_order_acquire)) {
            nextTick = std::chrono::steady_clock::now() + frameInterval;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now < nextTick) {
            std::this_thread::sleep_for(nextTick - now);
        }
        nextTick += frameInterval;

        Image frame;
        if (ScreenCaptureUtil::CaptureScreenRect(captureRect_, frame, false, true)) {
            lastFrame = frame;
        } else if (!lastFrame.IsValid()) {
            continue;
        } else {
            frame = lastFrame;
        }

        if (frame.width != captureWidth_ || frame.height != captureHeight_) {
            frame = frame.Crop(RECT{0, 0, captureWidth_, captureHeight_});
        }
        if (!frame.IsValid()) {
            continue;
        }

        const LONGLONG index = videoFramesWritten_.load(std::memory_order_acquire);
        const LONGLONG timestamp = (index * kOneSecond100ns) / fps;
        const LONGLONG duration = kOneSecond100ns / fps;
        if (WriteVideoFrame(frame.bgra.data(), frame.width, frame.height, timestamp, duration)) {
            videoFramesWritten_.fetch_add(1, std::memory_order_acq_rel);
        }
    }
}

void ScreenRecorder::AudioWriterLoop()
{
    ScopedCoInit coInit;
    if (FAILED(coInit.hr) && coInit.hr != RPC_E_CHANGED_MODE) {
        lastErrorMessage_ = L"Audio writer thread COM initialization failed.";
        return;
    }

    const auto chunkDuration = std::chrono::milliseconds(20);
    auto nextTick = std::chrono::steady_clock::now();
    std::vector<float> mixBuffer(static_cast<size_t>(kAudioChunkFrames) * 2, 0.0f);
    std::vector<int16_t> pcmBuffer(static_cast<size_t>(kAudioChunkFrames) * 2, 0);

    while (!stopRequested_.load(std::memory_order_acquire) || std::any_of(audioSources_.begin(), audioSources_.end(), [](const auto& source) {
               std::scoped_lock lock(source->mutex);
               return !source->stereoSamples.empty();
           })) {
        if (paused_.load(std::memory_order_acquire)) {
            nextTick = std::chrono::steady_clock::now() + chunkDuration;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now < nextTick) {
            std::this_thread::sleep_for(nextTick - now);
        }
        nextTick += chunkDuration;

        std::fill(mixBuffer.begin(), mixBuffer.end(), 0.0f);
        bool hasAnyAudio = false;
        for (const auto& source : audioSources_) {
            std::scoped_lock lock(source->mutex);
            const size_t availableFrames = source->stereoSamples.size() / 2;
            for (size_t i = 0; i < static_cast<size_t>(kAudioChunkFrames); ++i) {
                const float left = (i < availableFrames) ? source->stereoSamples[i * 2] : 0.0f;
                const float right = (i < availableFrames) ? source->stereoSamples[i * 2 + 1] : 0.0f;
                mixBuffer[i * 2] += left;
                mixBuffer[i * 2 + 1] += right;
                hasAnyAudio = hasAnyAudio || std::abs(left) > 1e-4f || std::abs(right) > 1e-4f;
            }
            const size_t popCount = std::min(source->stereoSamples.size(), static_cast<size_t>(kAudioChunkFrames) * 2);
            for (size_t i = 0; i < popCount; ++i) {
                source->stereoSamples.pop_front();
            }
        }

        for (size_t i = 0; i < mixBuffer.size(); ++i) {
            const float sample = std::clamp(mixBuffer[i], -1.0f, 1.0f);
            pcmBuffer[i] = static_cast<int16_t>(std::lround(sample * 32767.0f));
        }

        const LONGLONG writtenFrames = audioFramesWritten_.load(std::memory_order_acquire);
        const LONGLONG timestamp = (writtenFrames * kOneSecond100ns) / kOutputAudioSampleRate;
        const LONGLONG duration = (static_cast<LONGLONG>(kAudioChunkFrames) * kOneSecond100ns) / kOutputAudioSampleRate;
        if (WriteAudioChunk(pcmBuffer.data(), kAudioChunkFrames, timestamp, duration)) {
            audioFramesWritten_.fetch_add(kAudioChunkFrames, std::memory_order_acq_rel);
        }

        if (!hasAnyAudio && stopRequested_.load(std::memory_order_acquire)) {
            const bool allFinished = std::all_of(audioSources_.begin(), audioSources_.end(), [](const auto& source) {
                return source->finished.load(std::memory_order_acquire);
            });
            if (allFinished) {
                break;
            }
        }
    }
}

void ScreenRecorder::AudioCaptureLoop(AudioSourceState* state)
{
    if (!state) {
        return;
    }

    ScopedCoInit coInit;
    if (FAILED(coInit.hr) && coInit.hr != RPC_E_CHANGED_MODE) {
        state->errorMessage = L"Audio capture thread COM initialization failed.";
        state->finished.store(true, std::memory_order_release);
        return;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr) || !enumerator) {
        state->errorMessage = L"Create MMDeviceEnumerator failed: " + HrToText(hr);
        state->finished.store(true, std::memory_order_release);
        return;
    }

    ComPtr<IMMDevice> device;
    if (state->kind == AudioSourceState::Kind::SystemLoopback) {
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    } else {
        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
    }
    if (FAILED(hr) || !device) {
        state->errorMessage = L"Get default audio endpoint failed: " + HrToText(hr);
        state->finished.store(true, std::memory_order_release);
        return;
    }

    ComPtr<IAudioClient> audioClient;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(audioClient.GetAddressOf()));
    if (FAILED(hr) || !audioClient) {
        state->errorMessage = L"Activate audio client failed: " + HrToText(hr);
        state->finished.store(true, std::memory_order_release);
        return;
    }

    std::unique_ptr<WAVEFORMATEX, CoTaskMemDeleter> format;
    WAVEFORMATEX* rawFormat = nullptr;
    hr = audioClient->GetMixFormat(&rawFormat);
    format.reset(rawFormat);
    if (FAILED(hr) || !format) {
        state->errorMessage = L"Get mix format failed: " + HrToText(hr);
        state->finished.store(true, std::memory_order_release);
        return;
    }
    state->sourceSampleRate = std::max<UINT32>(1, format->nSamplesPerSec);

    DWORD streamFlags = AUDCLNT_STREAMFLAGS_NOPERSIST;
    if (state->kind == AudioSourceState::Kind::SystemLoopback) {
        streamFlags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    }

    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, 0, 0, format.get(), nullptr);
    if (FAILED(hr)) {
        state->errorMessage = L"Initialize audio capture failed: " + HrToText(hr);
        state->finished.store(true, std::memory_order_release);
        return;
    }

    ComPtr<IAudioCaptureClient> captureClient;
    hr = audioClient->GetService(IID_PPV_ARGS(&captureClient));
    if (FAILED(hr) || !captureClient) {
        state->errorMessage = L"Get audio capture service failed: " + HrToText(hr);
        state->finished.store(true, std::memory_order_release);
        return;
    }

    hr = audioClient->Start();
    if (FAILED(hr)) {
        state->errorMessage = L"Start audio capture failed: " + HrToText(hr);
        state->finished.store(true, std::memory_order_release);
        return;
    }

    std::vector<float> stereoFrames;
    while (!stopRequested_.load(std::memory_order_acquire)) {
        if (paused_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        UINT32 packetLength = 0;
        hr = captureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) {
            state->errorMessage = L"Get next audio packet failed: " + HrToText(hr);
            break;
        }
        if (packetLength == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        while (packetLength != 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) {
                state->errorMessage = L"Get audio buffer failed: " + HrToText(hr);
                packetLength = 0;
                break;
            }

            ConvertPacketToStereoFloat(data, frames, *format, flags, stereoFrames);
            captureClient->ReleaseBuffer(frames);

            {
                std::scoped_lock lock(state->mutex);
                const double step = static_cast<double>(state->sourceSampleRate) / static_cast<double>(kOutputAudioSampleRate);
                state->pendingInput.insert(state->pendingInput.end(), stereoFrames.begin(), stereoFrames.end());
                while ((state->pendingInput.size() / 2) >= 2 && state->resampleCursor + 1.0 < static_cast<double>(state->pendingInput.size() / 2)) {
                    const size_t idx = static_cast<size_t>(std::floor(state->resampleCursor));
                    const double frac = state->resampleCursor - static_cast<double>(idx);
                    const float l0 = state->pendingInput[idx * 2];
                    const float r0 = state->pendingInput[idx * 2 + 1];
                    const float l1 = state->pendingInput[(idx + 1) * 2];
                    const float r1 = state->pendingInput[(idx + 1) * 2 + 1];
                    state->stereoSamples.push_back(static_cast<float>(l0 + (l1 - l0) * frac));
                    state->stereoSamples.push_back(static_cast<float>(r0 + (r1 - r0) * frac));
                    state->resampleCursor += step;
                }

                const size_t keepFrameIndex = (state->resampleCursor >= 1.0)
                    ? (static_cast<size_t>(std::floor(state->resampleCursor)) - 1)
                    : 0;
                if (keepFrameIndex > 0) {
                    state->pendingInput.erase(state->pendingInput.begin(), state->pendingInput.begin() + static_cast<std::ptrdiff_t>(keepFrameIndex * 2));
                    state->resampleCursor -= static_cast<double>(keepFrameIndex);
                }
            }
            audioBufferCv_.notify_all();

            hr = captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) {
                state->errorMessage = L"Advance audio packet failed: " + HrToText(hr);
                packetLength = 0;
                break;
            }
        }
    }

    audioClient->Stop();
    state->finished.store(true, std::memory_order_release);
    audioBufferCv_.notify_all();
    if (!state->errorMessage.empty() && lastErrorMessage_.empty()) {
        lastErrorMessage_ = state->errorMessage;
    }
}

bool ScreenRecorder::WriteVideoFrame(const uint8_t* bgraData, int width, int height, LONGLONG timestamp, LONGLONG duration)
{
    if (!bgraData || !writer_ || !writer_->sinkWriter) {
        return false;
    }

    const DWORD bufferSize = static_cast<DWORD>(width * height * 4);
    ComPtr<IMFMediaBuffer> mediaBuffer;
    HRESULT hr = MFCreateMemoryBuffer(bufferSize, &mediaBuffer);
    if (FAILED(hr) || !mediaBuffer) {
        lastErrorMessage_ = L"Create video buffer failed: " + HrToText(hr);
        return false;
    }

    BYTE* dst = nullptr;
    hr = mediaBuffer->Lock(&dst, nullptr, nullptr);
    if (FAILED(hr) || !dst) {
        lastErrorMessage_ = L"Lock video buffer failed: " + HrToText(hr);
        return false;
    }
    memcpy(dst, bgraData, bufferSize);
    mediaBuffer->Unlock();
    mediaBuffer->SetCurrentLength(bufferSize);

    ComPtr<IMFSample> sample;
    hr = MFCreateSample(&sample);
    if (FAILED(hr) || !sample) {
        lastErrorMessage_ = L"Create video sample failed: " + HrToText(hr);
        return false;
    }
    sample->AddBuffer(mediaBuffer.Get());
    sample->SetSampleTime(timestamp);
    sample->SetSampleDuration(duration);

    std::scoped_lock lock(writerMutex_);
    hr = writer_->sinkWriter->WriteSample(writer_->videoStreamIndex, sample.Get());
    if (FAILED(hr)) {
        lastErrorMessage_ = L"Write video sample failed: " + HrToText(hr);
        return false;
    }
    return true;
}

bool ScreenRecorder::WriteAudioChunk(const int16_t* pcmData, UINT32 frameCount, LONGLONG timestamp, LONGLONG duration)
{
    if (!pcmData || !writer_ || !writer_->sinkWriter || !writer_->hasAudio) {
        return false;
    }

    const DWORD bufferSize = frameCount * kAudioBytesPerFrame;
    ComPtr<IMFMediaBuffer> mediaBuffer;
    HRESULT hr = MFCreateMemoryBuffer(bufferSize, &mediaBuffer);
    if (FAILED(hr) || !mediaBuffer) {
        lastErrorMessage_ = L"Create audio buffer failed: " + HrToText(hr);
        return false;
    }

    BYTE* dst = nullptr;
    hr = mediaBuffer->Lock(&dst, nullptr, nullptr);
    if (FAILED(hr) || !dst) {
        lastErrorMessage_ = L"Lock audio buffer failed: " + HrToText(hr);
        return false;
    }
    memcpy(dst, pcmData, bufferSize);
    mediaBuffer->Unlock();
    mediaBuffer->SetCurrentLength(bufferSize);

    ComPtr<IMFSample> sample;
    hr = MFCreateSample(&sample);
    if (FAILED(hr) || !sample) {
        lastErrorMessage_ = L"Create audio sample failed: " + HrToText(hr);
        return false;
    }
    sample->AddBuffer(mediaBuffer.Get());
    sample->SetSampleTime(timestamp);
    sample->SetSampleDuration(duration);

    std::scoped_lock lock(writerMutex_);
    hr = writer_->sinkWriter->WriteSample(writer_->audioStreamIndex, sample.Get());
    if (FAILED(hr)) {
        lastErrorMessage_ = L"Write audio sample failed: " + HrToText(hr);
        return false;
    }
    return true;
}

bool ScreenRecorder::ExportRecording(const std::filesystem::path& inputPath,
                                     const std::filesystem::path& outputPath,
                                     VideoExportQuality quality,
                                     std::wstring& errorMessage)
{
    if (quality == VideoExportQuality::Original) {
        std::error_code ec;
        std::filesystem::create_directories(outputPath.parent_path(), ec);
        std::filesystem::copy_file(inputPath, outputPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            errorMessage = L"Copy recording file failed.";
            return false;
        }
        return true;
    }

    uint32_t bitrate = RecommendedVideoBitrate(1920, 1080, 24, quality);
    FfmpegExporter::VideoInfo videoInfo{};
    std::wstring probeError;
    if (FfmpegExporter::ProbeRecordingVideoInfo(inputPath, videoInfo, probeError)) {
        bitrate = RecommendedVideoBitrate(videoInfo.width, videoInfo.height, videoInfo.fps, quality);
    }

    const auto profile = (quality == VideoExportQuality::Light)
        ? FfmpegExporter::CompressionProfile::Light
        : FfmpegExporter::CompressionProfile::Standard;

    if (!FfmpegExporter::ExportRecording(inputPath, outputPath, bitrate, profile, errorMessage)) {
        std::error_code ec;
        std::filesystem::remove(outputPath, ec);
        if (errorMessage.empty()) {
            errorMessage = L"FFmpeg 导出失败。";
        }
        return false;
    }
    return true;
}


