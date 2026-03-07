#pragma once

#include "common.h"

namespace UiUtil {

constexpr UINT kDefaultDpi = 96;

UINT GetWindowDpiSafe(HWND hwnd);
int DpiScale(int value, UINT dpi);
COLORREF UnifiedBorderColor(bool active, bool hovered, COLORREF borderDefault, COLORREF borderHover, COLORREF borderActive);

void FillRectColor(HDC hdc, const RECT& rc, COLORREF color);
void AddRoundRectPath(Gdiplus::GraphicsPath& path, const RECT& rc, float radius);
void DrawRoundedFillStroke(HDC hdc, const RECT& rc, COLORREF fill, COLORREF stroke,
    float strokeWidth, float radius, bool fillEnabled = true);
void DrawRoundBorderGdi(HDC hdc, const RECT& rc, COLORREF color, int thickness, int radius);
void ApplyRoundedRegion(HWND hwnd, int radiusPx);

HWND CreateChildControl(HWND parent, HINSTANCE hInstance, LPCWSTR className, LPCWSTR text,
    DWORD style, int controlId = 0, DWORD exStyle = 0);
HWND CreatePanel(HWND parent, HINSTANCE hInstance, bool visible = false);
HWND CreateLabel(HWND parent, HINSTANCE hInstance, LPCWSTR text);
HWND CreateOwnerDrawButton(HWND parent, HINSTANCE hInstance, LPCWSTR text, int controlId);

} // namespace UiUtil
