#include "ui/OcrResultPopupWindow.h"
#include "ui/UiUtil.h"
#include "ui/GdiResourceCache.h"
#include "ui/ThemeColors.h"

namespace {

constexpr wchar_t kOcrResultPopupClassName[] = L"SnapPinOcrResultPopupWindowClass";
constexpr UINT_PTR kAutoCloseTimerId = 1;
constexpr UINT kAutoCloseDelayMs = 10000;

// 使用统一的主题颜色
using namespace ThemeColors::Component::OcrPopup;

// 使用统一的 DPI 缩放函数
inline int DpiScale(int value, UINT dpi) {
    return UiUtil::DpiScale(value, dpi);
}

UINT MonitorDpi(HMONITOR monitor, HWND fallbackWindow) {
    if (monitor) {
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        static GetDpiForMonitorFn getDpiForMonitor = []() -> GetDpiForMonitorFn {
            HMODULE shcore = LoadLibraryW(L"Shcore.dll");
            if (!shcore) {
                return nullptr;
            }
            return reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor"));
        }();
        if (getDpiForMonitor) {
            UINT xDpi = 96;
            UINT yDpi = 96;
            if (SUCCEEDED(getDpiForMonitor(monitor, 0, &xDpi, &yDpi)) && xDpi > 0) {
                return xDpi;
            }
        }
    }

    if (fallbackWindow && IsWindow(fallbackWindow)) {
        const UINT dpi = GetDpiForWindow(fallbackWindow);
        if (dpi > 0) {
            return dpi;
        }
    }
    return 96;
}

HMONITOR ResolveMonitor(const std::optional<RECT>& anchorScreenRect, HWND owner) {
    if (anchorScreenRect.has_value()) {
        const RECT rc = *anchorScreenRect;
        const POINT center{
            rc.left + (rc.right - rc.left) / 2,
            rc.top + (rc.bottom - rc.top) / 2
        };
        return MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST);
    }
    if (owner && IsWindow(owner)) {
        return MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    }
    const POINT origin{ 0, 0 };
    return MonitorFromPoint(origin, MONITOR_DEFAULTTONEAREST);
}

RECT ResolveWorkArea(HMONITOR monitor) {
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (monitor && GetMonitorInfoW(monitor, &mi)) {
        return mi.rcWork;
    }
    RECT rc{};
    rc.right = GetSystemMetrics(SM_CXSCREEN);
    rc.bottom = GetSystemMetrics(SM_CYSCREEN);
    return rc;
}

} // namespace

OcrResultPopupWindow::~OcrResultPopupWindow() {
    Close();
}

void OcrResultPopupWindow::RegisterWindowClass(HINSTANCE hInstance) {
    static std::once_flag once;
    std::call_once(once, [hInstance]() {
        WNDCLASSW wc{};
        wc.lpfnWndProc = OcrResultPopupWindow::WndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kOcrResultPopupClassName;
        RegisterClassW(&wc);
    });
}

bool OcrResultPopupWindow::Show(HINSTANCE hInstance, HWND owner, const std::wstring& title, const std::wstring& text,
    const std::optional<RECT>& anchorScreenRect) {
    Close();

    hInstance_ = hInstance;
    owner_ = owner;
    titleText_ = title;
    bodyText_ = text;

    RegisterWindowClass(hInstance_);

    const HMONITOR monitor = ResolveMonitor(anchorScreenRect, owner_);
    const RECT workArea = ResolveWorkArea(monitor);
    dpi_ = MonitorDpi(monitor, owner_);

    const int width = DpiScale(440, dpi_);
    const int height = DpiScale(250, dpi_);
    const int margin = DpiScale(18, dpi_);
    const int x = workArea.right - margin - width;
    const int y = workArea.bottom - margin - height;

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kOcrResultPopupClassName,
        L"",
        WS_POPUP,
        x, y, width, height,
        owner_,
        nullptr,
        hInstance_,
        this);

    if (!hwnd_) {
        return false;
    }

    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);
    return true;
}

