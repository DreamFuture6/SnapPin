#pragma once
#include "common.h"
#include "core/Annotation.h"
#include "core/CaptureService.h"
#include "core/ClipboardService.h"
#include "core/Exporter.h"
#include "core/Image.h"
#include "ui/GdiBitmapBuffer.h"
#include "ui/ToolbarWindow.h"

enum class OverlayAction {
    Cancel,
    Save,
    QuickSave,
    Copy,
    CopyFile,
    Pin,
    Ocr,
};

struct OverlayResult {
    OverlayAction action = OverlayAction::Cancel;
    Image image;
    std::optional<RECT> selectionScreenRect;
};

class OverlayWindow {
public:
    using FinishedCallback = std::function<void(const OverlayResult&)>;

    OverlayWindow();
    ~OverlayWindow();

    static void PreloadUi(HINSTANCE hInstance);
    void SetGuideLinesEnabled(bool enabled) { guideLinesEnabled_ = enabled; }
    bool Show(HINSTANCE hInstance, const ScreenCapture& capture, bool fullScreenSelection, FinishedCallback callback,
        const std::optional<RECT>& fullSelectionScreenRect = std::nullopt);
    void Close();
    bool IsOpen() const { return hwnd_ != nullptr; }
    bool TryHandleColorCopyHotkey();
    void CancelCapture();
    void ExitToCursorMode();
    HWND Hwnd() const { return hwnd_; }
    bool IsInputSuppressed() const { return toolbar_.IsColorDialogOpen(); }

private:
    enum class Stage {
        Selecting,
        Annotating,
    };

    enum class DragMode {
        None,
        SelectingNew,
        MoveSelection,
        ResizeSelection,
        DrawShape,
        MoveShape,
        ResizeShape,
    };

    enum class HitKind {
        None,
        Inside,
        Left,
        Top,
        Right,
        Bottom,
        LeftTop,
        RightTop,
        LeftBottom,
        RightBottom,
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK HudWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnPaint();
    void OnHudPaint(HWND hudHwnd);
    void OnMouseDown(POINT p);
    void OnMouseMove(POINT p, WPARAM keys);
    void OnMouseUp(POINT p);
    void OnRightClick(POINT p);
    void OnMiddleClick(POINT p);
    void OnKeyDown(WPARAM vk, LPARAM lParam);
    void OnToolbarCommand(UINT id, UINT notifyCode);
    void EnterLongCaptureMode();
    void EnterWhiteboardMode();
    void OnLongCaptureTimer();
    void UpdateLongCaptureThumbnailCache(float dpiScale, bool force);
    bool CaptureTargetWindowFrame(Image& outFrame);
    void AppendLongCaptureFrame(const Image& frame, int targetOffset);
    void DrawLongCaptureThumbnail(Gdiplus::Graphics& g, float dpiScale);

    void UpdateHoverWindow(POINT screenPt);
    void ApplyToolbarStyle();
    void ApplyStyleToSelectedShape();
    void MarkSceneDirty();
    void EnsureStaticSceneBitmap();
    void EnsureHudWindows();
    void DestroyHudWindows();
    void RefreshFollowHudFromLastMouse();
    void UpdateHudWindows(std::optional<RECT> infoRect, std::optional<RECT> magnifierRect,
        std::optional<RECT> verticalGuideRect = std::nullopt, std::optional<RECT> horizontalGuideRect = std::nullopt);
    void DrawMagnifierPanel(Gdiplus::Graphics& g, const RECT& panelRect, POINT centerPt, float dpiScale) const;
    void DrawCursorInfoPanel(Gdiplus::Graphics& g, const RECT& panelRect, POINT centerPt, float dpiScale) const;
    void DrawEditingShortcutHint(Gdiplus::Graphics& g, float dpiScale, const RECT& selectionRect) const;
    void DrawCommittedShapesInSelection(Gdiplus::Graphics& g, const RECT& selectionRect, float dpiScale, bool selectionDragging) const;
    void DrawWhiteboardLayer(Gdiplus::Graphics& g, float dpiScale);
    void RenderStaticScene(Gdiplus::Graphics& g, float dpiScale);
    void DrawCurrentShapePreview(Gdiplus::Graphics& g, const RECT& selectionRect);
    std::optional<RECT> HitWindow(POINT screenPt) const;
    std::optional<RECT> ComputeCursorInfoRect(POINT p) const;
    std::optional<RECT> ComputeMagnifierRect(POINT p) const;
    bool IsCursorFollowUiActiveAt(POINT p) const;
    bool ComputeCrosshairGuideRects(POINT p, RECT& vertical, RECT& horizontal) const;
    bool IsPointInToolbarZone(POINT p, int inflatePx = 0) const;
    bool BeginTextEdit(POINT p);
    void EndTextEdit(bool commit);
    void DestroyTextEditControl();
    void DestroyTextEditBackground();
    void UpdateTextEditFont();
    void UpdateTextEditBackground();
    void ResizeTextEditToFit();
    bool ShouldShowCursorInfoOverlay() const;
    bool ShouldShowMagnifierOverlay() const;
    float DpiScaleForLocalPoint(POINT localPoint) const;
    float LogicalTextSizeToPixels(float logicalSize, POINT localPoint) const;
    void UpdateCursorVisual(POINT p);
    HCURSOR CursorForHit(HitKind hit) const;
    void SnapCursorToCrosshair();
    bool SaveSelectionWithoutHistory();

