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
void ConfigureComboControl(HWND combo, int controlHeight, int visibleItems, int minItemHeight, int verticalPadding);
void ApplyStableComboTheme(HWND combo);
void EnsureComboListTopMost(HWND combo);
std::wstring GetComboItemText(HWND combo, int itemIndex);
std::wstring GetComboSelectionOrWindowText(HWND combo, int selectionOverride = -1);
std::wstring GetOwnerDrawComboText(HWND combo, bool comboFace, UINT controlId, UINT itemId,
    UINT openComboControlId, int openComboSelectionIndex);
float ParseFloatOrFallback(const std::wstring& text, float fallback);
HWND CreateTrackedTooltipWindow(HWND owner, HFONT font);
void HideTrackedTooltipWindow(HWND tooltip, bool& visible);
void DestroyTrackedTooltipWindow(HWND& tooltip, bool& visible, std::wstring& textCache);
void ShowTrackedTooltipWindow(HWND owner, HWND tooltip, HFONT font, const std::wstring& text,
    std::wstring& textCache, bool& visible, POINT screenPt);

HWND CreateChildControl(HWND parent, HINSTANCE hInstance, LPCWSTR className, LPCWSTR text,
    DWORD style, int controlId = 0, DWORD exStyle = 0);
HWND CreatePanel(HWND parent, HINSTANCE hInstance, bool visible = false);
HWND CreateLabel(HWND parent, HINSTANCE hInstance, LPCWSTR text);
HWND CreateOwnerDrawButton(HWND parent, HINSTANCE hInstance, LPCWSTR text, int controlId);

} // namespace UiUtil
