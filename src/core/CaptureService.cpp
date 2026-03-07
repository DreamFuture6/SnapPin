#include "core/CaptureService.h"
#include "core/Logger.h"
#include "core/ScreenCaptureUtil.h"

namespace {
BOOL CALLBACK EnumWinProc(HWND hwnd, LPARAM lParam) {
    auto list = reinterpret_cast<std::vector<CapturableWindow>*>(lParam);
    if (!CaptureService::IsWindowCapturable(hwnd)) {
        return TRUE;
    }

    RECT rc{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rc, sizeof(rc)))) {
        GetWindowRect(hwnd, &rc);
    }
    if (RectWidth(rc) <= 1 || RectHeight(rc) <= 1) {
        return TRUE;
    }

    wchar_t title[256]{};
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));

    CapturableWindow item;
    item.hwnd = hwnd;
    item.rect = rc;
    item.title = title;
    list->push_back(std::move(item));
    return TRUE;
}
}

bool CaptureService::CaptureVirtualScreen(ScreenCapture& out) const {
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (w <= 0 || h <= 0) {
        return false;
    }

    const RECT virtualRect{ x, y, x + w, y + h };
    Image image;
    if (!ScreenCaptureUtil::CaptureScreenRect(virtualRect, image)) {
        Logger::Instance().Error(L"BitBlt capture failed.");
        return false;
    }

    out.virtualRect = virtualRect;
    out.image = std::move(image);
    return true;
}

std::vector<CapturableWindow> CaptureService::EnumerateWindows() const {
    std::vector<CapturableWindow> out;
    EnumWindows(EnumWinProc, reinterpret_cast<LPARAM>(&out));
    return out;
}

bool CaptureService::IsWindowCapturable(HWND hwnd) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        return false;
    }
    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        return false;
    }

    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) {
        return false;
    }

    wchar_t cls[128]{};
    GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
    if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0) {
        return false;
    }

    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) {
        return false;
    }

    RECT rc{};
    GetWindowRect(hwnd, &rc);
    return RectWidth(rc) > 16 && RectHeight(rc) > 16;
}
