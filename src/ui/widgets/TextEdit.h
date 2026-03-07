#pragma once

#include "common.h"

struct TextEditStyle {
    COLORREF panelColor = RGB(39, 43, 50);
    COLORREF inputColor = RGB(49, 54, 62);
    COLORREF borderDefault = RGB(130, 138, 152);
    COLORREF borderHover = RGB(156, 164, 178);
    COLORREF borderActive = RGB(95, 165, 255);
    int cornerRadiusDip = 4;
    int innerPaddingXDip = 8;
    int innerPaddingYDip = 5;
    int textMarginXDip = 6;
};

class TextEditControl {
public:
    bool Create(HWND parent, HINSTANCE hInstance, int controlId, const wchar_t* text, DWORD extraStyle = 0);
    void SetBounds(int x, int y, int width, int height, UINT flags = SWP_NOZORDER);

    void SetText(const wchar_t* text) const;
    void SetText(const std::wstring& text) const { SetText(text.c_str()); }
    std::wstring Text() const;

    HWND EditHandle() const { return edit_; }

private:
    static LRESULT CALLBACK FrameSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
        UINT_PTR, DWORD_PTR refData);
    static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
        UINT_PTR, DWORD_PTR refData);

    void SetHover(bool hover);
    void SetActive(bool active);
    void InvalidateBorder() const;
    void PaintFrame() const;
    void UpdateInnerLayout() const;

    HWND frame_ = nullptr;
    HWND edit_ = nullptr;
    TextEditStyle style_{};
    bool hover_ = false;
    bool active_ = false;
};
