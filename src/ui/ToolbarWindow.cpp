#include "ui/ToolbarWindow.h"
#include "app/AppIds.h"
#include "ui/GdiObject.h"
#include "ui/ThemeColors.h"

namespace
{
    constexpr int kToolbarHeight                = 36;
    constexpr wchar_t kToolbarWindowClassName[] = L"SnapPinToolbarClass";

    struct ButtonDef {
        UINT id;
        const wchar_t *iconText;
        const wchar_t *tooltipText;
        bool modeButton;
    };

    const ButtonDef kButtons[] = {
        {ID_TOOL_CURSOR, L"\u2316", L"\u5149\u6807", true},
        {ID_TOOL_RECT, L"\u25AD", L"\u77E9\u5F62", true},
        {ID_TOOL_ELLIPSE, L"\u25CB", L"\u692D\u5706", true},
        {ID_TOOL_ARROW, L"\u27A4", L"\u7BAD\u5934", true},
        {ID_TOOL_LINE, L"\u2571", L"\u76F4\u7EBF", true},
        {ID_TOOL_PEN, L"\u270E", L"\u753B\u7B14", true},
        {ID_TOOL_MOSAIC, L"\u25A6", L"\u9A6C\u8D5B\u514B", true},
        {ID_TOOL_TEXT, L"T", L"\u6587\u5B57", true},
        {ID_TOOL_NUMBER, L"\u2460", L"\u5E8F\u53F7", true},
        {ID_TOOL_ERASER, L"\u232B", L"\u6A61\u76AE\u64E6", true},
        {ID_TOOL_UNDO, L"\u21B6", L"\u64A4\u9500", false},
        {ID_TOOL_REDO, L"\u21B7", L"\u91CD\u505A", false},
        {ID_TOOL_LONG_CAPTURE, L"L", L"\u957F\u622A\u56FE", false},
        {ID_TOOL_OCR, L"\u2315", L"OCR", false},
        {ID_TOOL_WHITEBOARD, L"W", L"\u767D\u677F", false},
        {ID_TOOL_SAVE, L"\u2193", L"\u4FDD\u5B58", false},
        {ID_TOOL_COPY, L"\u2398", L"\u590D\u5236", false},
        {ID_TOOL_COPY_FILE, L"\u25A3", L"\u590D\u5236\u4E3A\u6587\u4EF6", false},
        {ID_TOOL_PIN, L"P", L"\u8D34\u56FE", false},
        {ID_TOOL_TRIM_ABOVE, L"\u2191", L"\u5411\u4E0A\u88C1\u526A", false},
        {ID_TOOL_TRIM_BELOW, L"\u2193", L"\u5411\u4E0B\u88C1\u526A", false},
        {ID_TOOL_CANCEL, L"\u2715", L"\u53D6\u6D88", false},
    };

    const ButtonDef *FindButtonDef(UINT id)
    {
        for (const auto &b : kButtons) {
            if (b.id == id) {
                return &b;
            }
        }
        return nullptr;
    }

    float ParseFloatText(const std::wstring &text, float fallback)
    {
        if (text.empty()) {
            return fallback;
        }
        wchar_t *end      = nullptr;
        const float value = static_cast<float>(wcstod(text.c_str(), &end));
        if (end == text.c_str()) {
            return fallback;
        }
        return value;
    }

