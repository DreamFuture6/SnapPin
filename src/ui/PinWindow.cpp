#include "ui/PinWindow.h"
#include "ui/FontManager.h"
#include "ui/UiUtil.h"
#include "ui/WindowUtil.h"
#include "common/KnownFolderUtil.h"

namespace
{
    constexpr UINT ID_PIN_COPY                    = 50001;
    constexpr UINT ID_PIN_SAVE                    = 50002;
    constexpr UINT ID_PIN_CLOSE                   = 50007;
    constexpr UINT_PTR ID_PIN_ZOOM_HINT_TIMER     = 1;
    constexpr UINT_PTR ID_PIN_SCALE_REBUILD_TIMER = 2;
    constexpr wchar_t kPinWindowClassName[]       = L"SnapPinPinWindowClass";

    float ComputeAdaptiveMaxZoom(const Image &image)
    {
        const double pixels = static_cast<double>(std::max(1, image.width)) * static_cast<double>(std::max(1, image.height));
        if (pixels <= 80000.0) { // <= 0.08MP
            return 4.0f;         // 400%
        }
        if (pixels <= 160000.0) { // <= 0.16MP
            return 3.5f;          // 350%
        }
        if (pixels <= 300000.0) { // <= 0.3MP
            return 3.0f;          // 300%
        }
        if (pixels <= 600000.0) { // <= 0.6MP
            return 2.5f;          // 250%
        }
        if (pixels <= 1000000.0) { // <= 1MP
            return 2.0f;          // 200%
        }
        return 1.5f; // 150%
    }

    std::filesystem::path DefaultSavePath()
    {
        std::filesystem::path out = KnownFolderUtil::GetPathOr(FOLDERID_Pictures, std::filesystem::temp_directory_path());
        out /= (L"SnapPin_Pin_" + FormatNowForFile() + L".png");
        return out;
    }

}

PinWindow::PinWindow() = default;
PinWindow::~PinWindow()
{
    Destroy();
}

void PinWindow::PreloadClass(HINSTANCE hInstance)
{
    static std::once_flag once;
    WindowUtil::RegisterWindowClassOnce(
        once,
        hInstance,
        kPinWindowClassName,
        PinWindow::WndProc,
        LoadCursorW(nullptr, IDC_ARROW));
}

bool PinWindow::Create(HINSTANCE hInstance, const Image &image, const std::optional<RECT> &screenRect,
                       const std::optional<State> &initialState, ClosedCallback onClosed, StateCallback onStateUpdated)
{
    if (!image.IsValid()) {
        return false;
    }

    hInstance_      = hInstance;
    image_          = image;
    onClosed_       = std::move(onClosed);
    onStateUpdated_ = std::move(onStateUpdated);
    cachedDpi_      = 96;  // 初始化为默认 DPI

    PreloadClass(hInstance_);

    int width           = std::clamp(image.width, 120, 1600);
    int height          = std::clamp(image.height, 80, 1200);
    int posX            = CW_USEDEFAULT;
    int posY            = CW_USEDEFAULT;
    const float maxZoom = ComputeAdaptiveMaxZoom(image_);
    if (initialState.has_value() && IsRectValid(initialState->windowRect)) {
        RECT rc = initialState->windowRect;
        width   = std::max(64, RectWidth(rc));
        height  = std::max(64, RectHeight(rc));
        posX    = rc.left;
        posY    = rc.top;
        zoom_   = std::clamp(initialState->zoom, 0.2f, maxZoom);
    } else if (screenRect.has_value()) {
        RECT rc = *screenRect;
        width   = std::max(64, RectWidth(rc));
        height  = std::max(64, RectHeight(rc));
        posX    = rc.left;
        posY    = rc.top;
    }

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        kPinWindowClassName,
        L"",
        WS_POPUP,
        posX,
        posY,
        width,
        height,
        nullptr,
        nullptr,
        hInstance_,
        this);

    if (!hwnd_) {
        return false;
    }

    bitmap_ = std::make_unique<Gdiplus::Bitmap>(image_.width, image_.height, PixelFormat32bppARGB);
    if (bitmap_) {
        Gdiplus::Rect r(0, 0, image_.width, image_.height);
        Gdiplus::BitmapData data{};
        if (bitmap_->LockBits(&r, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) == Gdiplus::Ok) {
            for (int y = 0; y < image_.height; ++y) {
                uint8_t *dst       = static_cast<uint8_t *>(data.Scan0) + static_cast<size_t>(data.Stride) * static_cast<size_t>(y);
                const uint8_t *src = image_.bgra.data() + static_cast<size_t>(image_.width) * static_cast<size_t>(y) * 4;
                memcpy(dst, src, static_cast<size_t>(image_.width) * 4);
            }
            bitmap_->UnlockBits(&data);
        }
    }
    RebuildScaledBitmap(width, height);

    SetLayeredWindowAttributes(hwnd_, 0, opacity_, LWA_ALPHA);
    // 获取实际的窗口 DPI 并缓存
    cachedDpi_ = UiUtil::GetWindowDpiSafe(hwnd_);
    InitializeBrushes();  // 初始化 Gdiplus 对象缓存
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void PinWindow::Destroy()
{
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    paintBuffer_.Reset();
    // Gdiplus 对象由 unique_ptr 自动销毁
    borderBrush_.reset();
    bgHintBrush_.reset();
    textBrush_.reset();
    imgAttrs_.reset();
}

