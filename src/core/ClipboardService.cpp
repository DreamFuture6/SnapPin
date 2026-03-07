#include "core/ClipboardService.h"
#include "core/ImageCodecUtil.h"

namespace {
class ScopedGlobalMemory {
public:
    explicit ScopedGlobalMemory(HGLOBAL handle = nullptr)
        : handle_(handle) {
    }

    ~ScopedGlobalMemory() {
        if (handle_) {
            GlobalFree(handle_);
        }
    }

    ScopedGlobalMemory(const ScopedGlobalMemory&) = delete;
    ScopedGlobalMemory& operator=(const ScopedGlobalMemory&) = delete;

    ScopedGlobalMemory(ScopedGlobalMemory&& other) noexcept
        : handle_(other.Release()) {
    }

    ScopedGlobalMemory& operator=(ScopedGlobalMemory&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                GlobalFree(handle_);
            }
            handle_ = other.Release();
        }
        return *this;
    }

    HGLOBAL Get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

    HGLOBAL Release() {
        const HGLOBAL released = handle_;
        handle_ = nullptr;
        return released;
    }

private:
    HGLOBAL handle_ = nullptr;
};

class ScopedClipboard {
public:
    explicit ScopedClipboard(HWND owner)
        : opened_(OpenClipboard(owner) != FALSE) {
    }

    ~ScopedClipboard() {
        if (opened_) {
            CloseClipboard();
        }
    }

    ScopedClipboard(const ScopedClipboard&) = delete;
    ScopedClipboard& operator=(const ScopedClipboard&) = delete;

    explicit operator bool() const { return opened_; }

private:
    bool opened_ = false;
};

HGLOBAL BuildDibV5(const Image& image) {
    if (!image.IsValid()) {
        return nullptr;
    }

    const size_t headerSize = sizeof(BITMAPV5HEADER);
    const size_t pixelSize = static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4;
    HGLOBAL hMem = GlobalAlloc(GHND, headerSize + pixelSize);
    if (!hMem) {
        return nullptr;
    }

    auto* ptr = static_cast<BYTE*>(GlobalLock(hMem));
    if (!ptr) {
        GlobalFree(hMem);
        return nullptr;
    }

    auto* header = reinterpret_cast<BITMAPV5HEADER*>(ptr);
    ZeroMemory(header, sizeof(BITMAPV5HEADER));
    header->bV5Size = sizeof(BITMAPV5HEADER);
    header->bV5Width = image.width;
    header->bV5Height = -image.height;
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5RedMask = 0x00FF0000;
    header->bV5GreenMask = 0x0000FF00;
    header->bV5BlueMask = 0x000000FF;
    header->bV5AlphaMask = 0xFF000000;
    header->bV5CSType = LCS_sRGB;

    memcpy(ptr + headerSize, image.bgra.data(), pixelSize);
    GlobalUnlock(hMem);
    return hMem;
}

HGLOBAL BuildPngBlob(const Image& image) {
    std::vector<uint8_t> pngBytes;
    if (!ImageCodecUtil::EncodeImageToBytes(image, L"image/png", pngBytes)) {
        return nullptr;
    }

    HGLOBAL out = GlobalAlloc(GHND, pngBytes.size());
    if (!out) {
        return nullptr;
    }

    void* dst = GlobalLock(out);
    if (!dst) {
        GlobalFree(out);
        return nullptr;
    }

    memcpy(dst, pngBytes.data(), pngBytes.size());
    GlobalUnlock(out);
    return out;
}
}

bool ClipboardService::CopyImage(HWND owner, const Image& image) const {
    if (!image.IsValid()) {
        return false;
    }

    ScopedGlobalMemory dib(BuildDibV5(image));
    ScopedGlobalMemory png(BuildPngBlob(image));
    if (!dib) {
        return false;
    }

    ScopedClipboard clipboard(owner);
    if (!clipboard) {
        return false;
    }

    EmptyClipboard();
    if (!SetClipboardData(CF_DIBV5, dib.Get())) {
        return false;
    }
    dib.Release();

    if (png) {
        const UINT fmt = RegisterClipboardFormatW(L"PNG");
        if (fmt != 0 && SetClipboardData(fmt, png.Get())) {
            png.Release();
        }
    }

    return true;
}

bool ClipboardService::CopyText(HWND owner, const std::wstring& text) const {
    if (text.empty()) {
        return false;
    }

    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    ScopedGlobalMemory hMem(GlobalAlloc(GHND, bytes));
    if (!hMem) {
        return false;
    }

    void* ptr = GlobalLock(hMem.Get());
    if (!ptr) {
        return false;
    }
    memcpy(ptr, text.c_str(), bytes);
    GlobalUnlock(hMem.Get());

    ScopedClipboard clipboard(owner);
    if (!clipboard) {
        return false;
    }

    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, hMem.Get())) {
        return false;
    }
    hMem.Release();

    return true;
}

bool ClipboardService::CopyFilePath(HWND owner, const std::filesystem::path& path) const {
    const auto wpath = path.wstring();
    const size_t bytes = (wpath.size() + 2) * sizeof(wchar_t);
    const size_t total = sizeof(DROPFILES) + bytes;

    ScopedGlobalMemory hMem(GlobalAlloc(GHND, total));
    if (!hMem) {
        return false;
    }

    auto* ptr = static_cast<BYTE*>(GlobalLock(hMem.Get()));
    if (!ptr) {
        return false;
    }

    auto* drop = reinterpret_cast<DROPFILES*>(ptr);
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;

    wchar_t* fileBuf = reinterpret_cast<wchar_t*>(ptr + sizeof(DROPFILES));
    wcscpy_s(fileBuf, wpath.size() + 1, wpath.c_str());
    fileBuf[wpath.size() + 1] = L'\0';
    GlobalUnlock(hMem.Get());

    ScopedClipboard clipboard(owner);
    if (!clipboard) {
        return false;
    }

    EmptyClipboard();
    if (!SetClipboardData(CF_HDROP, hMem.Get())) {
        return false;
    }
    hMem.Release();

    return true;
}
