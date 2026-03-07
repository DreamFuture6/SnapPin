#pragma once
#include "common.h"
#include "core/ClipboardService.h"
#include "core/Exporter.h"
#include "core/Image.h"
#include "ui/GdiBitmapBuffer.h"

class PinWindow {
public:
    struct State {
        RECT windowRect{};
        float zoom = 1.0f;
    };

    using ClosedCallback = std::function<void(PinWindow*, const State&)>;
    using StateCallback = std::function<void(const State&)>;

    PinWindow();
    ~PinWindow();

    static void PreloadClass(HINSTANCE hInstance);
    bool Create(HINSTANCE hInstance, const Image& image, const std::optional<RECT>& screenRect,
        const std::optional<State>& initialState, ClosedCallback onClosed, StateCallback onStateUpdated);
    void Destroy();
    State GetState() const;

    HWND Hwnd() const { return hwnd_; }

private:
    // 窗口过程和消息处理
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnPaint();
    void OnContextMenu(POINT screenPt);
    void SetOpacity(BYTE alpha);
    void RebuildScaledBitmap(int width, int height);
    void RequestScaledBitmapRebuild(int width, int height);
    void CancelScaledBitmapRebuild();
    void NotifyState();
    void SaveToFile();
    void InitializeBrushes();  // 初始化 Gdiplus 刷子缓存

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    Image image_{};
    std::unique_ptr<Gdiplus::Bitmap> bitmap_;
    std::unique_ptr<Gdiplus::Bitmap> scaledBitmap_;
    int scaledBitmapWidth_ = 0;
    int scaledBitmapHeight_ = 0;
    ClipboardService clipboard_;
    Exporter exporter_;

    bool dragging_ = false;
    POINT dragStartScreen_{};
    RECT dragStartWindow_{};

    float zoom_ = 1.0f;
    BYTE opacity_ = 255;
    UINT cachedDpi_ = 96;  // 缓存 DPI，避免频繁调用 GetDpiForWindow()
    UINT_PTR zoomHintTimer_ = 0;
    DWORD zoomHintUntilTick_ = 0;
    bool hintShowsOpacity_ = false;
    UINT_PTR scaledRebuildTimer_ = 0;
    int pendingScaledWidth_ = 0;
    int pendingScaledHeight_ = 0;
    DWORD lastScaleInputTick_ = 0;
    GdiBitmapBuffer paintBuffer_{};

    // Gdiplus 对象缓存，避免每次 OnPaint 都创建临时对象
    std::unique_ptr<Gdiplus::SolidBrush> borderBrush_;    // 边框刷（白色，alpha 140）
    std::unique_ptr<Gdiplus::SolidBrush> bgHintBrush_;    // 缩放提示背景刷
    std::unique_ptr<Gdiplus::SolidBrush> textBrush_;      // 缩放提示文本刷
    std::unique_ptr<Gdiplus::ImageAttributes> imgAttrs_;  // 图像绘制属性缓存

    ClosedCallback onClosed_;
    StateCallback onStateUpdated_;
};
