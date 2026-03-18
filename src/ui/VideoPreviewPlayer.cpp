#include "ui/VideoPreviewPlayer.h"

#include "core/MediaFoundationUtil.h"

#include <propvarutil.h>

namespace {

std::wstring HrToText(HRESULT hr)
{
    return MediaFoundationUtil::HResultToText(hr);
}

bool ReadPropVariantInt64(const PROPVARIANT& value, LONGLONG& out)
{
    switch (value.vt) {
    case VT_I8:
        out = value.hVal.QuadPart;
        return true;
    case VT_UI8:
        out = static_cast<LONGLONG>(value.uhVal.QuadPart);
        return true;
    case VT_I4:
        out = static_cast<LONGLONG>(value.lVal);
        return true;
    case VT_UI4:
        out = static_cast<LONGLONG>(value.ulVal);
        return true;
    default:
        out = 0;
        return false;
    }
}

} // namespace

VideoPreviewPlayer::VideoPreviewPlayer(HWND ownerHwnd)
    : ownerHwnd_(ownerHwnd)
{
}

VideoPreviewPlayer::~VideoPreviewPlayer()
{
    Close();
}

bool VideoPreviewPlayer::Open(HWND videoHost, const std::filesystem::path& filePath, std::wstring& errorMessage)
{
    Close();
    if (!MediaFoundationUtil::EnsureStartup()) {
        errorMessage = L"Media Foundation 初始化失败。";
        return false;
    }

    videoHost_ = videoHost;
    HRESULT hr = MFPCreateMediaPlayer(filePath.c_str(), FALSE, MFP_OPTION_FREE_THREADED_CALLBACK, this, videoHost_, &player_);
    if (FAILED(hr) || !player_) {
        errorMessage = L"创建预览播放器失败: " + HrToText(hr);
        return false;
    }
    ready_.store(false, std::memory_order_release);
    playing_.store(false, std::memory_order_release);
    rate_.store(1.0f, std::memory_order_release);
    lastKnownPosition100ns_.store(0, std::memory_order_release);
    lastKnownDuration100ns_.store(0, std::memory_order_release);
    return true;
}

void VideoPreviewPlayer::Close()
{
    ready_.store(false, std::memory_order_release);
    playing_.store(false, std::memory_order_release);
    lastKnownPosition100ns_.store(0, std::memory_order_release);
    lastKnownDuration100ns_.store(0, std::memory_order_release);
    if (player_) {
        player_->Shutdown();
        player_->Release();
        player_ = nullptr;
    }
}

void VideoPreviewPlayer::Play()
{
    if (!player_ || !ready_.load(std::memory_order_acquire)) {
        return;
    }
    player_->Play();
}

void VideoPreviewPlayer::Pause()
{
    if (!player_ || !ready_.load(std::memory_order_acquire)) {
        return;
    }
    player_->Pause();
}

void VideoPreviewPlayer::TogglePlayPause()
{
    if (playing_.load(std::memory_order_acquire)) {
        Pause();
    } else {
        Play();
    }
}

void VideoPreviewPlayer::SetRate(float rate)
{
    if (!player_ || !ready_.load(std::memory_order_acquire)) {
        return;
    }
    if (SUCCEEDED(player_->SetRate(rate))) {
        rate_.store(rate, std::memory_order_release);
    }
}

float VideoPreviewPlayer::Rate() const
{
    return rate_.load(std::memory_order_acquire);
}

