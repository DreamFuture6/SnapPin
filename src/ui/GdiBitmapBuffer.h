#pragma once

#include "common.h"

class GdiBitmapBuffer {
public:
    GdiBitmapBuffer() = default;
    ~GdiBitmapBuffer();

    GdiBitmapBuffer(const GdiBitmapBuffer&) = delete;
    GdiBitmapBuffer& operator=(const GdiBitmapBuffer&) = delete;

    GdiBitmapBuffer(GdiBitmapBuffer&& other) noexcept;
    GdiBitmapBuffer& operator=(GdiBitmapBuffer&& other) noexcept;

    bool Ensure(HDC referenceDc, int width, int height);
    void Reset();

    HDC dc() const { return dc_; }
    HBITMAP bitmap() const { return bitmap_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    void Swap(GdiBitmapBuffer& other) noexcept;

    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};
