#include "ui/UiUtil.h"
#include "ui/GdiResourceCache.h"

namespace UiUtil {

UINT GetWindowDpiSafe(HWND hwnd) {
    if (hwnd && IsWindow(hwnd)) {
        const UINT dpi = GetDpiForWindow(hwnd);
        if (dpi > 0) {
            return dpi;
        }
    }
    return kDefaultDpi;
}

int DpiScale(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), static_cast<int>(kDefaultDpi));
}

COLORREF UnifiedBorderColor(bool active, bool hovered, COLORREF borderDefault, COLORREF borderHover, COLORREF borderActive) {
    return active ? borderActive : (hovered ? borderHover : borderDefault);
}

void FillRectColor(HDC hdc, const RECT& rc, COLORREF color) {
    // 使用 GDI 资源缓存避免重复创建/销毁笔刷
    auto& cache = GdiResourceCache::Instance();
    HBRUSH brush = cache.GetBrush(color);
    if (brush) {
        FillRect(hdc, &rc, brush);
    } else {
        // 缓存满时的备用方案
        HBRUSH tempBrush = CreateSolidBrush(color);
        if (tempBrush) {
            FillRect(hdc, &rc, tempBrush);
            DeleteObject(tempBrush);
        }
    }
}

void AddRoundRectPath(Gdiplus::GraphicsPath& path, const RECT& rc, float radius) {
    const float l = static_cast<float>(rc.left);
    const float t = static_cast<float>(rc.top);
    const float r = static_cast<float>(rc.right);
    const float b = static_cast<float>(rc.bottom);
    const float w = r - l;
    const float h = b - t;
    const float rr = std::max(0.0f, std::min(radius, std::min(w, h) * 0.5f));
    if (rr <= 0.1f) {
        path.AddRectangle(Gdiplus::RectF(l, t, w, h));
        return;
    }
    const float d = rr * 2.0f;
    path.AddArc(l, t, d, d, 180.0f, 90.0f);
    path.AddArc(r - d, t, d, d, 270.0f, 90.0f);
    path.AddArc(r - d, b - d, d, d, 0.0f, 90.0f);
    path.AddArc(l, b - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void DrawRoundedFillStroke(HDC hdc, const RECT& rc, COLORREF fill, COLORREF stroke,
    float strokeWidth, float radius, bool fillEnabled) {
    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    Gdiplus::GraphicsPath fillPath;
    AddRoundRectPath(fillPath, rc, radius);

    if (fillEnabled) {
        Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
        g.FillPath(&brush, &fillPath);
    }
    if (strokeWidth > 0.0f) {
        RECT strokeRc = rc;
        const int inset = std::max(1, static_cast<int>(std::ceil(strokeWidth)));
        InflateRect(&strokeRc, -inset, -inset);
        Gdiplus::GraphicsPath strokePath;
        AddRoundRectPath(strokePath, strokeRc, std::max(0.0f, radius - static_cast<float>(inset)));
        Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(stroke), GetGValue(stroke), GetBValue(stroke)), strokeWidth);
        g.DrawPath(&pen, &strokePath);
    }
}

void DrawRoundBorderGdi(HDC hdc, const RECT& rc, COLORREF color, int thickness, int radius) {
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) {
        return;
    }
    const int stroke = std::max(1, thickness);
    const int inset = std::max(1, (stroke + 1) / 2);
    RECT r = rc;
    InflateRect(&r, -inset, -inset);
    if (r.right <= r.left || r.bottom <= r.top) {
        return;
    }

    const float l = static_cast<float>(r.left);
    const float t = static_cast<float>(r.top);
    const float rr = static_cast<float>(r.right);
    const float bb = static_cast<float>(r.bottom);
    const float rw = rr - l;
    const float rh = bb - t;
    const float rad = static_cast<float>(std::max(2, radius));
    const float cr = std::max(1.0f, std::min(rad, std::min(rw, rh) * 0.5f));
    const float d = cr * 2.0f;

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    Gdiplus::GraphicsPath path;
    path.AddArc(l, t, d, d, 180.0f, 90.0f);
    path.AddArc(rr - d, t, d, d, 270.0f, 90.0f);
    path.AddArc(rr - d, bb - d, d, d, 0.0f, 90.0f);
    path.AddArc(l, bb - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();

    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)),
        static_cast<Gdiplus::REAL>(stroke));
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    g.DrawPath(&pen, &path);
}

