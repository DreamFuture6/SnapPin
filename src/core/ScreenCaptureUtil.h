#pragma once

#include "common.h"
#include "core/Image.h"

namespace ScreenCaptureUtil {

bool CaptureScreenRect(const RECT& screenRect, Image& outImage);

} // namespace ScreenCaptureUtil
