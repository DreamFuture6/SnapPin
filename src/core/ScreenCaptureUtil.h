#pragma once

#include "common.h"
#include "core/Image.h"

namespace ScreenCaptureUtil {

bool CaptureScreenRect(const RECT& screenRect, Image& outImage, bool captureLayeredWindows = true, bool captureCursor = false);

} // namespace ScreenCaptureUtil
