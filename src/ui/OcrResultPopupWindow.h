#pragma once

#include "common.h"

class OcrResultPopupWindow {
public:
    OcrResultPopupWindow() = default;
    ~OcrResultPopupWindow();

    bool Show(HINSTANCE hInstance, HWND owner, const std::wstring& title, const std::wstring& text,
        const std::optional<RECT>& anchorScreenRect);
    void Close();
    bool IsOpen() const { return hwnd_ != nullptr && IsWindow(hwnd_) != FALSE; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void RegisterWindowClass(HINSTANCE hInstance);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void EnsureFonts();
    void Layout();

    HINSTANCE hInstance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    HWND titleLabel_ = nullptr;
    HWND bodyEdit_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT bodyFont_ = nullptr;
    HBRUSH backgroundBrush_ = nullptr;
    HBRUSH panelBrush_ = nullptr;
    UINT dpi_ = 96;
    UINT_PTR autoCloseTimer_ = 0;
    std::wstring titleText_;
    std::wstring bodyText_;
};