    bool HasSelection() const;
    RECT SelectionRectNormalized() const;
    void ClampSelectionToBounds();
    HitKind HitTestRectHandles(const RECT& rc, POINT p, int handleSize) const;

    int HitTestShape(POINT p, HitKind* outHit) const;
    void MoveShape(AnnotationShape& shape, int dx, int dy);
    void ResizeShape(AnnotationShape& shape, HitKind hit, POINT anchor, POINT current, bool keepRatio);

    void CommitCurrentShape();
    void PushUndo();
    void Undo();
    void Redo();

    RECT ScreenToLocalRect(const RECT& rc) const;
    POINT ScreenToLocalPoint(POINT p) const;
    POINT LocalToScreenPoint(POINT p) const;

    Image ComposeCurrent() const;
    Image ComposeBackgroundSelection() const;
    void Finish(OverlayAction action);

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;

    ScreenCapture capture_{};
    std::unique_ptr<Gdiplus::Bitmap> captureBitmap_;
    std::unique_ptr<Gdiplus::Bitmap> staticSceneBitmap_;
    bool staticSceneDirty_ = true;
    GdiBitmapBuffer paintBuffer_{};
    std::vector<CapturableWindow> windows_;

    Stage stage_ = Stage::Selecting;
    DragMode dragMode_ = DragMode::None;
    HitKind activeHit_ = HitKind::None;
    HitKind activeShapeHit_ = HitKind::None;

    RECT selection_{}; // local coordinates
    RECT initialSelection_{};
    POINT dragStart_{};
    POINT lastMouse_{};
    bool mouseLeaveTracking_ = false;
    bool precisionModeActive_ = false;
    POINT precisionLastRaw_{}; 
    double precisionMouseX_ = 0.0;
    double precisionMouseY_ = 0.0;
    RECT precisionBounds_{}; // local-space clamp bounds for shift precision mode

    std::optional<RECT> hoverWindowRect_;
    bool cursorInfoEnabled_ = false;
    bool followHudEnabled_ = true;
    bool guideLinesEnabled_ = true;
    bool colorHexMode_ = false;
    std::optional<RECT> lastInfoRect_;
    std::optional<RECT> lastMagnifierRect_;
    std::optional<RECT> lastVerticalGuideRect_;
    std::optional<RECT> lastHorizontalGuideRect_;
    HWND infoHudHwnd_ = nullptr;
    HWND magnifierHudHwnd_ = nullptr;
    HWND verticalGuideHudHwnd_ = nullptr;
    HWND horizontalGuideHudHwnd_ = nullptr;
    GdiBitmapBuffer infoHudBackBuffer_{};
    GdiBitmapBuffer magnifierHudBackBuffer_{};
    GdiBitmapBuffer verticalGuideHudBackBuffer_{};
    GdiBitmapBuffer horizontalGuideHudBackBuffer_{};

    bool longCaptureMode_ = false;
    bool whiteboardMode_ = false;
    UINT_PTR longCaptureTimer_ = 0;
    HWND longCaptureTargetHwnd_ = nullptr;
    Image longCaptureImage_{};
    Image longCaptureLastFrame_{};
    int longCaptureViewportOffsetY_ = 0;
    int longCaptureScrollDir_ = 0; // +1 append downward, -1 upward
    bool longCaptureMatchAccepted_ = true;
    std::optional<RECT> longCaptureThumbRect_;
    struct LongCaptureThumbCache {
        Image image{};
        int panelW = 0;
        int panelH = 0;
        int drawW = 0;
        int drawH = 0;
        double scale = 1.0;
        int viewportOffsetY = 0;
        int viewportW = 0;
        int viewportH = 0;
    };
    bool longCaptureThumbCacheReady_ = false;
    LongCaptureThumbCache longCaptureThumbCache_{};
    bool longCaptureThumbDirty_ = false;
    DWORD longCaptureThumbLastRenderTick_ = 0;

    ToolType tool_ = ToolType::None;
    COLORREF currentColor_ = RGB(255, 0, 0);
    float strokeWidth_ = 2.0f;
    bool fillEnabled_ = false;
    COLORREF fillColor_ = RGB(255, 255, 0);
    COLORREF textColor_ = RGB(255, 0, 0);
    float textSize_ = 18.0f;
    INT textStyle_ = Gdiplus::FontStyleRegular;
    int nextNumber_ = 1;

    HWND textEdit_ = nullptr;
    HFONT textEditFont_ = nullptr;
    HBITMAP textEditBgBitmap_ = nullptr;
    HBRUSH textEditBgBrush_ = nullptr;
    RECT textEditRect_{};
    AnnotationShape textDraftShape_{};
    bool textEditing_ = false;

    std::vector<AnnotationShape> shapes_;
    AnnotationShape currentShape_;
    bool hasCurrentShape_ = false;

    int selectedShape_ = -1;
    RECT selectedShapeBounds_{};

    std::vector<std::vector<AnnotationShape>> undoStack_;
    std::vector<std::vector<AnnotationShape>> redoStack_;

    ToolbarWindow toolbar_;
    bool toolbarHiddenByShiftPrecision_ = false;
    FinishedCallback callback_;
    Exporter exporter_;
    ClipboardService clipboard_;
};
