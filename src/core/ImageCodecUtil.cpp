#include "core/ImageCodecUtil.h"

namespace {

class ScopedComStream {
public:
    ScopedComStream() = default;
    ~ScopedComStream() {
        if (stream_) {
            stream_->Release();
        }
    }

    ScopedComStream(const ScopedComStream&) = delete;
    ScopedComStream& operator=(const ScopedComStream&) = delete;

    IStream** Receive() {
        if (stream_) {
            stream_->Release();
            stream_ = nullptr;
        }
        return &stream_;
    }

    IStream* Get() const { return stream_; }

private:
    IStream* stream_ = nullptr;
};

class ScopedGlobalLock {
public:
    explicit ScopedGlobalLock(HGLOBAL handle)
        : handle_(handle),
          data_(handle ? GlobalLock(handle) : nullptr) {
    }

    ~ScopedGlobalLock() {
        if (handle_ && data_) {
            GlobalUnlock(handle_);
        }
    }

    ScopedGlobalLock(const ScopedGlobalLock&) = delete;
    ScopedGlobalLock& operator=(const ScopedGlobalLock&) = delete;

    void* Get() const { return data_; }

private:
    HGLOBAL handle_ = nullptr;
    void* data_ = nullptr;
};

struct GdiplusState {
    std::once_flag once;
    bool initialized = false;
    ULONG_PTR token = 0;

    ~GdiplusState() {
        if (token != 0) {
            Gdiplus::GdiplusShutdown(token);
        }
    }
};

GdiplusState& GetGdiplusState() {
    static GdiplusState state;
    return state;
}

bool CopyImagePixelsToBitmap(const Image& image, Gdiplus::Bitmap& bitmap) {
    Gdiplus::Rect rect(0, 0, image.width, image.height);
    Gdiplus::BitmapData data{};
    if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return false;
    }

    for (int y = 0; y < image.height; ++y) {
        auto* dst = static_cast<uint8_t*>(data.Scan0) + static_cast<size_t>(data.Stride) * static_cast<size_t>(y);
        const auto* src = image.bgra.data() + static_cast<size_t>(image.width) * static_cast<size_t>(y) * 4;
        memcpy(dst, src, static_cast<size_t>(image.width) * 4);
    }

    bitmap.UnlockBits(&data);
    return true;
}

} // namespace

namespace ImageCodecUtil {

bool EnsureGdiplus() {
    auto& state = GetGdiplusState();
    std::call_once(state.once, [&state]() {
        Gdiplus::GdiplusStartupInput input;
        state.initialized = Gdiplus::GdiplusStartup(&state.token, &input, nullptr) == Gdiplus::Ok;
    });
    return state.initialized;
}

bool FindEncoderClsid(const WCHAR* mimeType, CLSID& clsid) {
    if (!mimeType || *mimeType == L'\0' || !EnsureGdiplus()) {
        return false;
    }

    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) {
        return false;
    }

    std::vector<BYTE> buffer(size);
    auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(num, size, codecs) != Gdiplus::Ok) {
        return false;
    }

    for (UINT i = 0; i < num; ++i) {
        if (wcscmp(codecs[i].MimeType, mimeType) == 0) {
            clsid = codecs[i].Clsid;
            return true;
        }
    }

    return false;
}

std::unique_ptr<Gdiplus::Bitmap> CreateBitmapFromImage(const Image& image) {
    if (!image.IsValid() || !EnsureGdiplus()) {
        return {};
    }

    auto bitmap = std::make_unique<Gdiplus::Bitmap>(image.width, image.height, PixelFormat32bppARGB);
    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok) {
        return {};
    }

    if (!CopyImagePixelsToBitmap(image, *bitmap)) {
        return {};
    }

    return bitmap;
}

bool CopyBitmapToImage(Gdiplus::Bitmap& bitmap, Image& image) {
    if (!EnsureGdiplus()) {
        return false;
    }

    const int width = static_cast<int>(bitmap.GetWidth());
    const int height = static_cast<int>(bitmap.GetHeight());
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (!image.IsValid() || image.width != width || image.height != height) {
        image = Image::Create(width, height);
        if (!image.IsValid()) {
            return false;
        }
    }

    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData data{};
    if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return false;
    }

    for (int y = 0; y < height; ++y) {
        const auto* src = static_cast<const uint8_t*>(data.Scan0) + static_cast<size_t>(data.Stride) * static_cast<size_t>(y);
        auto* dst = image.bgra.data() + static_cast<size_t>(width) * static_cast<size_t>(y) * 4;
        memcpy(dst, src, static_cast<size_t>(width) * 4);
    }

    bitmap.UnlockBits(&data);
    return true;
}

bool EncodeImageToBytes(const Image& image, const WCHAR* mimeType, std::vector<uint8_t>& bytes) {
    bytes.clear();

    auto bitmap = CreateBitmapFromImage(image);
    if (!bitmap) {
        return false;
    }

    CLSID encoder{};
    if (!FindEncoderClsid(mimeType, encoder)) {
        return false;
    }

    ScopedComStream stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.Receive())) || !stream.Get()) {
        return false;
    }

    if (bitmap->Save(stream.Get(), &encoder, nullptr) != Gdiplus::Ok) {
        return false;
    }

    HGLOBAL global = nullptr;
    if (FAILED(GetHGlobalFromStream(stream.Get(), &global)) || !global) {
        return false;
    }

    const SIZE_T size = GlobalSize(global);
    if (size == 0) {
        return false;
    }

    ScopedGlobalLock lock(global);
    if (!lock.Get()) {
        return false;
    }

    bytes.resize(size);
    memcpy(bytes.data(), lock.Get(), size);
    return true;
}

} // namespace ImageCodecUtil
