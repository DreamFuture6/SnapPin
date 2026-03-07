#include "ui/WindowUtil.h"

namespace WindowUtil {

void RegisterWindowClassOnce(
    std::once_flag& once,
    HINSTANCE hInstance,
    LPCWSTR className,
    WNDPROC wndProc,
    HCURSOR cursor,
    DWORD style,
    HBRUSH backgroundBrush,
    HICON icon)
{
    std::call_once(once, [=]() {
        WNDCLASSW wc{};
        wc.lpfnWndProc = wndProc;
        wc.hInstance = hInstance;
        wc.hCursor = cursor ? cursor : LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = className;
        wc.hbrBackground = backgroundBrush;
        wc.hIcon = icon;
        wc.style = style;
        RegisterClassW(&wc);
    });
}

void RegisterWindowClassExOnce(
    std::once_flag& once,
    HINSTANCE hInstance,
    LPCWSTR className,
    WNDPROC wndProc,
    HCURSOR cursor,
    HBRUSH backgroundBrush,
    HICON icon,
    HICON iconSmall,
    DWORD style)
{
    std::call_once(once, [=]() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = style;
        wc.lpfnWndProc = wndProc;
        wc.hInstance = hInstance;
        wc.hCursor = cursor ? cursor : LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = className;
        wc.hbrBackground = backgroundBrush;
        wc.hIcon = icon;
        wc.hIconSm = iconSmall;
        RegisterClassExW(&wc);
    });
}

} // namespace WindowUtil
