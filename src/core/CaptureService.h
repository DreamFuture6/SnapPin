#pragma once
#include "common.h"
#include "core/Image.h"

struct CapturableWindow {
    HWND hwnd = nullptr;
    RECT rect{};
    std::wstring title;
};

class CaptureService {
public:
    bool CaptureVirtualScreen(ScreenCapture& out) const;
    std::vector<CapturableWindow> EnumerateWindows() const;
    static bool IsWindowCapturable(HWND hwnd);

private:
};
