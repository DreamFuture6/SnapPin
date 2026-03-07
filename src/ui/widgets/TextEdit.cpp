#include "ui/widgets/TextEdit.h"
#include "ui/UiUtil.h"

namespace {
constexpr UINT_PTR kTextEditFrameSubclassId = 0x7201;
constexpr UINT_PTR kTextEditInnerSubclassId = 0x7202;
}

bool TextEditControl::Create(HWND parent, HINSTANCE hInstance, int controlId, const wchar_t* text, DWORD extraStyle) {
    frame_ = UiUtil::CreateChildControl(parent, hInstance, WC_STATICW, nullptr, WS_CHILD | WS_VISIBLE);
    if (!frame_) {
        return false;
    }

    edit_ = UiUtil::CreateChildControl(frame_, hInstance, WC_EDITW, text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | extraStyle, controlId);
    if (!edit_) {
        DestroyWindow(frame_);
        frame_ = nullptr;
        return false;
    }

    SetWindowSubclass(frame_, FrameSubclassProc, kTextEditFrameSubclassId, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(edit_, EditSubclassProc, kTextEditInnerSubclassId, reinterpret_cast<DWORD_PTR>(this));
    UpdateInnerLayout();
    return true;
}

void TextEditControl::SetBounds(int x, int y, int width, int height, UINT flags) {
    if (!frame_ || !IsWindow(frame_)) {
        return;
    }
    SetWindowPos(frame_, nullptr, x, y, width, height, flags);
    UpdateInnerLayout();
}

void TextEditControl::SetText(const wchar_t* text) const {
    if (!edit_ || !IsWindow(edit_)) {
        return;
    }
    SetWindowTextW(edit_, text ? text : L"");
}

std::wstring TextEditControl::Text() const {
    if (!edit_ || !IsWindow(edit_)) {
        return {};
    }
    const int len = GetWindowTextLengthW(edit_);
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    if (len > 0) {
        GetWindowTextW(edit_, text.data(), len + 1);
    }
    text.resize(static_cast<size_t>(len));
    return text;
}

void TextEditControl::SetHover(bool hover) {
    if (hover_ == hover) {
        return;
    }
    hover_ = hover;
    InvalidateBorder();
}

void TextEditControl::SetActive(bool active) {
    if (active_ == active) {
        return;
    }
    active_ = active;
    if (active_) {
        hover_ = false;
    }
    InvalidateBorder();
}

void TextEditControl::InvalidateBorder() const {
    if (!frame_ || !IsWindow(frame_)) {
        return;
    }
    RECT rc{};
    GetClientRect(frame_, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) {
        return;
    }
    const int bw = std::max(1, UiUtil::DpiScale(3, UiUtil::GetWindowDpiSafe(frame_)));
    if (w <= bw * 2 || h <= bw * 2) {
        InvalidateRect(frame_, nullptr, FALSE);
        return;
    }
    RECT rTop{ rc.left, rc.top, rc.right, rc.top + bw };
    RECT rBottom{ rc.left, rc.bottom - bw, rc.right, rc.bottom };
    RECT rLeft{ rc.left, rc.top + bw, rc.left + bw, rc.bottom - bw };
    RECT rRight{ rc.right - bw, rc.top + bw, rc.right, rc.bottom - bw };
    InvalidateRect(frame_, &rTop, FALSE);
    InvalidateRect(frame_, &rBottom, FALSE);
    InvalidateRect(frame_, &rLeft, FALSE);
    InvalidateRect(frame_, &rRight, FALSE);
}

void TextEditControl::PaintFrame() const {
    if (!frame_ || !IsWindow(frame_)) {
        return;
    }
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(frame_, &ps);
    RECT rc{};
    GetClientRect(frame_, &rc);
    UiUtil::FillRectColor(hdc, rc, style_.panelColor);

    InflateRect(&rc, -1, -1);
    const UINT dpi = UiUtil::GetWindowDpiSafe(frame_);
    const int corner = UiUtil::DpiScale(style_.cornerRadiusDip, dpi);
    UiUtil::DrawRoundedFillStroke(hdc, rc, style_.inputColor, style_.inputColor, 0.0f,
        static_cast<float>(corner), true);
    UiUtil::DrawRoundBorderGdi(
        hdc,
        rc,
        UiUtil::UnifiedBorderColor(active_, hover_, style_.borderDefault, style_.borderHover, style_.borderActive),
        1,
        corner);
    EndPaint(frame_, &ps);
}

void TextEditControl::UpdateInnerLayout() const {
    if (!frame_ || !edit_ || !IsWindow(frame_) || !IsWindow(edit_)) {
        return;
    }
    RECT rc{};
    GetClientRect(frame_, &rc);
    const UINT dpi = UiUtil::GetWindowDpiSafe(frame_);
    const int padX = UiUtil::DpiScale(style_.innerPaddingXDip, dpi);
    const int padY = UiUtil::DpiScale(style_.innerPaddingYDip, dpi);
    const int frameWidth = static_cast<int>(rc.right - rc.left);
    const int frameHeight = static_cast<int>(rc.bottom - rc.top);
    const int width = std::max(UiUtil::DpiScale(24, dpi), frameWidth - padX * 2);
    const int height = std::max(UiUtil::DpiScale(20, dpi), frameHeight - padY * 2);
    SetWindowPos(edit_, nullptr, padX, padY, width, height, SWP_NOZORDER);

    const int margin = UiUtil::DpiScale(style_.textMarginXDip, dpi);
    SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(margin, margin));
}

LRESULT CALLBACK TextEditControl::FrameSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR refData) {
    auto* self = reinterpret_cast<TextEditControl*>(refData);
    if (!self) {
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
    case WM_PAINT:
        self->PaintFrame();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
        if (!self->active_) {
            self->SetHover(true);
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (!self->active_) {
            self->SetHover(false);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (self->edit_ && IsWindow(self->edit_)) {
            SetFocus(self->edit_);
            return 0;
        }
        break;
    case WM_SIZE:
        self->UpdateInnerLayout();
        return 0;
    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HWND root = GetAncestor(hwnd, GA_ROOT);
        if (root && IsWindow(root)) {
            return SendMessageW(root, msg, wParam, lParam);
        }
        break;
    }
    case WM_NCDESTROY:
        self->hover_ = false;
        self->active_ = false;
        if (self->frame_ == hwnd) {
            self->frame_ = nullptr;
        }
        RemoveWindowSubclass(hwnd, FrameSubclassProc, kTextEditFrameSubclassId);
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK TextEditControl::EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR refData) {
    auto* self = reinterpret_cast<TextEditControl*>(refData);
    if (!self) {
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
    case WM_SETFOCUS:
        self->SetActive(true);
        break;
    case WM_KILLFOCUS:
        self->SetActive(false);
        break;
    case WM_MOUSEMOVE:
        if (!self->active_) {
            self->SetHover(true);
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }
        break;
    case WM_MOUSELEAVE:
        if (!self->active_) {
            self->SetHover(false);
        }
        break;
    case WM_NCDESTROY:
        if (self->edit_ == hwnd) {
            self->edit_ = nullptr;
        }
        RemoveWindowSubclass(hwnd, EditSubclassProc, kTextEditInnerSubclassId);
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}
