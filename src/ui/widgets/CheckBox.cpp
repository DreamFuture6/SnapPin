#include "ui/widgets/CheckBox.h"
#include "ui/UiUtil.h"

namespace {
constexpr UINT_PTR kCheckBoxSubclassId = 0x7101;
}

bool CheckBoxControl::Create(HWND parent, HINSTANCE hInstance, int controlId, const wchar_t* text, int boxGapDip) {
    boxGapDip_ = std::max(0, boxGapDip);
    hwnd_ = CreateWindowExW(
        0,
        WC_BUTTONW,
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
        hInstance,
        nullptr);
    if (!hwnd_) {
        return false;
    }
    SetWindowSubclass(hwnd_, SubclassProc, kCheckBoxSubclassId, reinterpret_cast<DWORD_PTR>(this));
    return true;
}

void CheckBoxControl::SetBounds(int x, int y, int width, int height, UINT flags) {
    if (!hwnd_ || !IsWindow(hwnd_)) {
        return;
    }
    SetWindowPos(hwnd_, nullptr, x, y, width, height, flags);
}

void CheckBoxControl::SetChecked(bool checked) {
    if (checked_ == checked) {
        return;
    }
    checked_ = checked;
    if (hwnd_ && IsWindow(hwnd_)) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

RECT CheckBoxControl::ComputeBoxRect() const {
    RECT rc{};
    if (!hwnd_ || !IsWindow(hwnd_)) {
        return rc;
    }
    GetClientRect(hwnd_, &rc);
    const UINT dpi = UiUtil::GetWindowDpiSafe(hwnd_);

    wchar_t text[256]{};
    GetWindowTextW(hwnd_, text, static_cast<int>(std::size(text)));
    HDC hdc = GetDC(hwnd_);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd_, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = nullptr;
    if (font) {
        oldFont = SelectObject(hdc, font);
    }
    SIZE sz{};
    GetTextExtentPoint32W(hdc, text, static_cast<int>(wcslen(text)), &sz);
    if (oldFont) {
        SelectObject(hdc, oldFont);
    }
    ReleaseDC(hwnd_, hdc);

    const int boxSize = UiUtil::DpiScale(18, dpi);
    const int boxGap = UiUtil::DpiScale(boxGapDip_, dpi);
    int boxLeft = rc.left + sz.cx + boxGap;
    const int maxLeft = rc.right - UiUtil::DpiScale(6, dpi) - boxSize;
    if (boxLeft > maxLeft) {
        boxLeft = maxLeft;
    }
    const int boxTop = rc.top + (rc.bottom - rc.top - boxSize) / 2;
    return RECT{ boxLeft, boxTop, boxLeft + boxSize, boxTop + boxSize };
}

void CheckBoxControl::SetHover(bool hover) {
    if (hover_ == hover) {
        return;
    }
    hover_ = hover;
    if (hwnd_ && IsWindow(hwnd_)) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

bool CheckBoxControl::HandleClickFromCurrentMessage() {
    if (!hwnd_ || !IsWindow(hwnd_)) {
        return false;
    }
    DWORD msgPos = GetMessagePos();
    POINT pt{ GET_X_LPARAM(msgPos), GET_Y_LPARAM(msgPos) };
    ScreenToClient(hwnd_, &pt);
    const RECT box = ComputeBoxRect();
    if (!PtInRect(&box, pt)) {
        return false;
    }
    SetChecked(!checked_);
    return true;
}

bool CheckBoxControl::HandleSetCursor() const {
    if (!hwnd_ || !IsWindow(hwnd_)) {
        return false;
    }
    POINT pt{};
    GetCursorPos(&pt);
    ScreenToClient(hwnd_, &pt);
    const RECT box = ComputeBoxRect();
    if (PtInRect(&box, pt)) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
    } else {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }
    return true;
}

bool CheckBoxControl::Draw(const DRAWITEMSTRUCT* dis, const CheckBoxRenderStyle& style, UINT dpi) const {
    if (!dis || dis->hwndItem != hwnd_) {
        return false;
    }
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    SetBkMode(hdc, TRANSPARENT);

    UiUtil::FillRectColor(hdc, rc, style.panelColor);

    wchar_t text[256]{};
    GetWindowTextW(hwnd_, text, static_cast<int>(std::size(text)));
    RECT tr = rc;
    SetTextColor(hdc, style.textColor);
    DrawTextW(hdc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

    const RECT box = ComputeBoxRect();
    const COLORREF boxFill = checked_ ? style.accentColor : (hover_ ? RGB(58, 64, 75) : style.inputColor);
    const COLORREF boxStroke = checked_ ? style.borderActive : (hover_ ? style.borderHover : style.borderDefault);
    UiUtil::DrawRoundedFillStroke(
        hdc,
        box,
        boxFill,
        boxStroke,
        1.0f,
        static_cast<float>(UiUtil::DpiScale(4, dpi)));

    if (checked_) {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen tick(
            Gdiplus::Color(255, 255, 255, 255),
            static_cast<Gdiplus::REAL>(std::max(1, UiUtil::DpiScale(2, dpi))));
        const float x = static_cast<float>(box.left);
        const float y = static_cast<float>(box.top);
        const float w = static_cast<float>(box.right - box.left);
        const float h = static_cast<float>(box.bottom - box.top);
        const Gdiplus::PointF p1(x + w * 0.24f, y + h * 0.53f);
        const Gdiplus::PointF p2(x + w * 0.43f, y + h * 0.72f);
        const Gdiplus::PointF p3(x + w * 0.76f, y + h * 0.30f);
        g.DrawLine(&tick, p1, p2);
        g.DrawLine(&tick, p2, p3);
    }
    return true;
}

LRESULT CALLBACK CheckBoxControl::SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR refData) {
    auto* self = reinterpret_cast<CheckBoxControl*>(refData);
    if (!self) {
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
    case WM_MOUSEMOVE: {
        const POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const RECT box = self->ComputeBoxRect();
        self->SetHover(PtInRect(&box, pt) != FALSE);
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        self->SetHover(false);
        return 0;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, SubclassProc, kCheckBoxSubclassId);
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}