    float ComboFloatValue(HWND combo, float fallback)
    {
        if (!combo) {
            return fallback;
        }
        int idx = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
        wchar_t text[64]{};
        if (idx >= 0) {
            SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(idx), reinterpret_cast<LPARAM>(text));
            return ParseFloatText(text, fallback);
        }
        GetWindowTextW(combo, text, static_cast<int>(std::size(text)));
        return ParseFloatText(text, fallback);
    }

    void ConfigureComboControl(HWND combo, int controlHeight, int visibleItems)
    {
        if (!combo) {
            return;
        }

        const int itemHeight = std::max(14, controlHeight - 8);
        SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), static_cast<LPARAM>(itemHeight));
        SendMessageW(combo, CB_SETITEMHEIGHT, 0, static_cast<LPARAM>(itemHeight));
        SendMessageW(combo, CB_SETMINVISIBLE, static_cast<WPARAM>(visibleItems), 0);
    }

    void EnsureDropListTopMost(HWND combo)
    {
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

    void ApplyStableComboTheme(HWND combo)
    {
        if (!combo) {
            return;
        }
        using SetWindowThemeFn           = HRESULT(WINAPI *)(HWND, LPCWSTR, LPCWSTR);
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

    void RegisterToolbarWindowClassOnce(std::once_flag &once, HINSTANCE hInstance, WNDPROC proc)
    {
        std::call_once(once, [hInstance, proc]() {
            WNDCLASSW wc{};
            wc.lpfnWndProc   = proc;
            wc.hInstance     = hInstance;
            wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
            wc.lpszClassName = kToolbarWindowClassName;
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            RegisterClassW(&wc);
        });
    }
}

LRESULT CALLBACK ToolbarWindow::ToolbarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ToolbarWindow *self = reinterpret_cast<ToolbarWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
        self     = reinterpret_cast<ToolbarWindow *>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    switch (msg) {
    case WM_COMMAND: {
        if (self && LOWORD(wParam) == ID_TOOL_FILL_ENABLE && HIWORD(wParam) == BN_CLICKED) {
            self->fillEnabledValue_ = !self->fillEnabledValue_;
            if (self->chkFill_) {
                InvalidateRect(self->chkFill_, nullptr, FALSE);
            }
        }
        if (HIWORD(wParam) == CBN_DROPDOWN) {
            EnsureDropListTopMost(reinterpret_cast<HWND>(lParam));
        }
        HWND owner = GetWindow(hwnd, GW_OWNER);
        if (owner) {
            SendMessageW(owner, WM_COMMAND, wParam, lParam);
        }
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
        if (self) {
            HWND ctrl = reinterpret_cast<HWND>(lParam);
            if (self->IsComboRelatedControl(ctrl)) {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                if (!self->comboBgBrush_) {
                    self->comboBgBrush_ = CreateSolidBrush(ThemeColors::Component::Toolbar::ComboBoxBgColor);
                }
                SetBkMode(hdc, OPAQUE);
                SetBkColor(hdc, ThemeColors::Component::Toolbar::ComboBoxBgColor);
                SetTextColor(hdc, ThemeColors::Component::Toolbar::ComboBoxTextColor);
                return reinterpret_cast<LRESULT>(self->comboBgBrush_);
            }
        }
        break;
    case WM_MOUSEMOVE:
        if (self) {
            self->UpdateHoverState();
        }
        break;
    case WM_TIMER:
        if (self && wParam == 1) {
            self->UpdateHoverState();
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (self && LOWORD(lParam) == HTCLIENT) {
            POINT cp{};
            GetCursorPos(&cp);
            ScreenToClient(hwnd, &cp);
            HWND hoverWnd = ChildWindowFromPointEx(hwnd, cp, CWP_SKIPDISABLED | CWP_SKIPINVISIBLE);
            if (!hoverWnd || hoverWnd == hwnd) {
                hoverWnd = reinterpret_cast<HWND>(wParam);
            }
            const UINT hoverId = hoverWnd ? static_cast<UINT>(GetDlgCtrlID(hoverWnd)) : 0;
            if (self->IsButtonLikeControlId(hoverId)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
            } else {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            }
            return TRUE;
        }
        break;
    case WM_SHOWWINDOW:
        if (self) {
            if (wParam) {
                SetTimer(hwnd, 1, 10, nullptr);
                self->UpdateHoverState();
            } else {
                KillTimer(hwnd, 1);
                self->hoveredControlId_ = 0;
                self->pendingHoverId_   = 0;
                self->HideTrackedTooltip();
            }
        }
        break;
    case WM_MEASUREITEM:
        if (self) {
            auto *mi = reinterpret_cast<MEASUREITEMSTRUCT *>(lParam);
            if (mi && (mi->CtlID == ID_TOOL_STROKE_WIDTH || mi->CtlID == ID_TOOL_TEXT_SIZE || mi->CtlID == ID_TOOL_TEXT_STYLE)) {
                const float s  = static_cast<float>(self->dpi_) / 96.0f;
                mi->itemHeight = static_cast<UINT>(std::max(18, static_cast<int>(std::round(24.0f * s))));
                return TRUE;
            }
        }
        break;
    case WM_DRAWITEM:
        if (self) {
            self->DrawButton(reinterpret_cast<const DRAWITEMSTRUCT *>(lParam));
            return TRUE;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        if (self) {
            self->PaintBackground(hdc);
        } else {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            UiGdi::ScopedGdiObject<HBRUSH> brush(CreateSolidBrush(RGB(26, 30, 36)));
            FillRect(hdc, &rc, brush.Get());
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ToolbarWindow::IsButtonLikeControlId(UINT id) const
{
    if (buttons_.count(id) > 0) {
        return true;
    }
    return id == ID_TOOL_STROKE_COLOR || id == ID_TOOL_FILL_ENABLE ||
           id == ID_TOOL_FILL_COLOR || id == ID_TOOL_TEXT_COLOR;
}

bool ToolbarWindow::IsComboControl(HWND hwnd) const
{
    if (!hwnd) {
        return false;
    }
    return hwnd == cmbStroke_ || hwnd == cmbTextSize_ || hwnd == cmbTextStyle_;
}

bool ToolbarWindow::IsComboList(HWND hwnd) const
{
    if (!hwnd) {
        return false;
    }
    auto matchList = [&](HWND combo) -> bool {
        if (!combo) {
            return false;
        }
        COMBOBOXINFO info{};
        info.cbSize = sizeof(info);
        return GetComboBoxInfo(combo, &info) && info.hwndList && info.hwndList == hwnd;
    };
    return matchList(cmbStroke_) || matchList(cmbTextSize_) || matchList(cmbTextStyle_);
}

bool ToolbarWindow::IsComboRelatedControl(HWND hwnd) const
{
    if (IsComboControl(hwnd) || IsComboList(hwnd)) {
        return true;
    }
    HWND parent = hwnd ? GetParent(hwnd) : nullptr;
    return IsComboControl(parent);
}

HWND ToolbarWindow::ControlById(UINT id) const
{
    if (buttons_.count(id) > 0) {
        return buttons_.at(id);
    }
    switch (id) {
    case ID_TOOL_STROKE_WIDTH:
        return cmbStroke_;
    case ID_TOOL_STROKE_COLOR:
        return btnStrokeColor_;
    case ID_TOOL_FILL_ENABLE:
        return chkFill_;
    case ID_TOOL_FILL_COLOR:
        return btnFillColor_;
    case ID_TOOL_TEXT_SIZE:
        return cmbTextSize_;
    case ID_TOOL_TEXT_STYLE:
        return cmbTextStyle_;
    case ID_TOOL_TEXT_COLOR:
        return btnTextColor_;
    default:
        return nullptr;
    }
}

void ToolbarWindow::HideTrackedTooltip()
{
    if (tooltip_) {
        ShowWindow(tooltip_, SW_HIDE);
    }
    trackedTooltipVisible_ = false;
}

void ToolbarWindow::ActivateTrackedTooltip(UINT id, POINT screenPt)
{
    if (!tooltip_) {
        return;
    }
    const std::wstring text = TooltipForControlId(id);
    if (text.empty()) {
        HideTrackedTooltip();
        return;
    }

    if (trackedTooltipText_ != text) {
        trackedTooltipText_ = text;
        SetWindowTextW(tooltip_, trackedTooltipText_.c_str());
    }

    HDC hdc = GetDC(tooltip_);
    RECT textRc{0, 0, 0, 0};
    if (hdc) {
        UiGdi::ScopedSelectObject selectedFont(hdc, font_);
        DrawTextW(hdc, trackedTooltipText_.c_str(), -1, &textRc, DT_CALCRECT | DT_SINGLELINE);
        ReleaseDC(hwnd_, hdc);
    }

    const int padX = 12;
    const int padY = 8;
    int width      = std::max(60, RectWidth(textRc) + padX * 2);
    int height     = std::max(24, RectHeight(textRc) + padY);
    int x          = screenPt.x + 14;
    int y          = screenPt.y + 24;

    MONITORINFO mi{};
    mi.cbSize    = sizeof(mi);
    HMONITOR mon = MonitorFromPoint(screenPt, MONITOR_DEFAULTTONEAREST);
    if (mon && GetMonitorInfoW(mon, &mi)) {
        x = std::clamp(x, static_cast<int>(mi.rcWork.left),
                       std::max(static_cast<int>(mi.rcWork.left), static_cast<int>(mi.rcWork.right - width)));
        y = std::clamp(y, static_cast<int>(mi.rcWork.top),
                       std::max(static_cast<int>(mi.rcWork.top), static_cast<int>(mi.rcWork.bottom - height)));
    }

    if (font_) {
        SendMessageW(tooltip_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    }
    SetWindowPos(tooltip_, HWND_TOPMOST, x, y, width, height,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
    trackedTooltipVisible_ = true;
}

void ToolbarWindow::UpdateHoverState()
{
    if (!hwnd_ || !IsWindowVisible(hwnd_)) {
        hoveredControlId_ = 0;
        pendingHoverId_   = 0;
        HideTrackedTooltip();
        return;
    }

    POINT sp{};
    GetCursorPos(&sp);
    HWND hit   = WindowFromPoint(sp);
    UINT hitId = 0;
    if (hit == tooltip_) {
        hitId = hoveredControlId_;
    }
    if (hit && (hit == hwnd_ || IsChild(hwnd_, hit))) {
        HWND ctrl = hit;
        if (ctrl == hwnd_) {
            POINT cp = sp;
            ScreenToClient(hwnd_, &cp);
            ctrl = ChildWindowFromPointEx(hwnd_, cp, CWP_SKIPDISABLED | CWP_SKIPINVISIBLE);
            if (ctrl == hwnd_) {
                ctrl = nullptr;
            }
        }
        if (ctrl && IsChild(hwnd_, ctrl)) {
            hitId = static_cast<UINT>(GetDlgCtrlID(ctrl));
        }
    }

    if (hoveredControlId_ != hitId) {
        const UINT prev   = hoveredControlId_;
        hoveredControlId_ = hitId;
        if (HWND prevCtrl = ControlById(prev)) {
            RedrawWindow(prevCtrl, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
        }
        if (HWND curCtrl = ControlById(hitId)) {
            RedrawWindow(curCtrl, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE | RDW_UPDATENOW);
        }
    }

    if (hitId == 0 || TooltipForControlId(hitId).empty()) {
        pendingHoverId_ = 0;
        HideTrackedTooltip();
        return;
    }

    const DWORD now = GetTickCount();
    if (pendingHoverId_ != hitId) {
        pendingHoverId_ = hitId;
        hoverSinceTick_ = now;
        HideTrackedTooltip();
        return;
    }

    const DWORD elapsed = now - hoverSinceTick_;
    if (elapsed >= 800 || trackedTooltipVisible_) {
        ActivateTrackedTooltip(hitId, sp);
    }
}

Gdiplus::RectF InsetRectF(const RECT &rc, float pad)
{
    const float l = static_cast<float>(rc.left) + pad;
    const float t = static_cast<float>(rc.top) + pad;
    const float r = static_cast<float>(rc.right) - pad;
    const float b = static_cast<float>(rc.bottom) - pad;
    return Gdiplus::RectF(l, t, std::max(1.0f, r - l), std::max(1.0f, b - t));
}

bool DrawToolbarIcon(UINT id, HDC hdc, const RECT &rc, COLORREF fg, float dpiScale)
{
    if (!hdc) {
        return false;
    }

    Gdiplus::Graphics g(hdc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    const float pad        = std::max(6.0f, 6.5f * dpiScale);
    const Gdiplus::RectF r = InsetRectF(rc, pad);
    const float l          = r.X;
    const float t          = r.Y;
    const float w          = r.Width;
    const float h          = r.Height;
    const float rr         = l + w;
    const float bb         = t + h;
    const float cx         = l + w * 0.5f;
    const float cy         = t + h * 0.5f;
    const float stroke     = std::max(1.5f, 1.7f * dpiScale);
    Gdiplus::Color color(255, GetRValue(fg), GetGValue(fg), GetBValue(fg));
    Gdiplus::Pen thickPen(color, stroke);
    Gdiplus::Pen thinPen(color, stroke * 0.8f);
    thickPen.SetLineJoin(Gdiplus::LineJoinRound);
    thickPen.SetStartCap(Gdiplus::LineCapRound);
    thickPen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::SolidBrush brush(color);

    const float side = std::min(w, h);
    const float ox   = cx - side * 0.5f;
    const float oy   = cy - side * 0.5f;
    const float s    = side / 24.0f;

    auto X      = [&](float x) -> float { return ox + x * s; };
    auto Y      = [&](float y) -> float { return oy + y * s; };
    auto Pt24   = [&](float x, float y) -> Gdiplus::PointF { return {X(x), Y(y)}; };
    auto Rect24 = [&](float x, float y, float ww, float hh) -> Gdiplus::RectF { return {X(x), Y(y), ww * s, hh * s}; };

    auto DrawRect = [&](Gdiplus::Pen &pen, const Gdiplus::RectF &rr) {
        g.DrawRectangle(&pen, rr.X, rr.Y, rr.Width, rr.Height);
    };
    auto FillRect = [&](Gdiplus::Brush &brushRef, const Gdiplus::RectF &rr) {
        g.FillRectangle(&brushRef, rr.X, rr.Y, rr.Width, rr.Height);
    };
    auto DrawEllipse = [&](Gdiplus::Pen &pen, const Gdiplus::RectF &rr) {
        g.DrawEllipse(&pen, rr.X, rr.Y, rr.Width, rr.Height);
    };
    auto FillEllipse = [&](Gdiplus::Brush &brushRef, const Gdiplus::RectF &rr) {
        g.FillEllipse(&brushRef, rr.X, rr.Y, rr.Width, rr.Height);
    };
    // Compatibility aliases: keep existing icon geometry call sites readable while using unified primitives above.
    auto P         = [&](float x, float y) -> Gdiplus::PointF { return Pt24(x, y); };
    auto R24       = [&](float x, float y, float ww, float hh) -> Gdiplus::RectF { return Rect24(x, y, ww, hh); };
    auto DrawRectF = [&](const Gdiplus::RectF &rr) { DrawRect(thickPen, rr); };
    auto FillRectF = [&](Gdiplus::Brush *b, const Gdiplus::RectF &rr) {
        if (b) {
            g.FillRectangle(b, rr.X, rr.Y, rr.Width, rr.Height);
        }
    };
    auto DrawEllipseF = [&](const Gdiplus::RectF &rr) { DrawEllipse(thickPen, rr); };
    auto FillEllipseF = [&](Gdiplus::Brush *b, const Gdiplus::RectF &rr) {
        if (b) {
            g.FillEllipse(b, rr.X, rr.Y, rr.Width, rr.Height);
        }
    };

    auto MakeRoundRectPath = [&](const Gdiplus::RectF &rr, float radiusPx, Gdiplus::GraphicsPath &path) {
        const float d  = radiusPx * 2.0f;
        const float x  = rr.X;
        const float y  = rr.Y;
        const float w0 = rr.Width;
        const float h0 = rr.Height;

        path.Reset();
        path.AddArc(x, y, d, d, 180.0f, 90.0f);
        path.AddArc(x + w0 - d, y, d, d, 270.0f, 90.0f);
        path.AddArc(x + w0 - d, y + h0 - d, d, d, 0.0f, 90.0f);
        path.AddArc(x, y + h0 - d, d, d, 90.0f, 90.0f);
        path.CloseFigure();
    };

    Gdiplus::SolidBrush accentBrush(Gdiplus::Color(140, GetRValue(fg), GetGValue(fg), GetBValue(fg)));

    const Gdiplus::RectF iconRc(ox, oy, side, side);

    switch (id) {
    case ID_TOOL_CURSOR: {
        Gdiplus::PointF pts[] = {
            P(4.8f, 2.2f),
            P(4.8f, 21.2f),
            P(9.8f, 16.4f),
            P(11.6f, 22.0f),
            P(14.8f, 20.8f),
            P(13.0f, 15.0f),
            P(21.2f, 15.0f)};
        g.FillPolygon(&accentBrush, pts, (INT)std::size(pts));
        g.DrawPolygon(&thickPen, pts, (INT)std::size(pts));
        return true;
    }

    case ID_TOOL_RECT: {
        const auto rr0 = R24(3.2f, 3.2f, 17.6f, 17.6f);
        DrawRectF(rr0);

        const float hs = 3.0f;
        FillRectF(&accentBrush, R24(2.4f, 2.4f, hs, hs));
        FillRectF(&accentBrush, R24(18.6f, 2.4f, hs, hs));
        FillRectF(&accentBrush, R24(2.4f, 18.6f, hs, hs));
        FillRectF(&accentBrush, R24(18.6f, 18.6f, hs, hs));
        return true;
    }

    case ID_TOOL_ELLIPSE: {
        const auto rr0 = R24(3.2f, 3.2f, 17.6f, 17.6f);
        DrawEllipseF(rr0);
        return true;
    }

    case ID_TOOL_LINE: {
        g.DrawLine(&thinPen, P(5.0f, 19.0f), P(19.0f, 5.0f));

        const float rDot = 2.5f;
        FillEllipseF(&brush, R24(5.0f - rDot, 19.0f - rDot, 2.0f * rDot, 2.0f * rDot));
        FillEllipseF(&brush, R24(19.0f - rDot, 5.0f - rDot, 2.0f * rDot, 2.0f * rDot));
        return true;
    }

    case ID_TOOL_ARROW: {
        const auto b = P(20.0f, 4.0f);
        g.DrawLine(&thickPen, P(4.0f, 20.0f), b);
        g.DrawLine(&thickPen, b, P(10.2f, 5.0f));
        g.DrawLine(&thickPen, b, P(19.0f, 13.8f));
        return true;
    }

    case ID_TOOL_PEN: {
        Gdiplus::PointF pts[] = {
            {l + w * 0.08f, bb - h * 0.18f},
            {l + w * 0.36f, t + h * 0.18f},
            {l + w * 0.66f, t + h * 0.52f},
            {rr - w * 0.10f, t + h * 0.18f}};
        g.DrawCurve(&thickPen, pts, static_cast<INT>(std::size(pts)), 0.42f);
        return true;
    }

    case ID_TOOL_MOSAIC: {
        const auto outer = R24(2.0f, 2.0f, 20.0f, 20.0f);
        Gdiplus::GraphicsPath path;
        MakeRoundRectPath(outer, 3.6f * s, path);
        g.FillPath(&accentBrush, &path);
        g.DrawPath(&thickPen, &path);
        FillRectF(&brush, R24(10.0f, 5.2f, 4.8f, 4.8f));
        FillRectF(&brush, R24(5.2f, 10.0f, 4.8f, 4.8f));
        FillRectF(&brush, R24(14.0f, 10.0f, 4.8f, 4.8f));
        FillRectF(&brush, R24(10.0f, 14.0f, 4.8f, 4.8f));
        return true;
    }

    case ID_TOOL_TEXT: {
        g.DrawLine(&thickPen, P(5.0f, 5.0f), P(19.0f, 5.0f));
        g.DrawLine(&thickPen, P(12.0f, 7.0f), P(12.0f, 20.0f));
        return true;
    }

    case ID_TOOL_NUMBER: {
        const auto rr0 = R24(2.0f, 2.0f, 20.0f, 20.0f);
        DrawEllipseF(rr0);
        Gdiplus::FontFamily ff(L"Segoe UI");
        const float fontPx = std::max(12.0f, side * 0.70f);
        Gdiplus::Font font(&ff, fontPx, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::StringFormat fmt;
        fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
        fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(L"1", -1, &font, Gdiplus::RectF(iconRc.X + 0.4f, iconRc.Y, iconRc.Width, iconRc.Height), &fmt, &brush);
        return true;
    }

    case ID_TOOL_ERASER: {
        Gdiplus::PointF body[] = {
            P(19.8f, 10.0f),
            P(12.8f, 3.0f),
            P(2.4f, 13.4f),
            P(9.4f, 20.4f)};
        g.DrawPolygon(&thickPen, body, (INT)std::size(body));
        Gdiplus::PointF bodyHead[] = {
            P(12.9f, 16.1f),
            P(7.3f, 10.5f),
            P(2.4f, 13.4f),
            P(9.4f, 20.4f)};
        g.FillPolygon(&accentBrush, bodyHead, (INT)std::size(bodyHead));
        g.DrawLine(&thinPen, P(12.9f, 16.1f), P(7.3f, 10.5f));
        g.DrawLine(&thinPen, P(23.0f, 22.2f), P(1.0f, 22.2f));
        return true;
    }

    case ID_TOOL_UNDO: {
        const auto start = P(20.9f, 15.8f);
        const auto end   = P(3.5f, 15.0f);
        g.DrawBezier(&thickPen, start, P(17.2f, 4.9f), P(7.2f, 4.9f), end);
        g.DrawLine(&thickPen, end, P(9.4f, 15.0f));
        g.DrawLine(&thickPen, end, P(3.5f, 8.2f));
        return true;
    }

    case ID_TOOL_REDO: {
        const auto start = P(3.1f, 15.8f);
        const auto end   = P(20.5f, 15.0f);
        g.DrawBezier(&thickPen, start, P(6.8f, 4.9f), P(16.8f, 4.9f), end);
        g.DrawLine(&thickPen, end, P(14.6f, 15.0f));
        g.DrawLine(&thickPen, end, P(20.5f, 8.2f));
        return true;
    }

    case ID_TOOL_SAVE: {
        g.DrawLine(&thickPen, P(12.0f, 1.8f), P(12.0f, 15.5f));
        g.DrawLine(&thickPen, P(12.0f, 15.5f), P(8.0f, 11.5f));
        g.DrawLine(&thickPen, P(12.0f, 15.5f), P(16.0f, 11.5f));
        g.DrawLine(&thickPen, P(2.5f, 15.0f), P(2.5f, 21.2f));
        g.DrawLine(&thickPen, P(2.5f, 21.2f), P(21.5f, 21.2f));
        g.DrawLine(&thickPen, P(21.5f, 21.2f), P(21.5f, 15.0f));
        return true;
    }

    case ID_TOOL_COPY: {
        DrawRectF(R24(3.8f, 7.8f, 13.2f, 13.2f));
        g.DrawLine(&thickPen, P(7.5f, 3.0f), P(7.5f, 7.8f));
        g.DrawLine(&thickPen, P(7.5f, 3.0f), P(20.7f, 3.0f));
        g.DrawLine(&thickPen, P(20.7f, 3.0f), P(20.7f, 16.2f));
        g.DrawLine(&thickPen, P(20.7f, 16.2f), P(17.0f, 16.2f));
        return true;
    }

    case ID_TOOL_COPY_FILE: {
        const Gdiplus::RectF rcFront = R24(2.2f, 6.2f, 15.6f, 15.6f);
        const Gdiplus::RectF rcBack  = R24(6.2f, 2.2f, 15.6f, 15.6f);
        const float radiusPx         = 2.8f * s;
        Gdiplus::GraphicsPath pathFront, pathBack;
        MakeRoundRectPath(rcFront, radiusPx, pathFront);
        MakeRoundRectPath(rcBack, radiusPx, pathBack);
        {
            const Gdiplus::RectF clipRc = R24(0.0f, 0.0f, 24.0f, 24.0f);
            Gdiplus::Region region(clipRc);
            region.Exclude(&pathFront);
            const Gdiplus::GraphicsState st = g.Save();
            g.SetClip(&region, Gdiplus::CombineModeReplace);
            g.DrawPath(&thickPen, &pathBack);
            g.Restore(st);
        }
        g.DrawPath(&thickPen, &pathFront);
        g.DrawEllipse(&thinPen, R24(12.6f, 9.0f, 2.6f, 2.6f));
        Gdiplus::PointF m[5] = {
            P(4.0f, 19.0f),
            P(8.1f, 15.2f),
            P(10.7f, 17.4f),
            P(13.0f, 15.8f),
            P(16.8f, 19.2f),
        };
        g.DrawLines(&thinPen, m, 5);

        return true;
    }

    case ID_TOOL_PIN: {
        Gdiplus::GraphicsPath path;
        path.StartFigure();
        path.AddLine(P(2.5f, 21.7f), P(4.1f, 21.5f));
        path.AddLine(P(4.1f, 21.5f), P(10.3f, 16.6f));
        path.AddLine(P(10.3f, 16.6f), P(14.3f, 19.8f));
        path.AddLine(P(14.3f, 19.8f), P(16.1f, 18.5f));
        path.AddLine(P(16.1f, 18.5f), P(16.4f, 13.6f));
        path.AddLine(P(16.4f, 13.6f), P(19.1f, 9.3f));
        path.AddLine(P(19.1f, 9.3f), P(20.9f, 9.6f));
        path.AddLine(P(20.9f, 9.6f), P(21.7f, 8.2f));
        path.AddLine(P(21.7f, 8.2f), P(15.9f, 2.0f));
        path.AddLine(P(15.9f, 2.0f), P(14.8f, 2.0f));
        path.AddLine(P(14.8f, 2.0f), P(14.0f, 4.7f));
        path.AddLine(P(14.0f, 4.7f), P(10.8f, 6.9f));
        path.AddLine(P(10.8f, 6.9f), P(5.7f, 7.1f));
        path.AddLine(P(5.7f, 7.1f), P(4.1f, 8.5f));
        path.AddLine(P(4.1f, 8.5f), P(6.8f, 13.6f));
        path.AddLine(P(6.8f, 13.6f), P(2.0f, 20.4f));
        path.CloseFigure();
        g.DrawPath(&thickPen, &path);
        return true;
    }

    case ID_TOOL_LONG_CAPTURE: {
        thickPen.SetLineJoin(Gdiplus::LineJoinRound);
        thickPen.SetStartCap(Gdiplus::LineCapRound);
        thickPen.SetEndCap(Gdiplus::LineCapRound);
        g.DrawLine(&thickPen, P(4.5f, 0.8f), P(19.5f, 0.8f));
        g.DrawArc(&thickPen, R24(1.5f, 0.8f, 6.0f, 6.0f), 180.0f, 90.0f);
        g.DrawArc(&thickPen, R24(16.5f, 0.8f, 6.0f, 6.0f), 270.0f, 90.0f);
        g.DrawLine(&thickPen, P(1.5f, 3.8f), P(1.5f, 5.6f));
        g.DrawLine(&thickPen, P(22.5f, 3.8f), P(22.5f, 5.6f));
        g.DrawLine(&thickPen, P(1.5f, 6.4f), P(1.5f, 11.4f));
        g.DrawLine(&thickPen, P(22.5f, 6.4f), P(22.5f, 11.4f));
        g.DrawEllipse(&thinPen, R24(2.5f, 19.4f, 5.0f, 5.0f));
        g.DrawEllipse(&thinPen, R24(16.5f, 19.4f, 5.0f, 5.0f));
        g.DrawLine(&thinPen, P(6.6f, 23.4f), P(17.0f, 13.4f));
        g.DrawLine(&thinPen, P(17.4f, 23.4f), P(7.0f, 13.4f));
        return true;
    }
    case ID_TOOL_OCR: {
        g.DrawArc(&thickPen, R24(0.0f, 0.0f, 5.0f, 5.0f), 180.0f, 90.0f);
        g.DrawArc(&thickPen, R24(19.0f, 0.0f, 5.0f, 5.0f), 270.0f, 90.0f);
        g.DrawArc(&thickPen, R24(19.0f, 19.0f, 5.0f, 5.0f), 0.0f, 90.0f);
        g.DrawArc(&thickPen, R24(0.0f, 19.0f, 5.0f, 5.0f), 90.0f, 90.0f);

        g.DrawLine(&thickPen, P(2.5f, 0.0f), P(7.0f, 0.0f));
        g.DrawLine(&thickPen, P(16.0f, 0.0f), P(21.5f, 0.0f));
        g.DrawLine(&thickPen, P(2.5f, 24.0f), P(7.0f, 24.0f));
        g.DrawLine(&thickPen, P(16.0f, 24.0f), P(21.5f, 24.0f));
        g.DrawLine(&thickPen, P(0.0f, 2.5f), P(0.0f, 7.0f));
        g.DrawLine(&thickPen, P(0.0f, 16.0f), P(0.0f, 21.5f));
        g.DrawLine(&thickPen, P(24.0f, 2.5f), P(24.0f, 7.0f));
        g.DrawLine(&thickPen, P(24.0f, 16.0f), P(24.0f, 21.5f));

        g.DrawLine(&thinPen, P(6.0f, 7.5f), P(18.0f, 7.5f));
        g.DrawLine(&thinPen, P(12.0f, 7.5f), P(12.0f, 19.0f));
        return true;
    }
    case ID_TOOL_WHITEBOARD: {
        DrawRectF(R24(2.5f, 2.8f, 19.0f, 13.2f));
        FillRectF(&accentBrush, R24(5.1f, 5.7f, 13.8f, 7.4f));
        g.DrawLine(&thinPen, P(9.5f, 16.0f), P(5.0f, 22.0f));
        g.DrawLine(&thinPen, P(14.5f, 16.0f), P(19.0f, 22.0f));
        return true;
    }
    case ID_TOOL_TRIM_ABOVE: {
        DrawRectF(R24(1.5f, 0.5f, 21.0f, 23.0f));
        g.DrawLine(&thickPen, P(1.5f, 12.0f), P(22.5f, 12.0f));
        g.DrawLine(&thinPen, P(6.0f, 3.5f), P(18.0f, 9.0f));
        g.DrawLine(&thinPen, P(6.0f, 9.0f), P(18.0f, 3.5f));
        return true;
    }
    case ID_TOOL_TRIM_BELOW: {
        DrawRectF(R24(1.5f, 0.5f, 21.0f, 23.0f));
        g.DrawLine(&thickPen, P(1.5f, 12.0f), P(22.5f, 12.0f));
        g.DrawLine(&thinPen, P(6.0f, 15.0f), P(18.0f, 20.5f));
        g.DrawLine(&thinPen, P(6.0f, 20.5f), P(18.0f, 15.0f));
        return true;
    }

    case ID_TOOL_CANCEL: {
        g.DrawLine(&thickPen, P(4.5f, 4.5f), P(19.5f, 19.5f));
        g.DrawLine(&thickPen, P(19.5f, 4.5f), P(4.5f, 19.5f));
        return true;
    }

    default:
        return false;
    }
}

void ToolbarWindow::PreloadClass(HINSTANCE hInstance)
{
    static std::once_flag once;
    RegisterToolbarWindowClassOnce(once, hInstance, ToolbarWindow::ToolbarProc);
}

void ToolbarWindow::WarmUp(HWND parent, HINSTANCE hInstance)
{
    static std::once_flag once;
    std::call_once(once, [parent, hInstance]() {
        ToolbarWindow warmup;
        if (warmup.Create(parent, hInstance)) {
            warmup.Destroy();
        }
    });
}

bool ToolbarWindow::Create(HWND parent, HINSTANCE hInstance)
{
    parent_ = parent;

    PreloadClass(hInstance);

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kToolbarWindowClassName,
        L"",
        WS_POPUP,
        0, 0, 920, kToolbarHeight,
        parent,
        nullptr,
        hInstance,
        this);

    if (!hwnd_) {
        return false;
    }

    if (!comboBgBrush_) {
        comboBgBrush_ = CreateSolidBrush(ThemeColors::Component::Toolbar::ComboBoxBgColor);
    }
    dpi_ = GetDpiForWindow(parent_) ? GetDpiForWindow(parent_) : 96;
    CreateButtons();
    UpdateDpiFont();
    SetActiveTool(ToolType::None);
    return true;
}

std::wstring ToolbarWindow::TooltipForControlId(UINT id) const
{
    if (const ButtonDef *def = FindButtonDef(id)) {
        return def->tooltipText;
    }
    switch (id) {
    case ID_TOOL_STROKE_WIDTH:
        return L"\u7EBF\u5BBD";
    case ID_TOOL_STROKE_COLOR:
        return L"\u7EBF\u6761\u989C\u8272";
    case ID_TOOL_FILL_ENABLE:
        return L"\u586B\u5145\u5F00\u5173";
    case ID_TOOL_FILL_COLOR:
        return L"\u586B\u5145\u989C\u8272";
    case ID_TOOL_TEXT_SIZE:
        return L"\u6587\u5B57\u5927\u5C0F";
    case ID_TOOL_TEXT_STYLE:
        return L"\u6587\u5B57\u6837\u5F0F";
    case ID_TOOL_TEXT_COLOR:
        return L"\u6587\u5B57\u989C\u8272";
    default:
        return L"";
    }
}

void ToolbarWindow::CreateTooltips()
{
    DestroyTooltips();
    if (!hwnd_) {
        return;
    }

    tooltip_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"STATIC",
        L"",
        WS_POPUP | SS_CENTER | SS_CENTERIMAGE | WS_BORDER,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr,
        nullptr,
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE)),
        nullptr);
    if (!tooltip_) {
        return;
    }

    if (font_) {
        SendMessageW(tooltip_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    }
    SetWindowPos(tooltip_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_HIDEWINDOW);
    trackedTooltipVisible_ = false;
    trackedTooltipText_.clear();
}

void ToolbarWindow::DestroyTooltips()
{
    HideTrackedTooltip();
    if (tooltip_) {
        DestroyWindow(tooltip_);
        tooltip_ = nullptr;
    }
    trackedTooltipText_.clear();
    trackedTooltipVisible_ = false;
}

void ToolbarWindow::CreateButtons()
{
    buttons_.clear();
    buttonWidths_.clear();
    modeButtonIds_.clear();
    actionButtonIds_.clear();
    separatorXs_.clear();

    const float s     = static_cast<float>(dpi_) / 96.0f;
    const int btnH    = std::max(24, static_cast<int>(std::round(28.0f * s)));
    comboPopupHeight_ = std::max(btnH * 8, static_cast<int>(std::round(220.0f * s)));

    for (const auto &b : kButtons) {
        const int btnW = btnH;
        HWND btn       = CreateWindowW(
            L"BUTTON", b.iconText,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            0, 0, btnW, btnH,
            hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(b.id)), nullptr, nullptr);
        buttons_[b.id]      = btn;
        buttonWidths_[b.id] = btnW;
        if (b.modeButton) {
            modeButtonIds_.push_back(b.id);
        } else {
            actionButtonIds_.push_back(b.id);
        }
    }

    const int comboW     = std::max(56, static_cast<int>(std::round(62.0f * s)));
    const int colorBtnW  = btnH;
    const int checkW     = btnH;
    const int textStyleW = std::max(74, static_cast<int>(std::round(86.0f * s)));

    cmbStroke_ = CreateWindowW(WC_COMBOBOXW, L"",
                               WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
                               0, 0, comboW, comboPopupHeight_,
                               hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOOL_STROKE_WIDTH)), nullptr, nullptr);
    ApplyStableComboTheme(cmbStroke_);
    const std::array<const wchar_t *, 10> strokeValues = {L"1", L"2", L"3", L"4", L"6", L"8", L"12", L"16", L"24", L"32"};
    int strokeSelect                                   = 0;
    for (size_t i = 0; i < strokeValues.size(); ++i) {
        SendMessageW(cmbStroke_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(strokeValues[i]));
        if (std::fabs(ParseFloatText(strokeValues[i], strokeWidthValue_) - strokeWidthValue_) < 0.01f) {
            strokeSelect = static_cast<int>(i);
        }
    }
    SendMessageW(cmbStroke_, CB_SETCURSEL, strokeSelect, 0);

    btnStrokeColor_ = CreateWindowW(L"BUTTON", L"",
                                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                    0, 0, colorBtnW, btnH,
                                    hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOOL_STROKE_COLOR)), nullptr, nullptr);

    chkFill_ = CreateWindowW(L"BUTTON", L"\u25a0",
                             WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                             0, 0, checkW, btnH,
                             hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOOL_FILL_ENABLE)), nullptr, nullptr);

    btnFillColor_ = CreateWindowW(L"BUTTON", L"",
                                  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                  0, 0, colorBtnW, btnH,
                                  hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOOL_FILL_COLOR)), nullptr, nullptr);

    cmbTextSize_ = CreateWindowW(WC_COMBOBOXW, L"",
                                 WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
                                 0, 0, comboW, comboPopupHeight_,
                                 hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOOL_TEXT_SIZE)), nullptr, nullptr);
    ApplyStableComboTheme(cmbTextSize_);
    const std::array<const wchar_t *, 5> textSizes = {L"14", L"18", L"24", L"32", L"40"};
    int textSizeSelect                             = 0;
    for (size_t i = 0; i < textSizes.size(); ++i) {
        SendMessageW(cmbTextSize_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(textSizes[i]));
        if (std::fabs(ParseFloatText(textSizes[i], textSizeValue_) - textSizeValue_) < 0.01f) {
            textSizeSelect = static_cast<int>(i);
        }
    }
    SendMessageW(cmbTextSize_, CB_SETCURSEL, textSizeSelect, 0);

    cmbTextStyle_ = CreateWindowW(WC_COMBOBOXW, L"",
                                  WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
                                  0, 0, textStyleW, comboPopupHeight_,
                                  hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOOL_TEXT_STYLE)), nullptr, nullptr);
    ApplyStableComboTheme(cmbTextStyle_);
    SendMessageW(cmbTextStyle_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"常规"));
    SendMessageW(cmbTextStyle_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"粗体"));
    SendMessageW(cmbTextStyle_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"斜体"));
    SendMessageW(cmbTextStyle_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"粗斜"));
    int styleSelect = 0;
    if ((textStyleValue_ & Gdiplus::FontStyleBold) != 0 && (textStyleValue_ & Gdiplus::FontStyleItalic) != 0) {
        styleSelect = 3;
    } else if ((textStyleValue_ & Gdiplus::FontStyleBold) != 0) {
        styleSelect = 1;
    } else if ((textStyleValue_ & Gdiplus::FontStyleItalic) != 0) {
        styleSelect = 2;
    }
    SendMessageW(cmbTextStyle_, CB_SETCURSEL, styleSelect, 0);

    btnTextColor_ = CreateWindowW(L"BUTTON", L"",
                                  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                  0, 0, colorBtnW, btnH,
                                  hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOOL_TEXT_COLOR)), nullptr, nullptr);

    ConfigureComboControl(cmbStroke_, btnH, 8);
    ConfigureComboControl(cmbTextSize_, btnH, 8);
    ConfigureComboControl(cmbTextStyle_, btnH, 8);

    CreateTooltips();
    ApplyColorLabels();
    RelayoutVisibleControls();
}