void ApplyRoundedRegion(HWND hwnd, int radiusPx) {
    if (!hwnd || !IsWindow(hwnd)) {
        return;
    }
    RECT rc{};
    GetClientRect(hwnd, &rc);
    if (rc.right <= rc.left || rc.bottom <= rc.top) {
        return;
    }
    const int dia = std::max(2, radiusPx * 2);
    HRGN rgn = CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, dia, dia);
    if (rgn && SetWindowRgn(hwnd, rgn, TRUE) == 0) {
        DeleteObject(rgn);
    }
}

void ConfigureComboControl(HWND combo, int controlHeight, int visibleItems, int minItemHeight, int verticalPadding) {
    if (!combo) {
        return;
    }
    const int itemHeight = std::max(minItemHeight, controlHeight - verticalPadding);
    SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), static_cast<LPARAM>(itemHeight));
    SendMessageW(combo, CB_SETITEMHEIGHT, 0, static_cast<LPARAM>(itemHeight));
    SendMessageW(combo, CB_SETMINVISIBLE, static_cast<WPARAM>(visibleItems), 0);
}

void ApplyStableComboTheme(HWND combo) {
    if (!combo) {
        return;
    }
    using SetWindowThemeFn = HRESULT(WINAPI *)(HWND, LPCWSTR, LPCWSTR);
    static SetWindowThemeFn setTheme = []() -> SetWindowThemeFn {
        HMODULE h = LoadLibraryW(L"uxtheme.dll");
        if (!h) {
            return nullptr;
        }
        return reinterpret_cast<SetWindowThemeFn>(GetProcAddress(h, "SetWindowTheme"));
    }();
    if (setTheme) {
        setTheme(combo, L"", L"");
    }

    LONG_PTR ex = GetWindowLongPtrW(combo, GWL_EXSTYLE);
    if ((ex & WS_EX_CLIENTEDGE) != 0) {
        SetWindowLongPtrW(combo, GWL_EXSTYLE, ex & ~WS_EX_CLIENTEDGE);
        SetWindowPos(combo, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    LONG_PTR style = GetWindowLongPtrW(combo, GWL_STYLE);
    if ((style & WS_BORDER) != 0) {
        SetWindowLongPtrW(combo, GWL_STYLE, style & ~WS_BORDER);
        SetWindowPos(combo, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    SendMessageW(combo, CB_SETEXTENDEDUI, TRUE, 0);
}

void EnsureComboListTopMost(HWND combo) {
    if (!combo) {
        return;
    }
    COMBOBOXINFO info{};
    info.cbSize = sizeof(info);
    if (GetComboBoxInfo(combo, &info) && info.hwndList) {
        SetWindowPos(info.hwndList, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
}

std::wstring GetComboItemText(HWND combo, int itemIndex) {
    if (!combo || itemIndex < 0) {
        return {};
    }
    const LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, static_cast<WPARAM>(itemIndex), 0);
    if (length == CB_ERR || length < 0) {
        return {};
    }
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const LRESULT copied = SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(itemIndex),
                                        reinterpret_cast<LPARAM>(text.data()));
    if (copied == CB_ERR || copied < 0) {
        return {};
    }
    text.resize(static_cast<size_t>(copied));
    return text;
}

std::wstring GetComboSelectionOrWindowText(HWND combo, int selectionOverride) {
    if (!combo) {
        return {};
    }
    const int selection = selectionOverride >= 0
        ? selectionOverride
        : static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (selection >= 0) {
        return GetComboItemText(combo, selection);
    }

    const int length = GetWindowTextLengthW(combo);
    if (length <= 0) {
        return {};
    }
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(combo, text.data(), length + 1);
    if (copied <= 0) {
        return {};
    }
    text.resize(static_cast<size_t>(copied));
    return text;
}

std::wstring GetOwnerDrawComboText(HWND combo, bool comboFace, UINT controlId, UINT itemId,
    UINT openComboControlId, int openComboSelectionIndex) {
    if (!combo) {
        return {};
    }
    if (comboFace) {
        int selection = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
        if (openComboControlId == controlId && openComboSelectionIndex >= 0) {
            selection = openComboSelectionIndex;
        }
        if (selection < 0) {
            return {};
        }
        return GetComboItemText(combo, selection);
    }
    if (itemId == static_cast<UINT>(-1)) {
        return {};
    }
    return GetComboItemText(combo, static_cast<int>(itemId));
}

float ParseFloatOrFallback(const std::wstring& text, float fallback) {
    if (text.empty()) {
        return fallback;
    }
    wchar_t* end = nullptr;
    const float value = static_cast<float>(wcstod(text.c_str(), &end));
    return end == text.c_str() ? fallback : value;
}

HWND CreateTrackedTooltipWindow(HWND owner, HFONT font) {
    if (!owner || !IsWindow(owner)) {
        return nullptr;
    }
    HWND tooltip = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"STATIC",
        L"",
        WS_POPUP | SS_CENTER | SS_CENTERIMAGE | WS_BORDER,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr,
        nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE)),
        nullptr);
    if (!tooltip) {
        return nullptr;
    }
    if (font) {
        SendMessageW(tooltip, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    }
    SetWindowPos(tooltip, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW);
    return tooltip;
}

void HideTrackedTooltipWindow(HWND tooltip, bool& visible) {
    if (tooltip) {
        ShowWindow(tooltip, SW_HIDE);
    }
    visible = false;
}

void DestroyTrackedTooltipWindow(HWND& tooltip, bool& visible, std::wstring& textCache) {
    HideTrackedTooltipWindow(tooltip, visible);
    if (tooltip) {
        DestroyWindow(tooltip);
        tooltip = nullptr;
    }
    textCache.clear();
    visible = false;
}

void ShowTrackedTooltipWindow(HWND owner, HWND tooltip, HFONT font, const std::wstring& text,
    std::wstring& textCache, bool& visible, POINT screenPt) {
    if (!owner || !tooltip || text.empty()) {
        HideTrackedTooltipWindow(tooltip, visible);
        return;
    }

    if (textCache != text) {
        textCache = text;
        SetWindowTextW(tooltip, textCache.c_str());
    }

    RECT textRc{0, 0, 0, 0};
    if (HDC hdc = GetDC(tooltip)) {
        HFONT oldFont = nullptr;
        if (font) {
            oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, font));
        }
        DrawTextW(hdc, textCache.c_str(), -1, &textRc, DT_CALCRECT | DT_SINGLELINE);
        if (oldFont) {
            SelectObject(hdc, oldFont);
        }
        ReleaseDC(tooltip, hdc);
    }

    const int padX = 12;
    const int padY = 8;
    const int width = std::max(60, RectWidth(textRc) + padX * 2);
    const int height = std::max(24, RectHeight(textRc) + padY);
    int x = screenPt.x + 14;
    int y = screenPt.y + 24;

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    const HMONITOR mon = MonitorFromPoint(screenPt, MONITOR_DEFAULTTONEAREST);
    if (mon && GetMonitorInfoW(mon, &mi)) {
        x = std::clamp(x, static_cast<int>(mi.rcWork.left),
                       std::max(static_cast<int>(mi.rcWork.left), static_cast<int>(mi.rcWork.right - width)));
        y = std::clamp(y, static_cast<int>(mi.rcWork.top),
                       std::max(static_cast<int>(mi.rcWork.top), static_cast<int>(mi.rcWork.bottom - height)));
    }

    if (font) {
        SendMessageW(tooltip, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    }
    SetWindowPos(tooltip, HWND_TOPMOST, x, y, width, height,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
    visible = true;
}

HWND CreateChildControl(HWND parent, HINSTANCE hInstance, LPCWSTR className, LPCWSTR text,
    DWORD style, int controlId, DWORD exStyle) {
    return CreateWindowExW(
        exStyle,
        className,
        text,
        style,
        0, 0, 0, 0,
        parent,
        (controlId != 0) ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)) : nullptr,
        hInstance,
        nullptr);
}

HWND CreatePanel(HWND parent, HINSTANCE hInstance, bool visible) {
    DWORD style = WS_CHILD;
    if (visible) {
        style |= WS_VISIBLE;
    }
    return CreateChildControl(parent, hInstance, WC_STATICW, nullptr, style);
}

HWND CreateLabel(HWND parent, HINSTANCE hInstance, LPCWSTR text) {
    return CreateChildControl(parent, hInstance, WC_STATICW, text,
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE);
}

HWND CreateOwnerDrawButton(HWND parent, HINSTANCE hInstance, LPCWSTR text, int controlId) {
    return CreateChildControl(parent, hInstance, WC_BUTTONW, text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, controlId);
}

} // namespace UiUtil
