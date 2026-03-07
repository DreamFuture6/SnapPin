#pragma once
#include "common.h"
#include "core/HistoryService.h"

class HistoryWindow {
public:
    enum class Action {
        Copy,
        Pin,
        OpenFile,
        ClearAll,
        Rename,
    };

    using ActionCallback = std::function<void(Action, const std::optional<HistoryItem>&, const std::wstring&)>;

    bool Show(HINSTANCE hInstance, const std::vector<HistoryItem>& items, ActionCallback callback);
    bool IsVisible() const { return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE; }
    HWND Hwnd() const { return hwnd_; }
    void Hide();
    void Close();
    void ReleasePreview();
    bool IsOpen() const { return hwnd_ != nullptr; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void RefreshList();
    void LayoutChildren(int width, int height);
    void PositionAsFlyout();
    void ShowFlyout();
    void HideFlyout();
    void ApplyFlyoutClip();
    void UpdateDpiFonts();
    void StepFlyoutAnimation();
    void UpdateSelection();
    void LoadPreview(const std::wstring& filePath);
    void DrawPreview(HDC hdc);
    int SelectedIndex() const;

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    HWND list_ = nullptr;
    HWND editTitle_ = nullptr;
    HWND lblMeta_ = nullptr;
    HWND lblPath_ = nullptr;
    HWND btnCopy_ = nullptr;
    HWND btnPin_ = nullptr;
    HWND btnClear_ = nullptr;
    RECT previewRect_{};
    std::unique_ptr<Gdiplus::Bitmap> previewBitmap_;
    HFONT titleFont_ = nullptr;
    HFONT normalFont_ = nullptr;
    HFONT btnFont_ = nullptr;
    UINT currentDpi_ = 96;
    int selectedIndex_ = -1;
    std::vector<HistoryItem> items_;
    ActionCallback callback_;

    UINT_PTR flyoutTimer_ = 0;
    int flyoutFrame_ = 0;
    RECT flyoutStartRect_{};
    RECT flyoutTargetRect_{};
    RECT flyoutWorkArea_{};
    bool flyoutHiding_ = false;
    bool deferPreviewUntilFlyoutShown_ = false;
};
