#pragma once

#include "common.h"
#include "core/Image.h"

namespace ImageCodecUtil {

bool EnsureGdiplus();
bool FindEncoderClsid(const WCHAR* mimeType, CLSID& clsid);
std::unique_ptr<Gdiplus::Bitmap> CreateBitmapFromImage(const Image& image);
bool CopyBitmapToImage(Gdiplus::Bitmap& bitmap, Image& image);
bool EncodeImageToBytes(const Image& image, const WCHAR* mimeType, std::vector<uint8_t>& bytes);

} // namespace ImageCodecUtil