void ToolbarWindow::RelayoutVisibleControls()
{
    if (!hwnd_) {
        return;
    }

    separatorXs_.clear();

    const float s          = static_cast<float>(dpi_) / 96.0f;
    const int gap          = std::max(2, static_cast<int>(std::round(4.0f * s)));
    const int margin       = std::max(2, static_cast<int>(std::round(4.0f * s)));
    const int btnH         = std::max(24, static_cast<int>(std::round(28.0f * s)));
    const int groupSepPad  = std::max(8, static_cast<int>(std::round(10.0f * s)));
    auto addGroupSeparator = [&](int &xx) {
        xx = std::max(margin, xx - gap);
        separatorXs_.push_back(xx + groupSepPad);
        xx += groupSepPad * 2;
    };

    const auto place = [&](HWND h, int &x, int w, bool visible, bool isCombo = false) {
        if (!h) {
            return;
        }
        ShowWindow(h, visible ? SW_SHOW : SW_HIDE);
        if (!visible) {
            return;
        }
        int y   = margin;
        int hgt = isCombo ? comboPopupHeight_ : btnH;
        SetWindowPos(h, nullptr, x, y, w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);
        x += w + gap;
    };

    int x = margin;
    if (longCaptureMode_) {
        for (UINT id : modeButtonIds_) {
            HWND h = buttons_.count(id) ? buttons_[id] : nullptr;
            int w  = buttonWidths_.count(id) ? buttonWidths_[id] : 34;
            place(h, x, w, false);
        }

        const int comboW     = std::max(56, static_cast<int>(std::round(62.0f * s)));
        const int colorBtnW  = btnH;
        const int checkW     = btnH;
        const int textStyleW = std::max(74, static_cast<int>(std::round(86.0f * s)));
        place(cmbStroke_, x, comboW, false, true);
        place(btnStrokeColor_, x, colorBtnW, false);
        place(chkFill_, x, checkW, false);
        place(btnFillColor_, x, colorBtnW, false);
        place(cmbTextSize_, x, comboW, false, true);
        place(cmbTextStyle_, x, textStyleW, false, true);
        place(btnTextColor_, x, colorBtnW, false);

        const std::array<UINT, 7> longCaptureActions = {
            ID_TOOL_TRIM_ABOVE,
            ID_TOOL_TRIM_BELOW,
            ID_TOOL_SAVE,
            ID_TOOL_COPY,
            ID_TOOL_COPY_FILE,
            ID_TOOL_PIN,
            ID_TOOL_CANCEL};
        bool hasAction = false;
        for (UINT id : longCaptureActions) {
            if (hasAction && (id == ID_TOOL_SAVE || id == ID_TOOL_CANCEL)) {
                addGroupSeparator(x);
            }
            HWND h = buttons_.count(id) ? buttons_[id] : nullptr;
            int w  = buttonWidths_.count(id) ? buttonWidths_[id] : 34;
            place(h, x, w, true);
            hasAction = true;
        }
        for (UINT id : actionButtonIds_) {
            if (std::find(longCaptureActions.begin(), longCaptureActions.end(), id) != longCaptureActions.end()) {
                continue;
            }
            HWND h = buttons_.count(id) ? buttons_[id] : nullptr;
            int w  = buttonWidths_.count(id) ? buttonWidths_[id] : 34;
            place(h, x, w, false);
        }
    } else if (whiteboardMode_) {
        const bool cursorMode   = activeTool_ == ToolType::None;
        const UINT activeToolId = ButtonIdFromTool(activeTool_);

        for (UINT id : modeButtonIds_) {
            HWND h             = buttons_.count(id) ? buttons_[id] : nullptr;
            int w              = buttonWidths_.count(id) ? buttonWidths_[id] : 34;
            const bool visible = cursorMode ? true : (id == ID_TOOL_CURSOR || id == activeToolId);
            place(h, x, w, visible);
        }

        if (!cursorMode) {
            addGroupSeparator(x);
        }

        const int comboW     = std::max(56, static_cast<int>(std::round(62.0f * s)));
        const int colorBtnW  = btnH;
        const int checkW     = btnH;
        const int textStyleW = std::max(74, static_cast<int>(std::round(86.0f * s)));

        bool showStroke = false;
        bool showFill   = false;
        bool showText   = false;
        switch (activeTool_) {
        case ToolType::Rect:
        case ToolType::Ellipse:
            showStroke = true;
            showFill   = true;
            break;
        case ToolType::Line:
        case ToolType::Arrow:
        case ToolType::Pen:
        case ToolType::Mosaic:
        case ToolType::Number:
            showStroke = true;
            break;
        case ToolType::Text:
            showText = true;
            break;
        default:
            break;
        }

        place(cmbStroke_, x, comboW, showStroke, true);
        place(btnStrokeColor_, x, colorBtnW, showStroke);
        place(chkFill_, x, checkW, showFill);
        place(btnFillColor_, x, colorBtnW, showFill);
        place(cmbTextSize_, x, comboW, showText, true);
        place(cmbTextStyle_, x, textStyleW, showText, true);
        place(btnTextColor_, x, colorBtnW, showText);

        if (showStroke || showFill || showText) {
            addGroupSeparator(x);
        } else if (cursorMode) {
            addGroupSeparator(x);
        }

        const std::array<UINT, 5> whiteboardActions = {
            ID_TOOL_SAVE,
            ID_TOOL_COPY,
            ID_TOOL_COPY_FILE,
            ID_TOOL_PIN,
            ID_TOOL_CANCEL};
        for (UINT id : whiteboardActions) {
            if (id == ID_TOOL_CANCEL) {
                addGroupSeparator(x);
            }
            HWND h = buttons_.count(id) ? buttons_[id] : nullptr;
            int w  = buttonWidths_.count(id) ? buttonWidths_[id] : 34;
            place(h, x, w, true);
        }

        for (UINT id : actionButtonIds_) {
            if (std::find(whiteboardActions.begin(), whiteboardActions.end(), id) != whiteboardActions.end()) {
                continue;
            }
            HWND h = buttons_.count(id) ? buttons_[id] : nullptr;
            int w  = buttonWidths_.count(id) ? buttonWidths_[id] : 34;
            place(h, x, w, false);
        }
    } else {
        const bool cursorMode   = activeTool_ == ToolType::None;
        const UINT activeToolId = ButtonIdFromTool(activeTool_);

        for (UINT id : modeButtonIds_) {
            HWND h             = buttons_.count(id) ? buttons_[id] : nullptr;
            int w              = buttonWidths_.count(id) ? buttonWidths_[id] : 34;
            const bool visible = cursorMode ? true : (id == ID_TOOL_CURSOR || id == activeToolId);
            place(h, x, w, visible);
        }

        if (!cursorMode) {
            addGroupSeparator(x);
        }

        bool showStroke = false;
        bool showFill   = false;
        bool showText   = false;
        switch (activeTool_) {
        case ToolType::Rect:
        case ToolType::Ellipse:
            showStroke = true;
            showFill   = true;
            break;
        case ToolType::Line:
        case ToolType::Arrow:
        case ToolType::Pen:
        case ToolType::Mosaic:
        case ToolType::Number:
            showStroke = true;
            break;
        case ToolType::Text:
            showText = true;
            break;
        default:
            break;
        }

        const int comboW     = std::max(56, static_cast<int>(std::round(62.0f * s)));
        const int colorBtnW  = btnH;
        const int checkW     = btnH;
        const int textStyleW = std::max(74, static_cast<int>(std::round(86.0f * s)));

        place(cmbStroke_, x, comboW, showStroke, true);
        place(btnStrokeColor_, x, colorBtnW, showStroke);
        place(chkFill_, x, checkW, showFill);
        place(btnFillColor_, x, colorBtnW, showFill);
        place(cmbTextSize_, x, comboW, showText, true);
        place(cmbTextStyle_, x, textStyleW, showText, true);
        place(btnTextColor_, x, colorBtnW, showText);

        if (showStroke || showFill || showText) {
            addGroupSeparator(x);
        }

        bool hasAction = false;
        for (UINT id : actionButtonIds_) {
            HWND h             = buttons_.count(id) ? buttons_[id] : nullptr;
            int w              = buttonWidths_.count(id) ? buttonWidths_[id] : 34;
            const bool visible = (id != ID_TOOL_TRIM_ABOVE && id != ID_TOOL_TRIM_BELOW);
            if (visible && hasAction && (id == ID_TOOL_LONG_CAPTURE || id == ID_TOOL_SAVE || id == ID_TOOL_CANCEL)) {
                addGroupSeparator(x);
            }
            place(h, x, w, visible);
            hasAction = hasAction || visible;
        }
    }

    toolbarWidth_  = std::max(220, x + margin);
    toolbarHeight_ = std::max(kToolbarHeight, btnH + margin * 2);

    SetWindowPos(hwnd_, nullptr, 0, 0, toolbarWidth_, toolbarHeight_,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    const int radius = std::max(4, static_cast<int>(std::round(6.0f * s)));
    HRGN rgn         = CreateRoundRectRgn(0, 0, toolbarWidth_ + 1, toolbarHeight_ + 1, radius, radius);
    SetWindowRgn(hwnd_, rgn, TRUE);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ToolbarWindow::PaintBackground(HDC hdc)
{
    if (!hdc) {
        return;
    }
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    UiGdi::ScopedGdiObject<HBRUSH> brush(CreateSolidBrush(RGB(20, 24, 31)));
    FillRect(hdc, &rc, brush.Get());

    UiGdi::ScopedGdiObject<HPEN> pen(CreatePen(PS_SOLID, 1, RGB(58, 63, 72)));
    UiGdi::ScopedSelectObject oldPen(hdc, pen.Get());
    for (int x : separatorXs_) {
        MoveToEx(hdc, x, 6, nullptr);
        LineTo(hdc, x, rc.bottom - 6);
    }
}

void ToolbarWindow::DrawButton(const DRAWITEMSTRUCT *dis)
{
    if (!dis || !dis->hDC) {
        return;
    }

    const UINT id          = dis->CtlID;
    const auto comboHandle = [&](UINT cid) -> HWND {
        if (cid == ID_TOOL_STROKE_WIDTH) return cmbStroke_;
        if (cid == ID_TOOL_TEXT_SIZE) return cmbTextSize_;
        if (cid == ID_TOOL_TEXT_STYLE) return cmbTextStyle_;
        return nullptr;
    };
    if (HWND combo = comboHandle(id)) {
        const bool disabled  = (dis->itemState & ODS_DISABLED) != 0;
        const bool selected  = (dis->itemState & ODS_SELECTED) != 0;
        const bool comboFace = (dis->itemID == static_cast<UINT>(-1)) || ((dis->itemState & ODS_COMBOBOXEDIT) != 0);
        const bool hovered   = hoveredControlId_ == id;
        // 使用统一的主题色系
        COLORREF bg     = comboFace
                              ? (hovered ? RGB(54, 62, 76) : ThemeColors::Component::Toolbar::ComboBoxBgColor)
                              : (selected ? RGB(62, 72, 90) : ThemeColors::Component::Toolbar::ComboBoxBgColor);
        COLORREF border = RGB(66, 72, 82);
        COLORREF fg     = disabled ? RGB(110, 116, 128) : ThemeColors::Component::Toolbar::ComboBoxTextColor;

        UiGdi::ScopedGdiObject<HBRUSH> fill(CreateSolidBrush(bg));
        FillRect(dis->hDC, &dis->rcItem, fill.Get());

        if (!comboFace) {
            UiGdi::ScopedGdiObject<HPEN> listPen(CreatePen(PS_SOLID, 1, border));
            UiGdi::ScopedSelectObject oldPen(dis->hDC, listPen.Get());
            UiGdi::ScopedSelectObject oldBrush(dis->hDC, GetStockObject(HOLLOW_BRUSH));
            Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
        }

        std::wstring itemText;
        wchar_t buf[128]{};
        if (dis->itemID != static_cast<UINT>(-1)) {
            SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(dis->itemID), reinterpret_cast<LPARAM>(buf));
            itemText = buf;
        } else {
            int cur = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
            if (cur >= 0) {
                SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(cur), reinterpret_cast<LPARAM>(buf));
                itemText = buf;
            }
        }

        RECT textRc = dis->rcItem;
        textRc.left += std::max(8, static_cast<int>(std::round(10.0f * (static_cast<float>(dpi_) / 96.0f))));
        textRc.right -= std::max(8, static_cast<int>(std::round(8.0f * (static_cast<float>(dpi_) / 96.0f))));
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, fg);
        UiGdi::ScopedSelectObject oldFont(dis->hDC, font_);
        DrawTextW(dis->hDC, itemText.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }

    const bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool hot      = hoveredControlId_ == id;
    const UINT activeId = ButtonIdFromTool(activeTool_);
    const bool checked  = (id == activeId) || (id == ID_TOOL_FILL_ENABLE && fillEnabledValue_);

    COLORREF bg = ThemeColors::Component::Toolbar::ComboBoxBgColor;
    if (checked) {
        bg = RGB(35, 81, 126);
    } else if (pressed) {
        bg = RGB(70, 80, 98);
    } else if (hot) {
        bg = RGB(62, 72, 90);
    }
    COLORREF border = checked ? RGB(95, 170, 240) : (hot ? RGB(110, 130, 165) : RGB(66, 72, 82));
    COLORREF fg     = disabled ? RGB(110, 116, 128) : ThemeColors::Component::Toolbar::ComboBoxTextColor;

    // Avoid white fringe on anti-aliased rounded corners by painting the control
    // background to the same toolbar color before drawing rounded content.
    {
        UiGdi::ScopedGdiObject<HBRUSH> panelBg(CreateSolidBrush(RGB(20, 24, 31)));
        FillRect(dis->hDC, &dis->rcItem, panelBg.Get());
    }
    const float dpiScale = static_cast<float>(dpi_) / 96.0f;
    Gdiplus::Graphics gg(dis->hDC);
    gg.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    gg.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    auto makeRoundRectPath = [](const Gdiplus::RectF &rr, float radius, Gdiplus::GraphicsPath &path) {
        const float r = std::min(radius, std::min(rr.Width, rr.Height) * 0.5f);
        const float d = r * 2.0f;
        path.Reset();
        path.AddArc(rr.X, rr.Y, d, d, 180.0f, 90.0f);
        path.AddArc(rr.X + rr.Width - d, rr.Y, d, d, 270.0f, 90.0f);
        path.AddArc(rr.X + rr.Width - d, rr.Y + rr.Height - d, d, d, 0.0f, 90.0f);
        path.AddArc(rr.X, rr.Y + rr.Height - d, d, d, 90.0f, 90.0f);
        path.CloseFigure();
    };

    const float btnRadius = std::max(4.0f, 5.0f * dpiScale);
    const Gdiplus::RectF btnRect(
        static_cast<float>(dis->rcItem.left) + 0.5f,
        static_cast<float>(dis->rcItem.top) + 0.5f,
        std::max(2.0f, static_cast<float>(RectWidth(dis->rcItem)) - 1.0f),
        std::max(2.0f, static_cast<float>(RectHeight(dis->rcItem)) - 1.0f));
    Gdiplus::GraphicsPath btnPath;
    makeRoundRectPath(btnRect, btnRadius, btnPath);
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));
    Gdiplus::Pen borderPen(Gdiplus::Color(255, GetRValue(border), GetGValue(border), GetBValue(border)), 1.0f);
    borderPen.SetLineJoin(Gdiplus::LineJoinRound);
    gg.FillPath(&bgBrush, &btnPath);
    gg.DrawPath(&borderPen, &btnPath);

    RECT textRc = dis->rcItem;
    if (id == ID_TOOL_STROKE_COLOR || id == ID_TOOL_FILL_COLOR || id == ID_TOOL_TEXT_COLOR) {
        const COLORREF c  = (id == ID_TOOL_STROKE_COLOR) ? strokeColor_ : (id == ID_TOOL_FILL_COLOR ? fillColor_ : textColor_);
        const int cx      = (textRc.left + textRc.right) / 2;
        const int cy      = (textRc.top + textRc.bottom) / 2;
        const int r       = std::max(5, static_cast<int>((textRc.bottom - textRc.top) / 4));
        const float ringW = std::max(2.0f, 1.8f * dpiScale);
        Gdiplus::SolidBrush cb(Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)));
        Gdiplus::Pen cp(Gdiplus::Color(255, 248, 248, 248), ringW);
        cp.SetLineJoin(Gdiplus::LineJoinRound);
        cp.SetStartCap(Gdiplus::LineCapRound);
        cp.SetEndCap(Gdiplus::LineCapRound);
        const Gdiplus::RectF dotRc(
            static_cast<float>(cx - r) + 0.5f,
            static_cast<float>(cy - r) + 0.5f,
            std::max(2.0f, static_cast<float>(r * 2)),
            std::max(2.0f, static_cast<float>(r * 2)));
        gg.FillEllipse(&cb, dotRc);
        gg.DrawEllipse(&cp, dotRc);
    } else if (id == ID_TOOL_FILL_ENABLE) {
        const float s    = dpiScale;
        const int pad    = std::max(4, static_cast<int>(std::round(6.0f * s)));
        const int lineW  = std::max(2, static_cast<int>(std::round(1.8f * s)));
        const int corner = std::max(2, static_cast<int>(std::round(3.0f * s)));
        RECT iconRc{
            textRc.left + pad,
            textRc.top + pad,
            textRc.right - pad,
            textRc.bottom - pad};
        if (RectWidth(iconRc) < 8 || RectHeight(iconRc) < 8) {
            iconRc = textRc;
            InflateRect(&iconRc, -2, -2);
        }

        const Gdiplus::Color iconColor(255, GetRValue(fg), GetGValue(fg), GetBValue(fg));
        const Gdiplus::RectF iconRect(
            static_cast<float>(iconRc.left) + 0.5f,
            static_cast<float>(iconRc.top) + 0.5f,
            std::max(2.0f, static_cast<float>(RectWidth(iconRc)) - 1.0f),
            std::max(2.0f, static_cast<float>(RectHeight(iconRc)) - 1.0f));
        Gdiplus::GraphicsPath iconPath;
        makeRoundRectPath(iconRect, static_cast<float>(corner), iconPath);
        Gdiplus::Pen iconPen(iconColor, static_cast<float>(lineW));
        iconPen.SetLineJoin(Gdiplus::LineJoinRound);
        iconPen.SetStartCap(Gdiplus::LineCapRound);
        iconPen.SetEndCap(Gdiplus::LineCapRound);
        gg.DrawPath(&iconPen, &iconPath);

        if (fillEnabledValue_) {
            const auto saved = gg.Save();
            gg.SetClip(&iconPath, Gdiplus::CombineModeIntersect);
            Gdiplus::Pen hatchPen(iconColor, std::max(1.0f, static_cast<float>(lineW) * 0.9f));
            hatchPen.SetLineJoin(Gdiplus::LineJoinRound);
            hatchPen.SetStartCap(Gdiplus::LineCapRound);
            hatchPen.SetEndCap(Gdiplus::LineCapRound);
            const float h  = iconRect.Height;
            const int step = std::max(lineW + 4, static_cast<int>(std::round(6.0f * s)));
            for (float x = iconRect.X - h; x < iconRect.GetRight(); x += static_cast<float>(step)) {
                gg.DrawLine(&hatchPen,
                            Gdiplus::PointF(x, iconRect.GetBottom()),
                            Gdiplus::PointF(x + h, iconRect.Y));
            }
            gg.Restore(saved);
        }
    } else if (const ButtonDef *def = FindButtonDef(id)) {
        if (!DrawToolbarIcon(id, dis->hDC, textRc, fg, static_cast<float>(dpi_) / 96.0f)) {
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, fg);
            HFONT useFont = iconFont_ ? iconFont_ : font_;
            UiGdi::ScopedSelectObject oldFont(dis->hDC, useFont);
            DrawTextW(dis->hDC, def->iconText, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

void ToolbarWindow::Destroy()
{
    if (hwnd_) {
        KillTimer(hwnd_, 1);
    }
    hoveredControlId_ = 0;
    pendingHoverId_   = 0;
    DestroyTooltips();
    UiGdi::ResetGdiObject(comboBgBrush_);
    UiGdi::ResetGdiObject(font_);
    UiGdi::ResetGdiObject(iconFont_);
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    buttons_.clear();
    buttonWidths_.clear();
    modeButtonIds_.clear();
    actionButtonIds_.clear();
    separatorXs_.clear();
}

void ToolbarWindow::ShowNear(const RECT &selection, const RECT &overlayBounds)
{
    if (!hwnd_) {
        return;
    }

    RelayoutVisibleControls();

    RECT selectionScreen = selection;
    RECT overlayScreen   = overlayBounds;
    RECT visible         = overlayBounds;

    if (parent_) {
        POINT sTl{selection.left, selection.top};
        POINT sBr{selection.right, selection.bottom};
        ClientToScreen(parent_, &sTl);
        ClientToScreen(parent_, &sBr);
        selectionScreen = RECT{sTl.x, sTl.y, sBr.x, sBr.y};

        POINT oTl{overlayBounds.left, overlayBounds.top};
        POINT oBr{overlayBounds.right, overlayBounds.bottom};
        ClientToScreen(parent_, &oTl);
        ClientToScreen(parent_, &oBr);
        overlayScreen = RECT{oTl.x, oTl.y, oBr.x, oBr.y};
        visible       = overlayScreen;

        POINT center{(selectionScreen.left + selectionScreen.right) / 2,
                     (selectionScreen.top + selectionScreen.bottom) / 2};
        MONITORINFO mi{};
        mi.cbSize    = sizeof(mi);
        HMONITOR mon = MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST);
        if (mon && GetMonitorInfoW(mon, &mi)) {
            RECT inter{};
            if (IntersectRect(&inter, &mi.rcMonitor, &overlayScreen)) {
                visible = inter;
            }
        }
    }

    const int margin     = 8;
    const int belowY     = selectionScreen.bottom + margin;
    const int aboveY     = selectionScreen.top - toolbarHeight_ - margin;
    const bool belowFits = belowY + toolbarHeight_ <= visible.bottom - margin;
    const bool aboveFits = aboveY >= visible.top + margin;

    int y = belowFits ? belowY : (aboveFits ? aboveY : belowY);
    y     = std::clamp(y,
                       static_cast<int>(visible.top) + margin,
                       std::max(static_cast<int>(visible.top) + margin, static_cast<int>(visible.bottom) - toolbarHeight_ - margin));

    int x = selectionScreen.left;
    x     = std::clamp(x,
                       static_cast<int>(visible.left) + margin,
                       std::max(static_cast<int>(visible.left) + margin, static_cast<int>(visible.right) - toolbarWidth_ - margin));

    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, toolbarWidth_, toolbarHeight_,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
    SetTimer(hwnd_, 1, 10, nullptr);
    UpdateHoverState();
}

void ToolbarWindow::Hide()
{
    if (hwnd_) {
        KillTimer(hwnd_, 1);
        hoveredControlId_ = 0;
        pendingHoverId_   = 0;
        HideTrackedTooltip();
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void ToolbarWindow::ApplyColorLabels()
{
    if (btnStrokeColor_) {
        InvalidateRect(btnStrokeColor_, nullptr, FALSE);
    }
    if (btnFillColor_) {
        InvalidateRect(btnFillColor_, nullptr, FALSE);
    }
    if (btnTextColor_) {
        InvalidateRect(btnTextColor_, nullptr, FALSE);
    }
}

void ToolbarWindow::SetActiveTool(ToolType tool)
{
    const UINT id = ButtonIdFromTool(tool);
    activeTool_   = IsModeButtonId(id) ? tool : ToolType::None;
    RelayoutVisibleControls();
}

void ToolbarWindow::SetLongCaptureMode(bool enabled)
{
    if (longCaptureMode_ == enabled) {
        return;
    }
    longCaptureMode_ = enabled;
    if (longCaptureMode_) {
        whiteboardMode_ = false;
        activeTool_     = ToolType::None;
    }
    RelayoutVisibleControls();
}

void ToolbarWindow::SetWhiteboardMode(bool enabled)
{
    if (whiteboardMode_ == enabled) {
        return;
    }
    whiteboardMode_ = enabled;
    if (whiteboardMode_) {
        longCaptureMode_ = false;
    }
    RelayoutVisibleControls();
}

float ToolbarWindow::StrokeWidth() const
{
    return ComboFloatValue(cmbStroke_, strokeWidthValue_);
}

bool ToolbarWindow::FillEnabled() const
{
    return fillEnabledValue_;
}

float ToolbarWindow::TextSize() const
{
    return ComboFloatValue(cmbTextSize_, textSizeValue_);
}

INT ToolbarWindow::TextStyle() const
{
    if (!cmbTextStyle_) {
        return textStyleValue_;
    }
    const int idx = static_cast<int>(SendMessageW(cmbTextStyle_, CB_GETCURSEL, 0, 0));
    switch (idx) {
    case 1:
        return Gdiplus::FontStyleBold;
    case 2:
        return Gdiplus::FontStyleItalic;
    case 3:
        return Gdiplus::FontStyleBold | Gdiplus::FontStyleItalic;
    default:
        return Gdiplus::FontStyleRegular;
    }
}

UINT_PTR CALLBACK ToolbarWindow::ColorDialogHookProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_INITDIALOG) {
        const auto *cc = reinterpret_cast<const CHOOSECOLORW *>(lParam);
        if (cc && cc->lCustData != 0) {
            auto *self = reinterpret_cast<ToolbarWindow *>(cc->lCustData);
            POINT pt   = self->colorDialogAnchor_;
            RECT rc{};
            if (GetWindowRect(hwnd, &rc)) {
                const int dialogW = RectWidth(rc);
                const int dialogH = RectHeight(rc);
                pt.x += 12;
                pt.y += 12;

                MONITORINFO mi{};
                mi.cbSize    = sizeof(mi);
                HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
                if (mon && GetMonitorInfoW(mon, &mi)) {
                    pt.x = std::clamp(pt.x, mi.rcWork.left, std::max(mi.rcWork.left, mi.rcWork.right - dialogW));
                    pt.y = std::clamp(pt.y, mi.rcWork.top, std::max(mi.rcWork.top, mi.rcWork.bottom - dialogH));
                }
                SetWindowPos(hwnd, HWND_TOPMOST, pt.x, pt.y, 0, 0,
                             SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
    } else if (msg == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) {
        FLASHWINFO fwi{};
        fwi.cbSize    = sizeof(fwi);
        fwi.hwnd      = hwnd;
        fwi.dwFlags   = FLASHW_ALL;
        fwi.uCount    = 2;
        fwi.dwTimeout = 0;
        FlashWindowEx(&fwi);
        SetForegroundWindow(hwnd);
        return TRUE;
    }
    return 0;
}

bool ToolbarWindow::PickColor(HWND owner, COLORREF &inOutColor)
{
    const DWORD now = GetTickCount();
    if (colorDialogOpen_ || now < colorDialogBlockUntilTick_) {
        return false;
    }

    GetCursorPos(&colorDialogAnchor_);
    colorDialogOpen_  = true;
    auto releaseGuard = std::unique_ptr<void, std::function<void(void *)>>(
        reinterpret_cast<void *>(1),
        [this](void *) { colorDialogOpen_ = false; });

    static COLORREF customColors[16] = {
        RGB(255, 0, 0), RGB(255, 136, 0), RGB(255, 220, 0), RGB(40, 180, 70),
        RGB(0, 160, 255), RGB(95, 90, 255), RGB(180, 90, 230), RGB(255, 255, 255),
        RGB(0, 0, 0), RGB(245, 100, 120), RGB(180, 180, 180), RGB(50, 50, 50),
        RGB(220, 70, 70), RGB(70, 150, 220), RGB(90, 200, 180), RGB(255, 200, 120)};
    HWND dialogOwner          = hwnd_ ? hwnd_ : owner;
    const bool disableOverlay = owner && owner != dialogOwner && IsWindowEnabled(owner);
    if (disableOverlay) {
        EnableWindow(owner, FALSE);
    }

    auto restoreOwner = std::unique_ptr<void, std::function<void(void *)>>(
        reinterpret_cast<void *>(1),
        [this, owner, disableOverlay](void *) {
            if (disableOverlay && owner) {
                EnableWindow(owner, TRUE);
            }
            if (hwnd_) {
                SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        });

    CHOOSECOLORW cc{};
    cc.lStructSize  = sizeof(cc);
    cc.hwndOwner    = dialogOwner;
    cc.rgbResult    = inOutColor;
    cc.lpCustColors = customColors;
    cc.Flags        = CC_FULLOPEN | CC_RGBINIT | CC_ENABLEHOOK;
    cc.lpfnHook     = ToolbarWindow::ColorDialogHookProc;
    cc.lCustData    = reinterpret_cast<LPARAM>(this);
    if (!ChooseColorW(&cc)) {
        colorDialogBlockUntilTick_ = GetTickCount() + 180;
        return false;
    }
    inOutColor                 = cc.rgbResult;
    colorDialogBlockUntilTick_ = GetTickCount() + 180;
    return true;
}

bool ToolbarWindow::ChooseStrokeColor(HWND owner)
{
    if (!PickColor(owner, strokeColor_)) {
        return false;
    }
    ApplyColorLabels();
    return true;
}

bool ToolbarWindow::ChooseFillColor(HWND owner)
{
    if (!PickColor(owner, fillColor_)) {
        return false;
    }
    ApplyColorLabels();
    return true;
}

bool ToolbarWindow::ChooseTextColor(HWND owner)
{
    if (!PickColor(owner, textColor_)) {
        return false;
    }
    ApplyColorLabels();
    return true;
}

bool ToolbarWindow::IsModeButtonId(UINT id) const
{
    return std::find(modeButtonIds_.begin(), modeButtonIds_.end(), id) != modeButtonIds_.end();
}

ToolType ToolbarWindow::ToolFromButtonId(UINT id) const
{
    switch (id) {
    case ID_TOOL_CURSOR:
        return ToolType::None;
    case ID_TOOL_RECT:
        return ToolType::Rect;
    case ID_TOOL_ELLIPSE:
        return ToolType::Ellipse;
    case ID_TOOL_ARROW:
        return ToolType::Arrow;
    case ID_TOOL_LINE:
        return ToolType::Line;
    case ID_TOOL_PEN:
        return ToolType::Pen;
    case ID_TOOL_MOSAIC:
        return ToolType::Mosaic;
    case ID_TOOL_TEXT:
        return ToolType::Text;
    case ID_TOOL_NUMBER:
        return ToolType::Number;
    case ID_TOOL_ERASER:
        return ToolType::Eraser;
    default:
        return ToolType::None;
    }
}

UINT ToolbarWindow::ButtonIdFromTool(ToolType tool) const
{
    switch (tool) {
    case ToolType::Rect:
        return ID_TOOL_RECT;
    case ToolType::Ellipse:
        return ID_TOOL_ELLIPSE;
    case ToolType::Arrow:
        return ID_TOOL_ARROW;
    case ToolType::Line:
        return ID_TOOL_LINE;
    case ToolType::Pen:
        return ID_TOOL_PEN;
    case ToolType::Mosaic:
        return ID_TOOL_MOSAIC;
    case ToolType::Text:
        return ID_TOOL_TEXT;
    case ToolType::Number:
        return ID_TOOL_NUMBER;
    case ToolType::Eraser:
        return ID_TOOL_ERASER;
    case ToolType::None:
    default:
        return ID_TOOL_CURSOR;
    }
}

void ToolbarWindow::UpdateDpiFont()
{
    if (!hwnd_) {
        return;
    }

    UINT dpi = GetDpiForWindow(parent_ ? parent_ : hwnd_);
    if (dpi == 0) {
        dpi = 96;
    }
    bool dpiChanged = dpi_ != dpi;
    if (dpiChanged) {
        strokeWidthValue_ = StrokeWidth();
        textSizeValue_    = TextSize();
        textStyleValue_   = TextStyle();
        fillEnabledValue_ = FillEnabled();

        dpi_ = dpi;
        std::vector<HWND> children;
        for (HWND child = GetWindow(hwnd_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            children.push_back(child);
        }
        for (HWND child : children) {
            DestroyWindow(child);
        }

        cmbStroke_      = nullptr;
        btnStrokeColor_ = nullptr;
        chkFill_        = nullptr;
        btnFillColor_   = nullptr;
        cmbTextSize_    = nullptr;
        cmbTextStyle_   = nullptr;
        btnTextColor_   = nullptr;

        CreateButtons();
    } else if (font_ != nullptr && iconFont_ != nullptr) {
        return;
    }

    UiGdi::ResetGdiObject(font_);
    UiGdi::ResetGdiObject(iconFont_);

    const int textPx = -MulDiv(13, static_cast<int>(dpi_), 96);
    font_            = CreateFontW(
        textPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    const int iconPx = -MulDiv(15, static_cast<int>(dpi_), 96);
    iconFont_        = CreateFontW(
        iconPx, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Symbol");

    for (HWND child = GetWindow(hwnd_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        const UINT id          = static_cast<UINT>(GetDlgCtrlID(child));
        const bool iconControl = buttons_.count(id) > 0 ||
                                 id == ID_TOOL_STROKE_COLOR || id == ID_TOOL_FILL_COLOR || id == ID_TOOL_TEXT_COLOR ||
                                 id == ID_TOOL_FILL_ENABLE;
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(iconControl ? iconFont_ : font_), TRUE);
    }
    if (tooltip_ && font_) {
        SendMessageW(tooltip_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    }
}
