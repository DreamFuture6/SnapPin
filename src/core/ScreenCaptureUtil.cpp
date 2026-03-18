#include "core/ScreenCaptureUtil.h"

namespace {

class ScreenDcHandle {
public:
    ScreenDcHandle()
        : dc_(GetDC(nullptr)) {
    }

    ~ScreenDcHandle() {
        if (dc_) {
            ReleaseDC(nullptr, dc_);
        }
    }

    ScreenDcHandle(const ScreenDcHandle&) = delete;
    ScreenDcHandle& operator=(const ScreenDcHandle&) = delete;

    HDC get() const { return dc_; }

private:
    HDC dc_ = nullptr;
};

class CompatibleDcHandle {
public:
    explicit CompatibleDcHandle(HDC referenceDc)
        : dc_(referenceDc ? CreateCompatibleDC(referenceDc) : nullptr) {
    }

    ~CompatibleDcHandle() {
        if (dc_) {
            DeleteDC(dc_);
        }
    }

    CompatibleDcHandle(const CompatibleDcHandle&) = delete;
    CompatibleDcHandle& operator=(const CompatibleDcHandle&) = delete;

    HDC get() const { return dc_; }

private:
    HDC dc_ = nullptr;
};

class BitmapHandle {
public:
    explicit BitmapHandle(HBITMAP bitmap = nullptr)
        : bitmap_(bitmap) {
    }

    ~BitmapHandle() {
        if (bitmap_) {
            DeleteObject(bitmap_);
        }
    }

    BitmapHandle(const BitmapHandle&) = delete;
    BitmapHandle& operator=(const BitmapHandle&) = delete;

    HBITMAP get() const { return bitmap_; }

private:
    HBITMAP bitmap_ = nullptr;
};

class ScopedSelectObject {
public:
    ScopedSelectObject(HDC dc, HGDIOBJ object)
        : dc_(dc),
          oldObject_((dc && object) ? SelectObject(dc, object) : nullptr) {
    }

    ~ScopedSelectObject() {
        if (dc_ && oldObject_) {
            SelectObject(dc_, oldObject_);
        }
    }

    ScopedSelectObject(const ScopedSelectObject&) = delete;
    ScopedSelectObject& operator=(const ScopedSelectObject&) = delete;

    bool IsValid() const { return oldObject_ != nullptr; }

private:
    HDC dc_ = nullptr;
    HGDIOBJ oldObject_ = nullptr;
};

void DrawCursorOnCapture(HDC targetDc, const RECT& captureRect)
{
    if (!targetDc) {
        return;
    }

    CURSORINFO cursorInfo{};
    cursorInfo.cbSize = sizeof(cursorInfo);
    if (!GetCursorInfo(&cursorInfo) || (cursorInfo.flags & CURSOR_SHOWING) == 0 || !cursorInfo.hCursor) {
        return;
    }

    ICONINFO iconInfo{};
    if (!GetIconInfo(cursorInfo.hCursor, &iconInfo)) {
        return;
    }

    BitmapHandle maskBitmap(iconInfo.hbmMask);
    BitmapHandle colorBitmap(iconInfo.hbmColor);
    const int drawX = cursorInfo.ptScreenPos.x - static_cast<int>(iconInfo.xHotspot) - captureRect.left;
    const int drawY = cursorInfo.ptScreenPos.y - static_cast<int>(iconInfo.yHotspot) - captureRect.top;
    DrawIconEx(targetDc, drawX, drawY, cursorInfo.hCursor, 0, 0, 0, nullptr, DI_NORMAL);
}

} // namespace

namespace ScreenCaptureUtil {

bool CaptureScreenRect(const RECT& screenRect, Image& outImage, bool captureLayeredWindows, bool captureCursor) {
    outImage = {};

    const RECT normalized = NormalizeRect(screenRect);
    const int width = RectWidth(normalized);
    const int height = RectHeight(normalized);
    if (width <= 0 || height <= 0) {
        return false;
    }

    ScreenDcHandle screenDc;
    if (!screenDc.get()) {
        return false;
    }

    CompatibleDcHandle memoryDc(screenDc.get());
    if (!memoryDc.get()) {
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    BitmapHandle dib(CreateDIBSection(screenDc.get(), &bmi, DIB_RGB_COLORS, &bits, nullptr, 0));
    if (!dib.get() || !bits) {
        return false;
    }

    ScopedSelectObject selectedBitmap(memoryDc.get(), dib.get());
    if (!selectedBitmap.IsValid()) {
        return false;
    }

    if (!BitBlt(memoryDc.get(), 0, 0, width, height, screenDc.get(),
            normalized.left, normalized.top, (captureLayeredWindows ? (SRCCOPY | CAPTUREBLT) : SRCCOPY))) {
        return false;
    }

    if (captureCursor) {
        DrawCursorOnCapture(memoryDc.get(), normalized);
    }

    Image image = Image::Create(width, height);
    if (!image.IsValid()) {
        return false;
    }

    memcpy(image.bgra.data(), bits, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    outImage = std::move(image);
    return true;
}

} // namespace ScreenCaptureUtil