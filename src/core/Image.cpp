#include "core/Image.h"

Image Image::Create(int w, int h) {
    Image img;
    if (w <= 0 || h <= 0) {
        return img;
    }
    img.width = w;
    img.height = h;
    img.bgra.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
    return img;
}

COLORREF Image::GetPixel(int x, int y) const {
    if (!IsValid() || x < 0 || y < 0 || x >= width || y >= height) {
        return RGB(0, 0, 0);
    }
    const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
    return RGB(bgra[idx + 2], bgra[idx + 1], bgra[idx]);
}

Image Image::Crop(const RECT& rect) const {
    if (!IsValid()) {
        return {};
    }
    RECT rc = NormalizeRect(rect);
    rc.left = std::clamp(rc.left, 0L, static_cast<LONG>(width));
    rc.top = std::clamp(rc.top, 0L, static_cast<LONG>(height));
    rc.right = std::clamp(rc.right, 0L, static_cast<LONG>(width));
    rc.bottom = std::clamp(rc.bottom, 0L, static_cast<LONG>(height));
    if (rc.right <= rc.left || rc.bottom <= rc.top) {
        return {};
    }

    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    Image out = Create(w, h);
    for (int y = 0; y < h; ++y) {
        const size_t srcIdx = (static_cast<size_t>(rc.top + y) * static_cast<size_t>(width) + static_cast<size_t>(rc.left)) * 4;
        const size_t dstIdx = static_cast<size_t>(y) * static_cast<size_t>(w) * 4;
        memcpy(out.bgra.data() + dstIdx, bgra.data() + srcIdx, static_cast<size_t>(w) * 4);
    }
    return out;
}

Image Image::ResizeNearest(int newWidth, int newHeight) const {
    if (!IsValid() || newWidth <= 0 || newHeight <= 0) {
        return {};
    }
    Image out = Create(newWidth, newHeight);
    const double sx = static_cast<double>(width) / static_cast<double>(newWidth);
    const double sy = static_cast<double>(height) / static_cast<double>(newHeight);

    for (int y = 0; y < newHeight; ++y) {
        const int srcY = std::clamp(static_cast<int>(y * sy), 0, height - 1);
        for (int x = 0; x < newWidth; ++x) {
            const int srcX = std::clamp(static_cast<int>(x * sx), 0, width - 1);
            const size_t srcIdx = (static_cast<size_t>(srcY) * static_cast<size_t>(width) + static_cast<size_t>(srcX)) * 4;
            const size_t dstIdx = (static_cast<size_t>(y) * static_cast<size_t>(newWidth) + static_cast<size_t>(x)) * 4;
            out.bgra[dstIdx + 0] = bgra[srcIdx + 0];
            out.bgra[dstIdx + 1] = bgra[srcIdx + 1];
            out.bgra[dstIdx + 2] = bgra[srcIdx + 2];
            out.bgra[dstIdx + 3] = bgra[srcIdx + 3];
        }
    }
    return out;
}

Image PixelateRect(const Image& src, RECT rect, int blockSize) {
    if (!src.IsValid()) {
        return {};
    }
    Image out = src;
    RECT rc = NormalizeRect(rect);
    rc.left = std::clamp(rc.left, 0L, static_cast<LONG>(src.width - 1));
    rc.top = std::clamp(rc.top, 0L, static_cast<LONG>(src.height - 1));
    rc.right = std::clamp(rc.right, 0L, static_cast<LONG>(src.width));
    rc.bottom = std::clamp(rc.bottom, 0L, static_cast<LONG>(src.height));

    const int bs = std::max(2, blockSize);
    for (int y = rc.top; y < rc.bottom; y += bs) {
        for (int x = rc.left; x < rc.right; x += bs) {
            const int cx = std::min(x + bs / 2, src.width - 1);
            const int cy = std::min(y + bs / 2, src.height - 1);
            const size_t cidx = (static_cast<size_t>(cy) * static_cast<size_t>(src.width) + static_cast<size_t>(cx)) * 4;
            const uint8_t b = src.bgra[cidx + 0];
            const uint8_t g = src.bgra[cidx + 1];
            const uint8_t r = src.bgra[cidx + 2];
            const uint8_t a = src.bgra[cidx + 3];

            for (int yy = y; yy < std::min(y + bs, static_cast<int>(rc.bottom)); ++yy) {
                for (int xx = x; xx < std::min(x + bs, static_cast<int>(rc.right)); ++xx) {
                    const size_t idx = (static_cast<size_t>(yy) * static_cast<size_t>(src.width) + static_cast<size_t>(xx)) * 4;
                    out.bgra[idx + 0] = b;
                    out.bgra[idx + 1] = g;
                    out.bgra[idx + 2] = r;
                    out.bgra[idx + 3] = a;
                }
            }
        }
    }
    return out;
}