LRESULT CALLBACK PinWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PinWindow *self = reinterpret_cast<PinWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
        self     = reinterpret_cast<PinWindow *>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT PinWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        OnPaint();
        return 0;
    case WM_DPICHANGED: {
        // 更新缓存的 DPI 值
        cachedDpi_ = GetDpiForWindow(hwnd_);
        return 0;
    }
    case WM_LBUTTONDOWN:
        SetFocus(hwnd_);
        dragging_ = true;
        GetCursorPos(&dragStartScreen_);
        GetWindowRect(hwnd_, &dragStartWindow_);
        SetCapture(hwnd_);
        return 0;
    case WM_MOUSEMOVE:
        if (dragging_) {
            POINT cur{};
            GetCursorPos(&cur);
            int dx = cur.x - dragStartScreen_.x;
            int dy = cur.y - dragStartScreen_.y;
            SetWindowPos(hwnd_, HWND_TOPMOST,
                         dragStartWindow_.left + dx,
                         dragStartWindow_.top + dy,
                         0, 0,
                         SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (dragging_) {
            dragging_ = false;
            ReleaseCapture();
            NotifyState();
        }
        return 0;
    case WM_RBUTTONUP: {
        POINT p{};
        GetCursorPos(&p);
        OnContextMenu(p);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int zoomWheelDelta    = 0;
        int opacityWheelDelta = 0;
        auto classifyWheel    = [&](WPARAM wheelWParam) {
            const bool msgShift = (GET_KEYSTATE_WPARAM(wheelWParam) & MK_SHIFT) != 0;
            const int d         = GET_WHEEL_DELTA_WPARAM(wheelWParam);
            if (msgShift) {
                opacityWheelDelta += d;
            } else {
                zoomWheelDelta += d;
            }
        };
        classifyWheel(wParam);

        MSG pending{};
        while (PeekMessageW(&pending, hwnd_, WM_MOUSEWHEEL, WM_MOUSEWHEEL, PM_REMOVE)) {
            classifyWheel(pending.wParam);
        }

        if (opacityWheelDelta != 0) {
            int steps = opacityWheelDelta / WHEEL_DELTA;
            if (steps == 0) {
                steps = (opacityWheelDelta > 0) ? 1 : -1;
            }
            int opacityPercent = std::clamp(static_cast<int>(std::lround((static_cast<float>(opacity_) / 255.0f) * 100.0f)), 10, 100);
            opacityPercent     = std::clamp(opacityPercent + steps * 5, 10, 100);
            const BYTE alpha   = static_cast<BYTE>(std::clamp(static_cast<int>(std::lround((opacityPercent / 100.0f) * 255.0f)), 26, 255));
            SetOpacity(alpha);
            hintShowsOpacity_ = true;
        }

        if (zoomWheelDelta != 0) {
            int steps = zoomWheelDelta / WHEEL_DELTA;
            if (steps == 0) {
                steps = (zoomWheelDelta > 0) ? 1 : -1;
            }
            const float maxZoom = ComputeAdaptiveMaxZoom(image_);
            const float factor  = std::pow(1.1f, static_cast<float>(steps));
            zoom_               = std::clamp(zoom_ * factor, 0.2f, maxZoom);
            const int w         = std::max(64, static_cast<int>(std::lround(image_.width * zoom_)));
            const int h         = std::max(64, static_cast<int>(std::lround(image_.height * zoom_)));
            RequestScaledBitmapRebuild(w, h);
            SetWindowPos(hwnd_, nullptr, 0, 0, w, h,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            NotifyState();
            hintShowsOpacity_ = false;
        }

        if (zoomWheelDelta == 0 && opacityWheelDelta == 0) {
            return 0;
        }

        hintShowsOpacity_ = (opacityWheelDelta != 0 && zoomWheelDelta == 0);
        zoomHintUntilTick_ = GetTickCount() + 1200;
        if (zoomHintTimer_ != 0) {
            KillTimer(hwnd_, zoomHintTimer_);
            zoomHintTimer_ = 0;
        }
        zoomHintTimer_      = SetTimer(hwnd_, ID_PIN_ZOOM_HINT_TIMER, 1200, nullptr);
        lastScaleInputTick_ = GetTickCount();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    case WM_SIZE: {
        const int w = std::max(1, static_cast<int>(LOWORD(lParam)));
        const int h = std::max(1, static_cast<int>(HIWORD(lParam)));
        if (w > 0 && h > 0) {
            RequestScaledBitmapRebuild(w, h);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if ((GetKeyState(VK_CONTROL) & 0x8000) && (wParam == 'C' || wParam == 'c')) {
            clipboard_.CopyImage(hwnd_, image_);
            return 0;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_TIMER:
        if (wParam == ID_PIN_ZOOM_HINT_TIMER) {
            if (GetTickCount() >= zoomHintUntilTick_) {
                if (zoomHintTimer_ != 0) {
                    KillTimer(hwnd_, zoomHintTimer_);
                    zoomHintTimer_ = 0;
                }
                zoomHintUntilTick_ = 0;
                hintShowsOpacity_  = false;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        if (wParam == ID_PIN_SCALE_REBUILD_TIMER) {
            CancelScaledBitmapRebuild();
            const DWORD now = GetTickCount();
            if ((now - lastScaleInputTick_) < 120) {
                // Still in active wheel burst: keep only the latest requested size.
                if (pendingScaledWidth_ > 0 && pendingScaledHeight_ > 0 && hwnd_) {
                    scaledRebuildTimer_ = SetTimer(hwnd_, ID_PIN_SCALE_REBUILD_TIMER, 60, nullptr);
                }
                return 0;
            }
            MSG wheelMsg{};
            if (PeekMessageW(&wheelMsg, hwnd_, WM_MOUSEWHEEL, WM_MOUSEWHEEL, PM_NOREMOVE)) {
                if (pendingScaledWidth_ > 0 && pendingScaledHeight_ > 0 && hwnd_) {
                    scaledRebuildTimer_ = SetTimer(hwnd_, ID_PIN_SCALE_REBUILD_TIMER, 60, nullptr);
                }
                return 0;
            }
            if (pendingScaledWidth_ > 0 && pendingScaledHeight_ > 0) {
                RebuildScaledBitmap(pendingScaledWidth_, pendingScaledHeight_);
                pendingScaledWidth_  = 0;
                pendingScaledHeight_ = 0;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_PIN_COPY:
            clipboard_.CopyImage(hwnd_, image_);
            break;
        case ID_PIN_SAVE:
            SaveToFile();
            break;
        case ID_PIN_CLOSE:
            Destroy();
            break;
        default:
            break;
        }
        return 0;
    case WM_DESTROY:
        if (zoomHintTimer_ != 0) {
            KillTimer(hwnd_, zoomHintTimer_);
            zoomHintTimer_ = 0;
        }
        CancelScaledBitmapRebuild();
        if (onClosed_) {
            onClosed_(this, GetState());
        }
        hwnd_ = nullptr;
        return 0;
    default:
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

void PinWindow::InitializeBrushes()
{
    try {
        // 初始化边框刷（白色，alpha=140）
        borderBrush_ = std::make_unique<Gdiplus::SolidBrush>(
            Gdiplus::Color(140, 255, 255, 255));

        // 初始化缩放提示背景刷
        bgHintBrush_ = std::make_unique<Gdiplus::SolidBrush>(
            Gdiplus::Color(170, 16, 18, 22));

        // 初始化文本刷（白色，alpha=255）
        textBrush_ = std::make_unique<Gdiplus::SolidBrush>(
            Gdiplus::Color(255, 240, 245, 255));

        // 初始化图像属性，设置平铺模式
        imgAttrs_ = std::make_unique<Gdiplus::ImageAttributes>();
        if (imgAttrs_) {
            imgAttrs_->SetWrapMode(Gdiplus::WrapModeTileFlipXY);
        }
    } catch (...) {
        // 初始化失败，对象仍为 nullptr，OnPaint 中会处理
    }
}

void PinWindow::OnPaint()
{
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int width  = std::max(1, RectWidth(rc));
    const int height = std::max(1, RectHeight(rc));
    HDC memDc        = nullptr;
    HGDIOBJ oldObj   = nullptr;
    if (paintBuffer_.Ensure(hdc, width, height) && paintBuffer_.dc() && paintBuffer_.bitmap()) {
        memDc  = paintBuffer_.dc();
        oldObj = SelectObject(memDc, paintBuffer_.bitmap());
    }
    HDC paintDc = memDc ? memDc : hdc;

    Gdiplus::Graphics g(paintDc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);

    const bool hasScaledForTarget = scaledBitmap_ &&
                                    scaledBitmapWidth_ == width &&
                                    scaledBitmapHeight_ == height;
    const bool interactiveScale = (lastScaleInputTick_ != 0) && ((GetTickCount() - lastScaleInputTick_) < 140);
    if (hasScaledForTarget) {
        g.DrawImage(scaledBitmap_.get(), 0, 0, width, height);
    } else if (bitmap_) {
        // Fast fallback during continuous zoom; prefer low-cost sampling to keep UI responsive.
        if (interactiveScale) {
            g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
            g.DrawImage(bitmap_.get(),
                        Gdiplus::Rect(0, 0, width, height),
                        0, 0, image_.width, image_.height,
                        Gdiplus::UnitPixel);
        } else {
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
            // 使用缓存的 ImageAttributes，避免每次都创建
            if (imgAttrs_) {
                g.DrawImage(bitmap_.get(),
                            Gdiplus::Rect(0, 0, width, height),
                            0, 0, image_.width, image_.height,
                            Gdiplus::UnitPixel, imgAttrs_.get());
            } else {
                // 备用方案：如果缓存为空，创建临时对象
                Gdiplus::ImageAttributes attrs;
                attrs.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
                g.DrawImage(bitmap_.get(),
                            Gdiplus::Rect(0, 0, width, height),
                            0, 0, image_.width, image_.height,
                            Gdiplus::UnitPixel, &attrs);
            }
        }
    }

    const int w = RectWidth(rc);
    const int h = RectHeight(rc);
    if (w > 1 && h > 1 && borderBrush_) {
        // 使用缓存的刷子，避免临时对象创建
        g.FillRectangle(borderBrush_.get(), 0, 0, w, 1);
        g.FillRectangle(borderBrush_.get(), 0, h - 1, w, 1);
        g.FillRectangle(borderBrush_.get(), 0, 0, 1, h);
        g.FillRectangle(borderBrush_.get(), w - 1, 0, 1, h);
    }

    if (zoomHintUntilTick_ != 0 && GetTickCount() <= zoomHintUntilTick_) {
        wchar_t zoomText[32]{};
        if (hintShowsOpacity_) {
            swprintf_s(zoomText, L"透明度 %d%%",
                       std::clamp(static_cast<int>(std::lround((static_cast<float>(opacity_) / 255.0f) * 100.0f)), 10, 100));
        } else {
            const int maxPercent = std::clamp(
                static_cast<int>(std::lround(ComputeAdaptiveMaxZoom(image_) * 100.0f)),
                150,
                400);
            swprintf_s(zoomText, L"缩放 %d%%",
                       std::clamp(static_cast<int>(std::lround(zoom_ * 100.0f)), 20, maxPercent));
        }
        // 使用缓存的 DPI 值，避免多次系统调用
        const float scale = static_cast<float>(cachedDpi_) / 96.0f;
        const float fontSize = std::max(10.0f, 12.0f * scale);

        // 使用 FontManager 缓存字体，避免每次绘制都创建
        auto& fontMgr = FontManager::Instance();
        Gdiplus::Font* pFont = fontMgr.GetFont(L"Segoe UI", fontSize, Gdiplus::FontStyleBold);
        if (pFont && bgHintBrush_ && textBrush_) {
            // 使用缓存的字体和刷子（常规情况，性能最优）
            Gdiplus::RectF measured;
            g.MeasureString(zoomText, -1, pFont, Gdiplus::PointF(0, 0), &measured);
            const float padX = 6.0f * scale;
            const float padY = 3.0f * scale;
            Gdiplus::RectF box(6.0f * scale, 6.0f * scale, measured.Width + padX * 2.0f, measured.Height + padY * 2.0f);
            g.FillRectangle(bgHintBrush_.get(), box);
            g.DrawString(zoomText, -1, pFont, Gdiplus::RectF(box.X + padX, box.Y + padY, box.Width, box.Height), nullptr, textBrush_.get());
        } else if (bgHintBrush_ && textBrush_) {
            // 字体缓存满时的备用方案，仍使用缓存的刷子
            Gdiplus::FontFamily ff(L"Segoe UI");
            Gdiplus::Font tempFont(&ff, fontSize, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::RectF measured;
            g.MeasureString(zoomText, -1, &tempFont, Gdiplus::PointF(0, 0), &measured);
            const float padX = 6.0f * scale;
            const float padY = 3.0f * scale;
            Gdiplus::RectF box(6.0f * scale, 6.0f * scale, measured.Width + padX * 2.0f, measured.Height + padY * 2.0f);
            g.FillRectangle(bgHintBrush_.get(), box);
            g.DrawString(zoomText, -1, &tempFont, Gdiplus::RectF(box.X + padX, box.Y + padY, box.Width, box.Height), nullptr, textBrush_.get());
        }
    }

    if (memDc) {
        BitBlt(hdc, 0, 0, width, height, memDc, 0, 0, SRCCOPY);
    }
    if (oldObj) {
        SelectObject(memDc, oldObj);
    }

    EndPaint(hwnd_, &ps);
}

void PinWindow::OnContextMenu(POINT screenPt)
{
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    MENUINFO mi{};
    mi.cbSize  = sizeof(mi);
    mi.fMask   = MIM_STYLE;
    mi.dwStyle = MNS_NOCHECK;
    SetMenuInfo(menu, &mi);

    AppendMenuW(menu, MF_STRING, ID_PIN_COPY, L"复制");
    AppendMenuW(menu, MF_STRING, ID_PIN_SAVE, L"保存");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Shift+鼠标滚轮：调整透明度");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_PIN_CLOSE, L"关闭");

    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}

void PinWindow::SetOpacity(BYTE alpha)
{
    opacity_ = static_cast<BYTE>(std::clamp(static_cast<int>(alpha), 26, 255));
    SetLayeredWindowAttributes(hwnd_, 0, opacity_, LWA_ALPHA);
}

void PinWindow::RebuildScaledBitmap(int width, int height)
{
    if (!bitmap_) {
        scaledBitmap_.reset();
        scaledBitmapWidth_  = 0;
        scaledBitmapHeight_ = 0;
        return;
    }

    width        = std::max(1, width);
    height       = std::max(1, height);
    auto rebuilt = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppARGB);
    if (!rebuilt || rebuilt->GetLastStatus() != Gdiplus::Ok) {
        scaledBitmap_.reset();
        scaledBitmapWidth_  = 0;
        scaledBitmapHeight_ = 0;
        return;
    }

    Gdiplus::Graphics g(rebuilt.get());
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    g.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    Gdiplus::ImageAttributes attrs;
    attrs.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
    g.DrawImage(bitmap_.get(),
                Gdiplus::Rect(0, 0, width, height),
                0, 0, image_.width, image_.height,
                Gdiplus::UnitPixel, &attrs);

    scaledBitmap_       = std::move(rebuilt);
    scaledBitmapWidth_  = width;
    scaledBitmapHeight_ = height;
}

void PinWindow::RequestScaledBitmapRebuild(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return;
    }
    pendingScaledWidth_  = width;
    pendingScaledHeight_ = height;
    lastScaleInputTick_  = GetTickCount();
    if (scaledRebuildTimer_ != 0) {
        KillTimer(hwnd_, scaledRebuildTimer_);
        scaledRebuildTimer_ = 0;
    }
    if (hwnd_) {
        scaledRebuildTimer_ = SetTimer(hwnd_, ID_PIN_SCALE_REBUILD_TIMER, 120, nullptr);
    }
}

void PinWindow::CancelScaledBitmapRebuild()
{
    if (scaledRebuildTimer_ != 0 && hwnd_) {
        KillTimer(hwnd_, scaledRebuildTimer_);
    }
    scaledRebuildTimer_ = 0;
}

PinWindow::State PinWindow::GetState() const
{
    State state{};
    state.zoom = zoom_;
    if (hwnd_) {
        GetWindowRect(hwnd_, &state.windowRect);
    }
    return state;
}

void PinWindow::NotifyState()
{
    if (onStateUpdated_) {
        onStateUpdated_(GetState());
    }
}

void PinWindow::SaveToFile()
{
    NotifyState();
    auto path = DefaultSavePath();
    exporter_.SaveImage(image_, path, false);
    ShellExecuteW(hwnd_, L"open", path.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
