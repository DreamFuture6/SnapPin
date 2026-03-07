#pragma once
#include "common.h"
#include "core/Annotation.h"

class ToolbarWindow {
public:
    static void PreloadClass(HINSTANCE hInstance);
    static void WarmUp(HWND parent, HINSTANCE hInstance);
    bool Create(HWND parent, HINSTANCE hInstance);
    void Destroy();

    void ShowNear(const RECT& selection, const RECT& overlayBounds);
    void Hide();
    void ApplyColorLabels();
    void SetActiveTool(ToolType tool);
    void SetLongCaptureMode(bool enabled);
    void SetWhiteboardMode(bool enabled);

    float StrokeWidth() const;
    COLORREF StrokeColor() const { return strokeColor_; }
    bool FillEnabled() const;
    COLORREF FillColor() const { return fillColor_; }
    float TextSize() const;
    INT TextStyle() const;
    COLORREF TextColor() const { return textColor_; }

    bool ChooseStrokeColor(HWND owner);
    bool ChooseFillColor(HWND owner);
    bool ChooseTextColor(HWND owner);

    bool IsModeButtonId(UINT id) const;
    ToolType ToolFromButtonId(UINT id) const;
    bool IsColorDialogOpen() const { return colorDialogOpen_; }

    HWND Hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK ToolbarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static UINT_PTR CALLBACK ColorDialogHookProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool PickColor(HWND owner, COLORREF& inOutColor);
    std::wstring TooltipForControlId(UINT id) const;
    void CreateTooltips();
    void DestroyTooltips();
    void CreateButtons();
    void RelayoutVisibleControls();
    void DrawButton(const DRAWITEMSTRUCT* dis);
    void PaintBackground(HDC hdc);
    void UpdateHoverState();
    void ActivateTrackedTooltip(UINT id, POINT screenPt);
    void HideTrackedTooltip();
    HWND ControlById(UINT id) const;
    bool IsButtonLikeControlId(UINT id) const;
    bool IsComboControl(HWND hwnd) const;
    bool IsComboList(HWND hwnd) const;
    bool IsComboRelatedControl(HWND hwnd) const;
    UINT ButtonIdFromTool(ToolType tool) const;
    void UpdateDpiFont();

    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    HFONT font_ = nullptr;
    HFONT iconFont_ = nullptr;
    HWND tooltip_ = nullptr;
    UINT dpi_ = 96;
    int toolbarWidth_ = 920;
    int toolbarHeight_ = 36;
    int comboPopupHeight_ = 220;

    std::unordered_map<UINT, HWND> buttons_;
    std::unordered_map<UINT, int> buttonWidths_;
    std::vector<UINT> modeButtonIds_;
    std::vector<UINT> actionButtonIds_;
    std::vector<int> separatorXs_;
    HBRUSH comboBgBrush_ = nullptr;
    UINT hoveredControlId_ = 0;
    UINT pendingHoverId_ = 0;
    DWORD hoverSinceTick_ = 0;
    bool trackedTooltipVisible_ = false;
    std::wstring trackedTooltipText_;

    HWND cmbStroke_ = nullptr;
    HWND btnStrokeColor_ = nullptr;
    HWND chkFill_ = nullptr;
    HWND btnFillColor_ = nullptr;
    HWND cmbTextSize_ = nullptr;
    HWND cmbTextStyle_ = nullptr;
    HWND btnTextColor_ = nullptr;

    ToolType activeTool_ = ToolType::None;
    float strokeWidthValue_ = 2.0f;
    float textSizeValue_ = 18.0f;
    INT textStyleValue_ = Gdiplus::FontStyleRegular;
    bool fillEnabledValue_ = false;
    COLORREF strokeColor_ = RGB(255, 0, 0);
    COLORREF fillColor_ = RGB(255, 255, 0);
    COLORREF textColor_ = RGB(255, 0, 0);
    bool colorDialogOpen_ = false;
    POINT colorDialogAnchor_ { CW_USEDEFAULT, CW_USEDEFAULT };
    DWORD colorDialogBlockUntilTick_ = 0;
    bool longCaptureMode_ = false;
    bool whiteboardMode_ = false;
};