void VideoPreviewPlayer::SeekToTime(LONGLONG position100ns)
{
    if (!player_ || !ready_.load(std::memory_order_acquire)) {
        return;
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    InitPropVariantFromInt64(position100ns, &value);
    if (SUCCEEDED(player_->SetPosition(MFP_POSITIONTYPE_100NS, &value))) {
        lastKnownPosition100ns_.store(std::max<LONGLONG>(0, position100ns), std::memory_order_release);
    }
    PropVariantClear(&value);
}

void VideoPreviewPlayer::SeekPreviewFrame(LONGLONG position100ns)
{
    if (!player_ || !ready_.load(std::memory_order_acquire)) {
        return;
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    InitPropVariantFromInt64(position100ns, &value);
    if (SUCCEEDED(player_->SetPosition(MFP_POSITIONTYPE_100NS, &value))) {
        lastKnownPosition100ns_.store(std::max<LONGLONG>(0, position100ns), std::memory_order_release);
        player_->UpdateVideo();
    }
    PropVariantClear(&value);
}

void VideoPreviewPlayer::SeekToRatio(double ratio)
{
    if (!player_ || !ready_.load(std::memory_order_acquire)) {
        return;
    }
    ratio = std::clamp(ratio, 0.0, 1.0);
    const LONGLONG duration = Duration100ns();
    SeekToTime(static_cast<LONGLONG>(std::llround(duration * ratio)));
}

void VideoPreviewPlayer::PrimeFirstFrame(LONGLONG position100ns)
{
    if (!player_ || !ready_.load(std::memory_order_acquire)) {
        return;
    }
    SeekToTime(position100ns);
    player_->Play();
    player_->Pause();
    SeekToTime(position100ns);
    playing_.store(false, std::memory_order_release);
}

LONGLONG VideoPreviewPlayer::Duration100ns() const
{
    if (!player_ || !ready_.load(std::memory_order_acquire)) {
        return lastKnownDuration100ns_.load(std::memory_order_acquire);
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    if (FAILED(player_->GetDuration(MFP_POSITIONTYPE_100NS, &value))) {
        return lastKnownDuration100ns_.load(std::memory_order_acquire);
    }
    LONGLONG duration = 0;
    if (ReadPropVariantInt64(value, duration)) {
        lastKnownDuration100ns_.store(std::max<LONGLONG>(0, duration), std::memory_order_release);
    }
    PropVariantClear(&value);
    return lastKnownDuration100ns_.load(std::memory_order_acquire);
}

LONGLONG VideoPreviewPlayer::Position100ns() const
{
    if (!player_ || !ready_.load(std::memory_order_acquire)) {
        return lastKnownPosition100ns_.load(std::memory_order_acquire);
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    if (FAILED(player_->GetPosition(MFP_POSITIONTYPE_100NS, &value))) {
        return lastKnownPosition100ns_.load(std::memory_order_acquire);
    }
    LONGLONG position = 0;
    if (ReadPropVariantInt64(value, position)) {
        lastKnownPosition100ns_.store(std::max<LONGLONG>(0, position), std::memory_order_release);
    }
    PropVariantClear(&value);
    return lastKnownPosition100ns_.load(std::memory_order_acquire);
}

STDMETHODIMP VideoPreviewPlayer::QueryInterface(REFIID riid, void** ppvObject)
{
    if (!ppvObject) {
        return E_POINTER;
    }
    if (riid == __uuidof(IMFPMediaPlayerCallback) || riid == __uuidof(IUnknown)) {
        *ppvObject = static_cast<IMFPMediaPlayerCallback*>(this);
        AddRef();
        return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) VideoPreviewPlayer::AddRef()
{
    return refCount_.fetch_add(1, std::memory_order_acq_rel) + 1;
}

STDMETHODIMP_(ULONG) VideoPreviewPlayer::Release()
{
    const ULONG value = refCount_.fetch_sub(1, std::memory_order_acq_rel) - 1;
    return value;
}

void STDMETHODCALLTYPE VideoPreviewPlayer::OnMediaPlayerEvent(MFP_EVENT_HEADER* eventHeader)
{
    if (!eventHeader) {
        return;
    }

    switch (eventHeader->eEventType) {
    case MFP_EVENT_TYPE_MEDIAITEM_SET:
        ready_.store(true, std::memory_order_release);
        lastKnownPosition100ns_.store(0, std::memory_order_release);
        if (ownerHwnd_) {
            PostMessageW(ownerHwnd_, WM_APP + 104, EventReady, 0);
        }
        break;
    case MFP_EVENT_TYPE_PLAY:
        playing_.store(true, std::memory_order_release);
        break;
    case MFP_EVENT_TYPE_PAUSE:
    case MFP_EVENT_TYPE_STOP:
        playing_.store(false, std::memory_order_release);
        break;
    case MFP_EVENT_TYPE_PLAYBACK_ENDED:
        playing_.store(false, std::memory_order_release);
        lastKnownPosition100ns_.store(lastKnownDuration100ns_.load(std::memory_order_acquire), std::memory_order_release);
        if (ownerHwnd_) {
            PostMessageW(ownerHwnd_, WM_APP + 104, EventEnded, 0);
        }
        break;
    case MFP_EVENT_TYPE_ERROR:
        playing_.store(false, std::memory_order_release);
        if (ownerHwnd_) {
            PostMessageW(ownerHwnd_, WM_APP + 104, EventError, eventHeader->hrEvent);
        }
        break;
    default:
        break;
    }
}
