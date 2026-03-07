#include "ui/GdiBitmapBuffer.h"

GdiBitmapBuffer::~GdiBitmapBuffer() {
    Reset();
}

GdiBitmapBuffer::GdiBitmapBuffer(GdiBitmapBuffer&& other) noexcept {
    Swap(other);
}

GdiBitmapBuffer& GdiBitmapBuffer::operator=(GdiBitmapBuffer&& other) noexcept {
    if (this != &other) {
        Reset();
        Swap(other);
    }
    return *this;
}

bool GdiBitmapBuffer::Ensure(HDC referenceDc, int width, int height) {
    if (!referenceDc || width <= 0 || height <= 0) {
        return false;
    }

    if (!dc_) {
        dc_ = CreateCompatibleDC(referenceDc);
        if (!dc_) {
            return false;
        }
    }

    if (bitmap_ && width <= width_ && height <= height_) {
        return true;
    }

    const int targetWidth = std::max(width, width_);
    const int targetHeight = std::max(height, height_);
    HBITMAP newBitmap = CreateCompatibleBitmap(referenceDc, targetWidth, targetHeight);
    if (!newBitmap) {
        return false;
    }

    if (bitmap_) {
        DeleteObject(bitmap_);
    }
    bitmap_ = newBitmap;
    width_ = targetWidth;
    height_ = targetHeight;
    return true;
}

void GdiBitmapBuffer::Reset() {
    if (bitmap_) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
    if (dc_) {
        DeleteDC(dc_);
        dc_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
}

void GdiBitmapBuffer::Swap(GdiBitmapBuffer& other) noexcept {
    std::swap(dc_, other.dc_);
    std::swap(bitmap_, other.bitmap_);
    std::swap(width_, other.width_);
    std::swap(height_, other.height_);
}
