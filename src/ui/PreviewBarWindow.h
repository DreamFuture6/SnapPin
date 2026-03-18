#pragma once

#include "common.h"
#include "core/ScreenRecorder.h"

class PreviewBarWindow {
public:
    static void PreloadClass(HINSTANCE hInstance);
    bool Create(HWND parent, HINSTANCE hInstance);
    void Destroy();

    void ShowNear(const RECT& selection, const RECT& overlayBounds);
    void Hide();
    HWND Hwnd() const { return hwnd_; }

    void SetPlaying(bool playing);
    void SetPreviewMetrics(LONGLONG duration100ns, LONGLONG position100ns);

    float PlaybackRate() const;
    VideoExportQuality ExportQuality() const;
    LONGLONG SliderSeekPosition100ns() const;
    bool IsSliderTracking() const { return sliderTracking_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Relayout();
    void UpdateTimeLabel();
    void NotifyParent(UINT id, UINT code = 0, LPARAM lParam = 0) const;
    void InvalidateProgress(double oldRatio = -1.0);
    void InvalidateButton(UINT id);
    void UpdateHoverState(POINT pt);
    void BeginProgressDrag(POINT pt);
    void UpdateProgressFromPoint(POINT pt, bool notifyParent);
    void EndProgressDrag(bool notifyParent);
    RECT ProgressFillRectForRatio(double ratio) const;
    UINT HitTest(POINT pt) const;
    RECT ButtonRect(UINT id) const;
    std::wstring ButtonText(UINT id) const;
    void DrawButton(HDC hdc, UINT id, const RECT& rc, bool hot, bool pressed) const;
    void DrawProgress(HDC hdc) const;
    void DrawComboItem(const DRAWITEMSTRUCT* dis) const;
    std::wstring TooltipForControlId(UINT id) const;
    void CreateTooltips();
    void DestroyTooltips();
    void HideTrackedTooltip();
    void ActivateTrackedTooltip(UINT id, POINT screenPt);
    void UpdateTrackedTooltip(UINT id, POINT screenPt);

    HWND hwnd_ = nullptr;
    HWND parent_ = nullptr;
    HFONT font_ = nullptr;
    HFONT iconFont_ = nullptr;
    HWND tooltip_ = nullptr;
    UINT dpi_ = 96;
    int width_ = 560;
    int height_ = 78;
    bool sliderTracking_ = false;
    bool playing_ = false;
    bool mouseTracking_ = false;
    bool progressHot_ = false;
    bool progressPressed_ = false;
    UINT hoveredButtonId_ = 0;
    UINT pressedButtonId_ = 0;
    LONGLONG duration100ns_ = 0;
    LONGLONG position100ns_ = 0;
    double progressRatio_ = 0.0;
    std::wstring timeText_ = L"00:00 / 00:00";

    RECT timeRect_{};
    RECT progressTrackRect_{};
    RECT playPauseRect_{};
    RECT speedRect_{};
    RECT qualityRect_{};
    RECT retryRect_{};
    RECT exportRect_{};
    RECT closeRect_{};

    HWND cmbSpeed_ = nullptr;
    HWND cmbQuality_ = nullptr;
    HBRUSH comboBgBrush_ = nullptr;
    UINT openComboControlId_ = 0;
    int openComboSelectionIndex_ = -1;
    UINT pendingHoverId_ = 0;
    DWORD hoverSinceTick_ = 0;
    bool trackedTooltipVisible_ = false;
    std::wstring trackedTooltipText_;
    bool windowDragging_ = false;
    POINT dragStartPoint_{};
};
