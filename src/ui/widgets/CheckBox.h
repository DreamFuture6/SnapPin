#pragma once

#include "common.h"

struct CheckBoxRenderStyle {
    COLORREF panelColor = RGB(0, 0, 0);
    COLORREF textColor = RGB(255, 255, 255);
    COLORREF inputColor = RGB(0, 0, 0);
    COLORREF accentColor = RGB(0, 120, 215);
    COLORREF borderDefault = RGB(120, 120, 120);
    COLORREF borderHover = RGB(160, 160, 160);
    COLORREF borderActive = RGB(95, 165, 255);
};

class CheckBoxControl {
public:
    bool Create(HWND parent, HINSTANCE hInstance, int controlId, const wchar_t* text, int boxGapDip = 60);
    void SetBounds(int x, int y, int width, int height, UINT flags = SWP_NOZORDER);

    void SetChecked(bool checked);
    bool Checked() const { return checked_; }

    bool HandleClickFromCurrentMessage();
    bool HandleSetCursor() const;
    bool Draw(const DRAWITEMSTRUCT* dis, const CheckBoxRenderStyle& style, UINT dpi) const;

private:
    static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
        UINT_PTR, DWORD_PTR refData);

    RECT ComputeBoxRect() const;
    void SetHover(bool hover);

    HWND hwnd_ = nullptr;
    bool checked_ = false;
    bool hover_ = false;
    int boxGapDip_ = 60;
};
