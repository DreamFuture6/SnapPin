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
    SetWindowRgn(hwnd, rgn, TRUE);
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
