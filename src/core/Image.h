#pragma once
#include "common.h"

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bgra; // 32bpp BGRA top-down

    bool IsValid() const {
        return width > 0 && height > 0 && bgra.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    }

    COLORREF GetPixel(int x, int y) const;
    Image Crop(const RECT& rect) const;
    Image ResizeNearest(int newWidth, int newHeight) const;

    static Image Create(int w, int h);
};

struct ScreenCapture {
    RECT virtualRect{}; // screen coordinates
    Image image;
};

Image PixelateRect(const Image& src, RECT rect, int blockSize);