void OcrResultPopupWindow::Close() {
    if (autoCloseTimer_ != 0 && hwnd_) {
        KillTimer(hwnd_, autoCloseTimer_);
        autoCloseTimer_ = 0;
    }
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    // 注意：titleFont_ 和 bodyFont_ 由 GdiResourceCache 管理，不在这里删除
    if (backgroundBrush_) {
        DeleteObject(backgroundBrush_);
        backgroundBrush_ = nullptr;
    }
    if (panelBrush_) {
        DeleteObject(panelBrush_);
        panelBrush_ = nullptr;
    }
    titleLabel_ = nullptr;
    bodyEdit_ = nullptr;
}

void OcrResultPopupWindow::EnsureFonts() {
    // 从缓存获取字体，不再手动创建/销毁
    auto& cache = GdiResourceCache::Instance();
    const int titleSize = DpiScale(18, dpi_);
    const int bodySize = DpiScale(15, dpi_);
    titleFont_ = cache.GetFont(L"Segoe UI", titleSize, FW_BOLD);
    bodyFont_ = cache.GetFont(L"Segoe UI", bodySize, FW_NORMAL);
}

void OcrResultPopupWindow::Layout() {
    if (!hwnd_) {
        return;
    }

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int padding = DpiScale(16, dpi_);
    const int titleHeight = DpiScale(26, dpi_);
    const int bodyTop = padding + titleHeight + DpiScale(10, dpi_);
    const int bodyHeight = (std::max)(DpiScale(80, dpi_), static_cast<int>(rc.bottom) - bodyTop - padding);

    if (titleLabel_) {
        SetWindowPos(titleLabel_, nullptr, padding, padding, rc.right - padding * 2, titleHeight, SWP_NOZORDER);
    }
    if (bodyEdit_) {
        SetWindowPos(bodyEdit_, nullptr, padding, bodyTop, rc.right - padding * 2, bodyHeight, SWP_NOZORDER);
    }
}

LRESULT CALLBACK OcrResultPopupWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OcrResultPopupWindow* self = reinterpret_cast<OcrResultPopupWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<OcrResultPopupWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT OcrResultPopupWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        EnsureFonts();
        backgroundBrush_ = CreateSolidBrush(BackgroundColor);
        panelBrush_ = CreateSolidBrush(PanelColor);

        titleLabel_ = CreateWindowW(WC_STATICW, titleText_.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, hwnd_, nullptr, hInstance_, nullptr);
        bodyEdit_ = CreateWindowExW(0, WC_EDITW, bodyText_.c_str(),
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            0, 0, 0, 0, hwnd_, nullptr, hInstance_, nullptr);

        if (!backgroundBrush_ || !panelBrush_ || !titleLabel_ || !bodyEdit_) {
            return -1;
        }

        if (titleFont_) {
            SendMessageW(titleLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);
        }
        if (bodyFont_) {
            SendMessageW(bodyEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont_), TRUE);
        }
        SendMessageW(bodyEdit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
            MAKELPARAM(DpiScale(8, dpi_), DpiScale(8, dpi_)));

        Layout();
        autoCloseTimer_ = SetTimer(hwnd_, kAutoCloseTimerId, kAutoCloseDelayMs, nullptr);
        return 0;
    }
    case WM_SIZE:
        Layout();
        return 0;
    case WM_TIMER:
        if (wParam == kAutoCloseTimerId) {
            Close();
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        Close();
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND target = reinterpret_cast<HWND>(lParam);
        if (target == titleLabel_) {
            SetBkColor(hdc, BackgroundColor);
            SetTextColor(hdc, TitleColor);
            return reinterpret_cast<LRESULT>(backgroundBrush_);
        }
        if (target == bodyEdit_) {
            SetBkColor(hdc, PanelColor);
            SetTextColor(hdc, BodyColor);
            return reinterpret_cast<LRESULT>(panelBrush_);
        }
        break;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd_, &ps);
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        FillRect(hdc, &rc, backgroundBrush_);
        HBRUSH borderBrush = CreateSolidBrush(BorderColor);
        if (borderBrush) {
            FrameRect(hdc, &rc, borderBrush);
            DeleteObject(borderBrush);
        }
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_NCDESTROY:
        if (autoCloseTimer_ != 0) {
            KillTimer(hwnd_, autoCloseTimer_);
            autoCloseTimer_ = 0;
        }
        hwnd_ = nullptr;
        titleLabel_ = nullptr;
        bodyEdit_ = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}
