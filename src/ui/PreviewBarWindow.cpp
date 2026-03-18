#include "ui/PreviewBarWindow.h"

#include "app/AppIds.h"
#include "ui/ThemeColors.h"
#include "ui/ToolbarWindow.h"
#include "ui/UiUtil.h"
#include "ui/WindowProc.h"
#include "ui/WindowUtil.h"

namespace {

constexpr wchar_t kPreviewBarWindowClassName[] = L"SnapPinPreviewBarClass";
constexpr UINT kPreviewProgressHitId = 0xF0FF;

std::wstring FormatDuration(LONGLONG duration100ns)
{
    const LONGLONG totalSeconds = std::max<LONGLONG>(0, duration100ns / 10'000'000LL);
    const int hours = static_cast<int>(totalSeconds / 3600);
    const int minutes = static_cast<int>((totalSeconds % 3600) / 60);
    const int seconds = static_cast<int>(totalSeconds % 60);
    wchar_t buffer[32]{};
    if (hours > 0) {
        swprintf_s(buffer, L"%02d:%02d:%02d", hours, minutes, seconds);
    } else {
        swprintf_s(buffer, L"%02d:%02d", minutes, seconds);
    }
    return buffer;
}

void ConfigureComboControl(HWND combo, int controlHeight, int visibleItems)
{
    UiUtil::ConfigureComboControl(combo, controlHeight, visibleItems, 18, 4);
}

void ApplyStableComboTheme(HWND combo)
{
    UiUtil::ApplyStableComboTheme(combo);
}

void MakeRoundRectPath(const Gdiplus::RectF& rect, float radius, Gdiplus::GraphicsPath& path)
{
    const float r = std::min(radius, std::min(rect.Width, rect.Height) * 0.5f);
    const float d = r * 2.0f;
    path.Reset();
    path.AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

} // namespace

void PreviewBarWindow::PreloadClass(HINSTANCE hInstance)
{
    static std::once_flag once;
    WindowUtil::RegisterWindowClassOnce(
        once,
        hInstance,
        kPreviewBarWindowClassName,
        PreviewBarWindow::WndProc,
        LoadCursorW(nullptr, IDC_ARROW),
        0,
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
}

bool PreviewBarWindow::Create(HWND parent, HINSTANCE hInstance)
{
    if (hwnd_) {
        return true;
    }
    parent_ = parent;
    PreloadClass(hInstance);

    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOPARENTNOTIFY,
                            kPreviewBarWindowClassName,
                            L"SnapPin Preview Bar",
                            WS_POPUP,
                            0, 0, width_, height_,
                            parent,
                            nullptr,
                            hInstance,
                            this);
    if (!hwnd_) {
        return false;
    }

    // 确保没有 WS_EX_TRANSPARENT（防止点击穿透）
    LONG_PTR exStyles = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (exStyles & WS_EX_TRANSPARENT) {
        exStyles &= ~WS_EX_TRANSPARENT;
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyles);
    }

    dpi_ = GetDpiForWindow(parent_) ? GetDpiForWindow(parent_) : 96;
    const int fontPx = -MulDiv(13, static_cast<int>(dpi_), 96);
    const int iconFontPx = -MulDiv(16, static_cast<int>(dpi_), 96);
    font_ = CreateFontW(fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    iconFont_ = CreateFontW(iconFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_SWISS, L"Segoe UI Symbol");

    cmbSpeed_ = CreateWindowW(WC_COMBOBOXW,
                              L"",
                              WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
                              0, 0, 90, 240,
                              hwnd_,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOOL_PREVIEW_SPEED)),
                              hInstance,
                              nullptr);
    cmbQuality_ = CreateWindowW(WC_COMBOBOXW,
                                L"",
                                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL,
                                0, 0, 110, 240,
                                hwnd_,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TOOL_PREVIEW_EXPORT_QUALITY)),
                                hInstance,
                                nullptr);

    const wchar_t* speedValues[] = {L"0.2X", L"0.5X", L"0.8X", L"1.0X", L"1.2X", L"1.5X", L"1.8X", L"2.0X"};
    for (const wchar_t* value : speedValues) {
        SendMessageW(cmbSpeed_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    }
    SendMessageW(cmbSpeed_, CB_SETCURSEL, 3, 0);

    const wchar_t* qualityValues[] = {L"\u8F7B\u91CF\u753B\u8D28", L"\u6807\u51C6\u753B\u8D28", L"\u539F\u59CB\u753B\u8D28"};
    for (const wchar_t* value : qualityValues) {
        SendMessageW(cmbQuality_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value));
    }
    SendMessageW(cmbQuality_, CB_SETCURSEL, 2, 0);

    SendMessageW(cmbSpeed_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    SendMessageW(cmbQuality_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    ApplyStableComboTheme(cmbSpeed_);
    ApplyStableComboTheme(cmbQuality_);
    ConfigureComboControl(cmbSpeed_, 28, 8);
    ConfigureComboControl(cmbQuality_, 28, 3);

    CreateTooltips();
    UpdateTimeLabel();
    Relayout();
    return true;
}

void PreviewBarWindow::Destroy()
{
    DestroyTooltips();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    cmbSpeed_ = nullptr;
    cmbQuality_ = nullptr;

    if (comboBgBrush_) {
        DeleteObject(comboBgBrush_);
        comboBgBrush_ = nullptr;
    }
    if (iconFont_) {
        DeleteObject(iconFont_);
        iconFont_ = nullptr;
    }
    if (font_) {
        DeleteObject(font_);
        font_ = nullptr;
    }
}

void PreviewBarWindow::ShowNear(const RECT& selection, const RECT& overlayBounds)
{
    if (!hwnd_) {
        return;
    }
    (void)overlayBounds;
    const UINT nextDpi = parent_ && GetDpiForWindow(parent_) ? GetDpiForWindow(parent_) : dpi_;
    if (nextDpi != dpi_) {
        dpi_ = nextDpi;
        Relayout();
    }

    const int margin = 8;
    int x = selection.left;
    int y = selection.bottom + margin;
    if (parent_) {
        POINT screenPoint{x, y};
        ClientToScreen(parent_, &screenPoint);
        x = screenPoint.x;
        y = screenPoint.y;
    }

    const bool wasVisible = IsWindowVisible(hwnd_) != FALSE;
    UINT flags = SWP_NOACTIVATE;
    if (wasVisible) {
        flags |= SWP_NOOWNERZORDER;
    } else {
        flags |= SWP_SHOWWINDOW;
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width_, height_, flags);
}

void PreviewBarWindow::Hide()
{
    if (!hwnd_) {
        return;
    }
    if (GetCapture() == hwnd_) {
        ReleaseCapture();
    }
    sliderTracking_ = false;
    progressPressed_ = false;
    pressedButtonId_ = 0;
    hoveredButtonId_ = 0;
    progressHot_ = false;
    openComboControlId_ = 0;
    openComboSelectionIndex_ = -1;
    pendingHoverId_ = 0;
    hoverSinceTick_ = 0;
    windowDragging_ = false;
    HideTrackedTooltip();
    ShowWindow(hwnd_, SW_HIDE);
}

void PreviewBarWindow::SetPlaying(bool playing)
{
    if (playing_ == playing) {
        return;
    }
    playing_ = playing;
    InvalidateButton(ID_TOOL_PREVIEW_PLAY_PAUSE);
}

void PreviewBarWindow::SetPreviewMetrics(LONGLONG duration100ns, LONGLONG position100ns)
{
    const double oldRatio = progressRatio_;
    const LONGLONG nextDuration = std::max<LONGLONG>(0, duration100ns);
    const LONGLONG nextPosition = sliderTracking_
        ? SliderSeekPosition100ns()
        : std::clamp<LONGLONG>(position100ns, 0, nextDuration);
    const double nextRatio = nextDuration <= 0 ? 0.0
                                               : std::clamp(static_cast<double>(nextPosition) / static_cast<double>(nextDuration), 0.0, 1.0);
    if (duration100ns_ == nextDuration &&
        position100ns_ == nextPosition &&
        std::abs(progressRatio_ - nextRatio) < 0.000001) {
        return;
    }

    duration100ns_ = nextDuration;
    position100ns_ = nextPosition;
    progressRatio_ = nextRatio;
    UpdateTimeLabel();
    InvalidateProgress(oldRatio);
}

float PreviewBarWindow::PlaybackRate() const
{
    if (!cmbSpeed_) {
        return 1.0f;
    }
    const std::wstring text = UiUtil::GetComboSelectionOrWindowText(cmbSpeed_);
    return std::clamp(UiUtil::ParseFloatOrFallback(text, 1.0f), 0.2f, 2.0f);
}

VideoExportQuality PreviewBarWindow::ExportQuality() const
{
    const int selection = static_cast<int>(SendMessageW(cmbQuality_, CB_GETCURSEL, 0, 0));
    switch (selection) {
    case 0:
        return VideoExportQuality::Light;
    case 1:
        return VideoExportQuality::Standard;
    case 2:
    default:
        return VideoExportQuality::Original;
    }
}

LONGLONG PreviewBarWindow::SliderSeekPosition100ns() const
{
    if (duration100ns_ <= 0) {
        return 0;
    }
    return static_cast<LONGLONG>(std::llround(static_cast<double>(duration100ns_) * std::clamp(progressRatio_, 0.0, 1.0)));
}

void PreviewBarWindow::Relayout()
{
    if (!hwnd_) {
        return;
    }

    const float scale = static_cast<float>(dpi_) / 96.0f;
    const int margin = std::max(10, static_cast<int>(std::round(10.0f * scale)));
    const int rowGap = std::max(8, static_cast<int>(std::round(8.0f * scale)));
    const int controlH = std::max(26, static_cast<int>(std::round(28.0f * scale)));
    const int line1Y = margin;
    const int line2Y = line1Y + controlH + rowGap;
    const int timeW = std::max(90, static_cast<int>(std::round(112.0f * scale)));
    const int buttonW = controlH;
    const int speedW = std::max(82, static_cast<int>(std::round(92.0f * scale)));
    const int qualityW = std::max(104, static_cast<int>(std::round(120.0f * scale)));

    width_ = std::max(560, static_cast<int>(std::round(560.0f * scale)));
    height_ = margin * 2 + controlH * 2 + rowGap;

    timeRect_ = RECT{margin, line1Y, margin + timeW, line1Y + controlH};
    playPauseRect_ = RECT{width_ - margin - buttonW, line1Y, width_ - margin, line1Y + controlH};
    progressTrackRect_ = RECT{timeRect_.right + rowGap, line1Y, playPauseRect_.left - rowGap, line1Y + controlH};

    int x = margin;
    speedRect_ = RECT{x, line2Y, x + speedW, line2Y + controlH};
    x = speedRect_.right + rowGap;
    qualityRect_ = RECT{x, line2Y, x + qualityW, line2Y + controlH};
    x = qualityRect_.right + rowGap;
    retryRect_ = RECT{x, line2Y, x + buttonW, line2Y + controlH};
    x = retryRect_.right + rowGap;
    exportRect_ = RECT{x, line2Y, x + buttonW, line2Y + controlH};
    x = exportRect_.right + rowGap;
    closeRect_ = RECT{x, line2Y, x + buttonW, line2Y + controlH};

    MoveWindow(cmbSpeed_, speedRect_.left, speedRect_.top, RectWidth(speedRect_), 320, TRUE);
    MoveWindow(cmbQuality_, qualityRect_.left, qualityRect_.top, RectWidth(qualityRect_), 320, TRUE);
    ConfigureComboControl(cmbSpeed_, controlH, 8);
    ConfigureComboControl(cmbQuality_, controlH, 3);

    SetWindowPos(hwnd_, nullptr, 0, 0, width_, height_, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    const int radius = std::max(4, static_cast<int>(std::round(6.0f * scale)));
    HRGN region = CreateRoundRectRgn(0, 0, width_ + 1, height_ + 1, radius, radius);
    if (region && SetWindowRgn(hwnd_, region, TRUE) == 0) {
        DeleteObject(region);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PreviewBarWindow::UpdateTimeLabel()
{
    const std::wstring total = FormatDuration(duration100ns_);
    const std::wstring current = FormatDuration(position100ns_);
    timeText_ = current + L" / " + total;
    if (hwnd_ && IsRectValid(timeRect_)) {
        InvalidateRect(hwnd_, &timeRect_, FALSE);
    }
}

std::wstring PreviewBarWindow::TooltipForControlId(UINT id) const
{
    switch (id) {
    case ID_TOOL_PREVIEW_PLAY_PAUSE:
        return playing_ ? L"\u6682\u505C\u64AD\u653E" : L"\u5F00\u59CB\u64AD\u653E";
    case ID_TOOL_PREVIEW_RERECORD:
        return L"\u91CD\u65B0\u5F55\u5236";
    case ID_TOOL_PREVIEW_EXPORT:
        return L"\u5BFC\u51FA\u89C6\u9891";
    case ID_TOOL_PREVIEW_CLOSE:
        return L"\u5173\u95ED\u9884\u89C8";
    case ID_TOOL_PREVIEW_SPEED:
        return L"\u64AD\u653E\u500D\u901F";
    case ID_TOOL_PREVIEW_EXPORT_QUALITY:
        return L"\u5BFC\u51FA\u753B\u8D28";
    default:
        return L"";
    }
}

void PreviewBarWindow::CreateTooltips()
{
    DestroyTooltips();
    if (!hwnd_) {
        return;
    }
    tooltip_ = UiUtil::CreateTrackedTooltipWindow(hwnd_, font_);
    if (!tooltip_) {
        return;
    }
    trackedTooltipVisible_ = false;
    trackedTooltipText_.clear();
}

void PreviewBarWindow::DestroyTooltips()
{
    UiUtil::DestroyTrackedTooltipWindow(tooltip_, trackedTooltipVisible_, trackedTooltipText_);
}

void PreviewBarWindow::HideTrackedTooltip()
{
    UiUtil::HideTrackedTooltipWindow(tooltip_, trackedTooltipVisible_);
}

void PreviewBarWindow::ActivateTrackedTooltip(UINT id, POINT screenPt)
{
    if (!tooltip_) {
        return;
    }

    const std::wstring text = TooltipForControlId(id);
    if (text.empty()) {
        HideTrackedTooltip();
        return;
    }
    UiUtil::ShowTrackedTooltipWindow(hwnd_, tooltip_, font_, text, trackedTooltipText_, trackedTooltipVisible_, screenPt);
}

void PreviewBarWindow::UpdateTrackedTooltip(UINT id, POINT screenPt)
{
    if (id == 0 || TooltipForControlId(id).empty()) {
        pendingHoverId_ = 0;
        HideTrackedTooltip();
        return;
    }

    const DWORD now = GetTickCount();
    if (pendingHoverId_ != id) {
        pendingHoverId_ = id;
        hoverSinceTick_ = now;
        HideTrackedTooltip();
        return;
    }

    const DWORD elapsed = now - hoverSinceTick_;
    if (elapsed >= 350 || trackedTooltipVisible_) {
        ActivateTrackedTooltip(id, screenPt);
    }
}

void PreviewBarWindow::NotifyParent(UINT id, UINT code, LPARAM lParam) const
{
    if (parent_) {
        SendMessageW(parent_, WM_COMMAND, MAKEWPARAM(id, code), lParam != 0 ? lParam : reinterpret_cast<LPARAM>(hwnd_));
    }
}

void PreviewBarWindow::InvalidateProgress(double oldRatio)
{
    if (hwnd_ && IsRectValid(progressTrackRect_)) {
        RECT dirty{};
        if (oldRatio < 0.0) {
            dirty = progressTrackRect_;
        } else {
            const RECT oldFill = ProgressFillRectForRatio(oldRatio);
            const RECT newFill = ProgressFillRectForRatio(progressRatio_);
            if (IsRectValid(oldFill)) {
                dirty = oldFill;
            }
            if (IsRectValid(newFill)) {
                if (IsRectValid(dirty)) {
                    dirty.left = std::min(dirty.left, newFill.left);
                    dirty.top = std::min(dirty.top, newFill.top);
                    dirty.right = std::max(dirty.right, newFill.right);
                    dirty.bottom = std::max(dirty.bottom, newFill.bottom);
                } else {
                    dirty = newFill;
                }
            }
            if (!IsRectValid(dirty)) {
                dirty = progressTrackRect_;
            }
        }
        InflateRect(&dirty, 6, 6);
        InvalidateRect(hwnd_, &dirty, FALSE);
    }
}

RECT PreviewBarWindow::ProgressFillRectForRatio(double ratio) const
{
    if (!IsRectValid(progressTrackRect_)) {
        return RECT{};
    }
    const float scale = static_cast<float>(dpi_) / 96.0f;
    RECT fillRect = progressTrackRect_;
    const int innerPad = std::max(2, static_cast<int>(std::round(2.0f * scale)));
    InflateRect(&fillRect, -innerPad, -innerPad);
    const int fullWidth = std::max(0, RectWidth(fillRect));
    if (fullWidth <= 0) {
        return RECT{};
    }
    const int fillWidth = std::max(0, static_cast<int>(std::round(fullWidth * std::clamp(ratio, 0.0, 1.0))));
    fillRect.right = std::min<LONG>(fillRect.right, static_cast<LONG>(fillRect.left + fillWidth));
    return fillRect;
}

void PreviewBarWindow::InvalidateButton(UINT id)
{
    if (!hwnd_) {
        return;
    }
    RECT rc = ButtonRect(id);
    if (IsRectValid(rc)) {
        InflateRect(&rc, 2, 2);
        InvalidateRect(hwnd_, &rc, FALSE);
    }
}

void PreviewBarWindow::UpdateHoverState(POINT pt)
{
    const UINT hit = HitTest(pt);
    const bool newProgressHot = (hit == kPreviewProgressHitId);
    const UINT newHoveredButtonId = (hit != kPreviewProgressHitId) ? hit : 0;

    if (progressHot_ != newProgressHot) {
        progressHot_ = newProgressHot;
        InvalidateProgress();
    }
    if (hoveredButtonId_ != newHoveredButtonId) {
        const UINT old = hoveredButtonId_;
        hoveredButtonId_ = newHoveredButtonId;
        if (old != 0) {
            InvalidateButton(old);
        }
        if (hoveredButtonId_ != 0) {
            InvalidateButton(hoveredButtonId_);
        }
    }
}

void PreviewBarWindow::BeginProgressDrag(POINT pt)
{
    sliderTracking_ = true;
    progressPressed_ = true;
    SetCapture(hwnd_);
    UpdateProgressFromPoint(pt, false);
    NotifyParent(ID_TOOL_PREVIEW_PROGRESS, 0, reinterpret_cast<LPARAM>(hwnd_));
}

void PreviewBarWindow::UpdateProgressFromPoint(POINT pt, bool notifyParent)
{
    const double oldRatio = progressRatio_;
    RECT trackRect = progressTrackRect_;
    const int innerPad = std::max(2, static_cast<int>(std::round(2.0f * (static_cast<float>(dpi_) / 96.0f))));
    InflateRect(&trackRect, -innerPad, -innerPad);
    const int left = trackRect.left;
    const int width = std::max(1, RectWidth(trackRect));
    const double nextRatio = std::clamp(static_cast<double>(pt.x - left) / static_cast<double>(width), 0.0, 1.0);
    const bool changed = std::abs(nextRatio - oldRatio) >= 0.000001;
    progressRatio_ = nextRatio;
    if (changed) {
        position100ns_ = SliderSeekPosition100ns();
        UpdateTimeLabel();
        InvalidateProgress(oldRatio);
    }
    if (notifyParent && changed) {
        NotifyParent(ID_TOOL_PREVIEW_PROGRESS, 0, reinterpret_cast<LPARAM>(hwnd_));
    }
}

void PreviewBarWindow::EndProgressDrag(bool notifyParent)
{
    if (!progressPressed_) {
        return;
    }
    progressPressed_ = false;
    sliderTracking_ = false;
    if (GetCapture() == hwnd_) {
        ReleaseCapture();
    }
    if (notifyParent) {
        NotifyParent(ID_TOOL_PREVIEW_PROGRESS, 0, reinterpret_cast<LPARAM>(hwnd_));
    }
    InvalidateProgress();
}

UINT PreviewBarWindow::HitTest(POINT pt) const
{
    if (IsRectValid(progressTrackRect_) && PtInRect(&progressTrackRect_, pt)) {
        return kPreviewProgressHitId;
    }

    const std::array<UINT, 4> buttonIds = {
        ID_TOOL_PREVIEW_PLAY_PAUSE,
        ID_TOOL_PREVIEW_RERECORD,
        ID_TOOL_PREVIEW_EXPORT,
        ID_TOOL_PREVIEW_CLOSE,
    };
    for (UINT id : buttonIds) {
        const RECT rc = ButtonRect(id);
        if (IsRectValid(rc) && PtInRect(&rc, pt)) {
            return id;
        }
    }
    return 0;
}

RECT PreviewBarWindow::ButtonRect(UINT id) const
{
    switch (id) {
    case ID_TOOL_PREVIEW_PLAY_PAUSE:
        return playPauseRect_;
    case ID_TOOL_PREVIEW_RERECORD:
        return retryRect_;
    case ID_TOOL_PREVIEW_EXPORT:
        return exportRect_;
    case ID_TOOL_PREVIEW_CLOSE:
        return closeRect_;
    default:
        return RECT{};
    }
}

std::wstring PreviewBarWindow::ButtonText(UINT id) const
{
    switch (id) {
    case ID_TOOL_PREVIEW_PLAY_PAUSE:
        return playing_ ? L"\u23F8" : L"\u25B6";
    case ID_TOOL_PREVIEW_RERECORD:
        return L"\u21BA";
    case ID_TOOL_PREVIEW_EXPORT:
        return L"\u2193";
    case ID_TOOL_PREVIEW_CLOSE:
        return L"\u2715";
    default:
        return L"";
    }
}

void PreviewBarWindow::DrawButton(HDC hdc, UINT id, const RECT& rc, bool hot, bool pressed) const
{
    if (!hdc || !IsRectValid(rc)) {
        return;
    }

    const bool checked = (id == ID_TOOL_PREVIEW_PLAY_PAUSE && playing_);
    COLORREF bg = ThemeColors::Component::Toolbar::ComboBoxBgColor;
    if (checked) {
        bg = hot ? RGB(45, 100, 160) : RGB(35, 81, 126);
    } else if (pressed) {
        bg = RGB(70, 80, 98);
    } else if (hot) {
        bg = RGB(62, 72, 90);
    }
    const COLORREF border = checked ? RGB(95, 170, 240) : (hot ? RGB(110, 130, 165) : RGB(66, 72, 82));
    const COLORREF fg = ThemeColors::Component::Toolbar::ComboBoxTextColor;

    {
        HBRUSH panelBrush = CreateSolidBrush(RGB(20, 24, 31));
        FillRect(hdc, &rc, panelBrush);
        DeleteObject(panelBrush);
    }

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    const float scale = static_cast<float>(dpi_) / 96.0f;
    const Gdiplus::RectF rect(
        static_cast<float>(rc.left) + 0.5f,
        static_cast<float>(rc.top) + 0.5f,
        std::max(2.0f, static_cast<float>(RectWidth(rc)) - 1.0f),
        std::max(2.0f, static_cast<float>(RectHeight(rc)) - 1.0f));
    Gdiplus::GraphicsPath path;
    MakeRoundRectPath(rect, std::max(4.0f, 5.0f * scale), path);
    Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, GetRValue(bg), GetGValue(bg), GetBValue(bg)));
    Gdiplus::Pen borderPen(Gdiplus::Color(255, GetRValue(border), GetGValue(border), GetBValue(border)), 1.0f);
    borderPen.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.FillPath(&bgBrush, &path);
    graphics.DrawPath(&borderPen, &path);

    UINT mappedIconId = 0;
    bool mappedRecordingPaused = false;
    switch (id) {
    case ID_TOOL_PREVIEW_PLAY_PAUSE:
        mappedIconId = ID_TOOL_RECORD_PAUSE;
        mappedRecordingPaused = !playing_;
        break;
    case ID_TOOL_PREVIEW_RERECORD:
        mappedIconId = ID_TOOL_REDO;
        break;
    case ID_TOOL_PREVIEW_EXPORT:
        mappedIconId = ID_TOOL_SAVE;
        break;
    case ID_TOOL_PREVIEW_CLOSE:
        mappedIconId = ID_TOOL_CANCEL;
        break;
    default:
        break;
    }
    const bool drewIcon = mappedIconId != 0
        ? ToolbarWindow::DrawToolbarIcon(mappedIconId, hdc, rc, fg, scale, false, mappedRecordingPaused)
        : false;
    if (!drewIcon) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, fg);
        HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, iconFont_ ? iconFont_ : font_));
        RECT textRc = rc;
        DrawTextW(hdc, ButtonText(id).c_str(), -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
    }
}

void PreviewBarWindow::DrawProgress(HDC hdc) const
{
    if (!hdc || !IsRectValid(progressTrackRect_)) {
        return;
    }

    const float scale = static_cast<float>(dpi_) / 96.0f;
    const RECT track = progressTrackRect_;
    RECT fillRect = ProgressFillRectForRatio(progressRatio_);

    const COLORREF trackBg = ThemeColors::Component::Toolbar::ComboBoxBgColor;
    const COLORREF border = progressPressed_
        ? RGB(95, 170, 240)
        : (progressHot_ ? RGB(110, 130, 165) : RGB(66, 72, 82));
    const COLORREF fillColor = progressPressed_
        ? RGB(60, 136, 246)
        : (progressHot_ ? RGB(95, 165, 255) : RGB(84, 150, 232));

    Gdiplus::Graphics graphics(hdc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    const Gdiplus::RectF outer(
        static_cast<float>(track.left) + 0.5f,
        static_cast<float>(track.top) + 0.5f,
        std::max(2.0f, static_cast<float>(RectWidth(track)) - 1.0f),
        std::max(2.0f, static_cast<float>(RectHeight(track)) - 1.0f));
    Gdiplus::GraphicsPath outerPath;
    MakeRoundRectPath(outer, std::max(3.0f, 4.0f * scale), outerPath);
    Gdiplus::SolidBrush outerBrush(Gdiplus::Color(
        static_cast<BYTE>(255),
        static_cast<BYTE>(GetRValue(trackBg)),
        static_cast<BYTE>(GetGValue(trackBg)),
        static_cast<BYTE>(GetBValue(trackBg))));
    Gdiplus::Pen borderPen(Gdiplus::Color(
        static_cast<BYTE>(255),
        static_cast<BYTE>(GetRValue(border)),
        static_cast<BYTE>(GetGValue(border)),
        static_cast<BYTE>(GetBValue(border))), 1.0f);
    borderPen.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.FillPath(&outerBrush, &outerPath);
    graphics.DrawPath(&borderPen, &outerPath);

    if (RectWidth(fillRect) > 0 && RectHeight(fillRect) > 0) {
        const Gdiplus::RectF inner(
            static_cast<float>(fillRect.left) + 0.5f,
            static_cast<float>(fillRect.top) + 0.5f,
            std::max(2.0f, static_cast<float>(RectWidth(fillRect)) - 1.0f),
            std::max(2.0f, static_cast<float>(RectHeight(fillRect)) - 1.0f));
        Gdiplus::GraphicsPath innerPath;
        MakeRoundRectPath(inner, std::max(2.5f, 3.0f * scale), innerPath);
        Gdiplus::SolidBrush fillBrush(Gdiplus::Color(
            static_cast<BYTE>(255),
            static_cast<BYTE>(GetRValue(fillColor)),
            static_cast<BYTE>(GetGValue(fillColor)),
            static_cast<BYTE>(GetBValue(fillColor))));
        graphics.FillPath(&fillBrush, &innerPath);
    }
}

void PreviewBarWindow::DrawComboItem(const DRAWITEMSTRUCT* dis) const
{
    if (!dis || !dis->hDC) {
        return;
    }

    auto comboHandle = [&](UINT id) -> HWND {
        if (id == ID_TOOL_PREVIEW_SPEED) {
            return cmbSpeed_;
        }
        if (id == ID_TOOL_PREVIEW_EXPORT_QUALITY) {
            return cmbQuality_;
        }
        return nullptr;
    };

    HWND combo = comboHandle(dis->CtlID);
    if (!combo) {
        return;
    }

    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool selected = (dis->itemState & ODS_SELECTED) != 0;
    // 只有当 itemID 为 -1 时，才是显示区域
    const bool comboFace = (dis->itemID == static_cast<UINT>(-1));

    COLORREF bg = comboFace
        ? ThemeColors::Component::Toolbar::ComboBoxBgColor
        : (selected ? RGB(62, 72, 90) : ThemeColors::Component::Toolbar::ComboBoxBgColor);
    const COLORREF border = RGB(66, 72, 82);
    const COLORREF fg = disabled ? RGB(110, 116, 128) : ThemeColors::Component::Toolbar::ComboBoxTextColor;

    HBRUSH fillBrush = CreateSolidBrush(bg);
    FillRect(dis->hDC, &dis->rcItem, fillBrush);
    DeleteObject(fillBrush);

    if (!comboFace) {
        HPEN listPen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ oldPen = SelectObject(dis->hDC, listPen);
        HGDIOBJ oldBrush = SelectObject(dis->hDC, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
        SelectObject(dis->hDC, oldBrush);
        SelectObject(dis->hDC, oldPen);
        DeleteObject(listPen);
    }

    const std::wstring itemText = UiUtil::GetOwnerDrawComboText(
        combo, comboFace, dis->CtlID, dis->itemID, openComboControlId_, openComboSelectionIndex_);

    RECT textRc = dis->rcItem;
    textRc.left += std::max(8, static_cast<int>(std::round(10.0f * (static_cast<float>(dpi_) / 96.0f))));
    textRc.right -= std::max(8, static_cast<int>(std::round(8.0f * (static_cast<float>(dpi_) / 96.0f))));

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, fg);
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dis->hDC, font_));
    DrawTextW(dis->hDC, itemText.c_str(), -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dis->hDC, oldFont);
}

LRESULT CALLBACK PreviewBarWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* self = WINDOW_PROC_GET_THIS(PreviewBarWindow);
    WindowProc::HandleWindowCreate(hwnd, msg, lParam, self);

    switch (msg) {
    case WM_NCHITTEST:
        // 明确处理非客户区点击，避免返回 HTTRANSPARENT 导致穿透
        if (self) {
            const long x = GET_X_LPARAM(lParam);
            const long y = GET_Y_LPARAM(lParam);
            POINT ptScreen{static_cast<LONG>(x), static_cast<LONG>(y)};
            POINT ptClient = ptScreen;
            ScreenToClient(hwnd, &ptClient);
            RECT clientRc{};
            GetClientRect(hwnd, &clientRc);
            if (PtInRect(&clientRc, ptClient)) {
                return HTCLIENT;
            }
        }
        break;
    case WM_COMMAND:
        if (self) {
            auto invalidateComboFace = [&](UINT controlId) {
                HWND combo = nullptr;
                if (controlId == ID_TOOL_PREVIEW_SPEED) {
                    combo = self->cmbSpeed_;
                } else if (controlId == ID_TOOL_PREVIEW_EXPORT_QUALITY) {
                    combo = self->cmbQuality_;
                }
                if (combo) {
                    InvalidateRect(combo, nullptr, FALSE);
                }
            };

            if (HIWORD(wParam) == CBN_DROPDOWN) {
                self->openComboControlId_ = LOWORD(wParam);
                if (HWND combo = reinterpret_cast<HWND>(lParam)) {
                    self->openComboSelectionIndex_ = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
                } else {
                    self->openComboSelectionIndex_ = -1;
                }
                invalidateComboFace(self->openComboControlId_);
                self->HideTrackedTooltip();
            } else if (HIWORD(wParam) == CBN_CLOSEUP) {
                const UINT controlId = LOWORD(wParam);
                self->openComboControlId_ = 0;
                self->openComboSelectionIndex_ = -1;
                invalidateComboFace(controlId);
                self->pendingHoverId_ = 0;
            } else if (HIWORD(wParam) == CBN_SELCHANGE) {
                // 当下拉列表中的选择改变时（例如滚动时），更新显示索引
                const UINT controlId = LOWORD(wParam);
                if (self->openComboControlId_ == controlId) {
                    if (HWND combo = reinterpret_cast<HWND>(lParam)) {
                        self->openComboSelectionIndex_ = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
                    } else if (controlId == ID_TOOL_PREVIEW_SPEED) {
                        self->openComboSelectionIndex_ = static_cast<int>(SendMessageW(self->cmbSpeed_, CB_GETCURSEL, 0, 0));
                    } else if (controlId == ID_TOOL_PREVIEW_EXPORT_QUALITY) {
                        self->openComboSelectionIndex_ = static_cast<int>(SendMessageW(self->cmbQuality_, CB_GETCURSEL, 0, 0));
                    }
                    invalidateComboFace(controlId);
                }
            }

            if (self->parent_) {
                SendMessageW(self->parent_, WM_COMMAND, wParam, lParam);
                return 0;
            }
        }
        break;
    case WM_MEASUREITEM:
        if (self) {
            auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (measure && (measure->CtlID == ID_TOOL_PREVIEW_SPEED || measure->CtlID == ID_TOOL_PREVIEW_EXPORT_QUALITY)) {
                measure->itemHeight = static_cast<UINT>(std::max(18, static_cast<int>(std::round(28.0f * (static_cast<float>(self->dpi_) / 96.0f))) - 4));
                return TRUE;
            }
        }
        break;
    case WM_DRAWITEM:
        if (self) {
            const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (draw && (draw->CtlID == ID_TOOL_PREVIEW_SPEED || draw->CtlID == ID_TOOL_PREVIEW_EXPORT_QUALITY)) {
                self->DrawComboItem(draw);
                return TRUE;
            }
        }
        break;
    case WM_CTLCOLORLISTBOX:
        if (self) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            if (!self->comboBgBrush_) {
                self->comboBgBrush_ = CreateSolidBrush(ThemeColors::Component::Toolbar::ComboBoxBgColor);
            }
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, ThemeColors::Component::Toolbar::ComboBoxBgColor);
            SetTextColor(hdc, ThemeColors::Component::Toolbar::ComboBoxTextColor);
            return reinterpret_cast<LRESULT>(self->comboBgBrush_);
        }
        break;
    case WM_ERASEBKGND:
        return TRUE;
    case WM_MOUSEMOVE:
        if (self) {
            auto isComboPoint = [&](POINT localPt) -> bool {
                return (IsRectValid(self->speedRect_) && PtInRect(&self->speedRect_, localPt)) ||
                       (IsRectValid(self->qualityRect_) && PtInRect(&self->qualityRect_, localPt));
            };
            auto tooltipControlIdAtPoint = [&](POINT localPt) -> UINT {
                const UINT hit = self->HitTest(localPt);
                if (hit == ID_TOOL_PREVIEW_PLAY_PAUSE ||
                    hit == ID_TOOL_PREVIEW_RERECORD ||
                    hit == ID_TOOL_PREVIEW_EXPORT ||
                    hit == ID_TOOL_PREVIEW_CLOSE) {
                    return hit;
                }
                if (IsRectValid(self->speedRect_) && PtInRect(&self->speedRect_, localPt)) {
                    return ID_TOOL_PREVIEW_SPEED;
                }
                if (IsRectValid(self->qualityRect_) && PtInRect(&self->qualityRect_, localPt)) {
                    return ID_TOOL_PREVIEW_EXPORT_QUALITY;
                }
                return 0;
            };
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE | TME_HOVER;
            tme.dwHoverTime = 350;
            tme.hwndTrack = hwnd;
            if (TrackMouseEvent(&tme)) {
                self->mouseTracking_ = true;
            }
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (self->windowDragging_) {
                // 计算窗口移动距离
                const int dx = pt.x - self->dragStartPoint_.x;
                const int dy = pt.y - self->dragStartPoint_.y;
                if (dx != 0 || dy != 0) {
                    // 获取当前窗口位置
                    RECT rc{};
                    GetWindowRect(hwnd, &rc);
                    // 移动窗口
                    SetWindowPos(hwnd, nullptr, rc.left + dx, rc.top + dy, 0, 0,
                                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                }
                // 强制设置移动光标
                SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                self->HideTrackedTooltip();
                return 0;
            }
            self->UpdateHoverState(pt);
            if (self->progressPressed_) {
                self->UpdateProgressFromPoint(pt, true);
            }
            POINT sp{};
            GetCursorPos(&sp);
            self->UpdateTrackedTooltip(tooltipControlIdAtPoint(pt), sp);
            // 强制设置光标，防止 WM_SETCURSOR 未到达时的穿透视觉问题
            if (self->HitTest(pt) != 0 || isComboPoint(pt)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
            } else {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            }
            return 0;
        }
        break;
    case WM_MOUSEHOVER:
        if (self) {
            auto tooltipControlIdAtPoint = [&](POINT localPt) -> UINT {
                const UINT hit = self->HitTest(localPt);
                if (hit == ID_TOOL_PREVIEW_PLAY_PAUSE ||
                    hit == ID_TOOL_PREVIEW_RERECORD ||
                    hit == ID_TOOL_PREVIEW_EXPORT ||
                    hit == ID_TOOL_PREVIEW_CLOSE) {
                    return hit;
                }
                if (IsRectValid(self->speedRect_) && PtInRect(&self->speedRect_, localPt)) {
                    return ID_TOOL_PREVIEW_SPEED;
                }
                if (IsRectValid(self->qualityRect_) && PtInRect(&self->qualityRect_, localPt)) {
                    return ID_TOOL_PREVIEW_EXPORT_QUALITY;
                }
                return 0;
            };
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            POINT sp{};
            GetCursorPos(&sp);
            const UINT hoverId = tooltipControlIdAtPoint(pt);
            self->pendingHoverId_ = hoverId;
            if (hoverId != 0) {
                self->hoverSinceTick_ = GetTickCount();
                self->ActivateTrackedTooltip(hoverId, sp);
            } else {
                self->HideTrackedTooltip();
            }
            return 0;
        }
        break;
    case WM_MOUSELEAVE:
        if (self) {
            self->mouseTracking_ = false;
            self->UpdateHoverState(POINT{-32768, -32768});
            self->pendingHoverId_ = 0;
            self->HideTrackedTooltip();
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
        if (self) {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const UINT hit = self->HitTest(pt);
            if (hit == kPreviewProgressHitId) {
                self->BeginProgressDrag(pt);
                return 0;
            }
            if (hit != 0) {
                self->pressedButtonId_ = hit;
                self->hoveredButtonId_ = hit;
                SetCapture(hwnd);
                self->InvalidateButton(hit);
                return 0;
            }
            // 如果点击在空白区域，开始窗口拖动
            self->windowDragging_ = true;
            self->dragStartPoint_ = pt;
            SetCapture(hwnd);
        }
        return 0;  // 防止穿透
        break;
    case WM_LBUTTONUP:
        if (self) {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (self->progressPressed_) {
                self->UpdateProgressFromPoint(pt, false);
                self->EndProgressDrag(true);
                return 0;
            }
            if (self->pressedButtonId_ != 0) {
                const UINT id = self->pressedButtonId_;
                self->pressedButtonId_ = 0;
                if (GetCapture() == hwnd) {
                    ReleaseCapture();
                }
                self->InvalidateButton(id);
                if (self->HitTest(pt) == id) {
                    self->NotifyParent(id, BN_CLICKED, reinterpret_cast<LPARAM>(hwnd));
                }
                return 0;
            }
            if (self->windowDragging_) {
                self->windowDragging_ = false;
                if (GetCapture() == hwnd) {
                    ReleaseCapture();
                }
                return 0;
            }
        }
        return 0;  // 防止穿透
        break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        // 阻止右键和中键穿透
        if (self) {
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        // 阻止滚轮穿透
        if (self) {
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (self) {
            const bool hadProgressCapture = self->progressPressed_;
            const UINT oldPressed = self->pressedButtonId_;
            self->progressPressed_ = false;
            self->sliderTracking_ = false;
            self->pressedButtonId_ = 0;
            self->windowDragging_ = false;
            self->pendingHoverId_ = 0;
            self->HideTrackedTooltip();
            if (hadProgressCapture) {
                self->InvalidateProgress();
            }
            if (oldPressed != 0) {
                self->InvalidateButton(oldPressed);
            }
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (self && LOWORD(lParam) == HTCLIENT) {
            auto isComboPoint = [&](POINT localPt) -> bool {
                return (IsRectValid(self->speedRect_) && PtInRect(&self->speedRect_, localPt)) ||
                    (IsRectValid(self->qualityRect_) && PtInRect(&self->qualityRect_, localPt));
            };
            auto comboControlIdFromWindow = [&](HWND target) -> UINT {
                auto matchCombo = [&](HWND combo) -> bool {
                    if (!combo || !target || !IsWindow(combo)) {
                        return false;
                    }
                    if (target == combo || IsChild(combo, target)) {
                        return true;
                    }
                    COMBOBOXINFO info{};
                    info.cbSize = sizeof(info);
                    if (!GetComboBoxInfo(combo, &info) || !info.hwndList || !IsWindow(info.hwndList)) {
                        return false;
                    }
                    return target == info.hwndList || IsChild(info.hwndList, target);
                };
                if (matchCombo(self->cmbSpeed_)) {
                    return ID_TOOL_PREVIEW_SPEED;
                }
                if (matchCombo(self->cmbQuality_)) {
                    return ID_TOOL_PREVIEW_EXPORT_QUALITY;
                }
                return 0;
            };

            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            const HWND cursorWnd = reinterpret_cast<HWND>(wParam);
            const UINT comboControlId = comboControlIdFromWindow(cursorWnd);
            const UINT hit = self->HitTest(pt);
            const bool interactive = hit != 0 || isComboPoint(pt) || comboControlId != 0;

            if (!self->windowDragging_ && !self->progressPressed_ && self->pressedButtonId_ == 0) {
                UINT tooltipId = 0;
                if (hit == ID_TOOL_PREVIEW_PLAY_PAUSE ||
                    hit == ID_TOOL_PREVIEW_RERECORD ||
                    hit == ID_TOOL_PREVIEW_EXPORT ||
                    hit == ID_TOOL_PREVIEW_CLOSE) {
                    tooltipId = hit;
                } else if (IsRectValid(self->speedRect_) && PtInRect(&self->speedRect_, pt)) {
                    tooltipId = ID_TOOL_PREVIEW_SPEED;
                } else if (IsRectValid(self->qualityRect_) && PtInRect(&self->qualityRect_, pt)) {
                    tooltipId = ID_TOOL_PREVIEW_EXPORT_QUALITY;
                } else if (comboControlId != 0) {
                    tooltipId = comboControlId;
                }

                POINT screenPt{};
                GetCursorPos(&screenPt);
                self->UpdateTrackedTooltip(tooltipId, screenPt);
            } else {
                self->pendingHoverId_ = 0;
                self->HideTrackedTooltip();
            }

            if (self->windowDragging_) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            } else if (interactive) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
            } else {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            }
            return TRUE;
        }
        break;
    case WM_PAINT:
        if (self) {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            HBRUSH bgBrush = CreateSolidBrush(RGB(20, 24, 31));
            FillRect(hdc, &rc, bgBrush);
            DeleteObject(bgBrush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, ThemeColors::Component::Toolbar::ComboBoxTextColor);
            HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, self->font_));
            RECT timeRc = self->timeRect_;
            DrawTextW(hdc, self->timeText_.c_str(), -1, &timeRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(hdc, oldFont);

            self->DrawProgress(hdc);
            self->DrawButton(hdc, ID_TOOL_PREVIEW_PLAY_PAUSE, self->playPauseRect_,
                self->hoveredButtonId_ == ID_TOOL_PREVIEW_PLAY_PAUSE,
                self->pressedButtonId_ == ID_TOOL_PREVIEW_PLAY_PAUSE);
            self->DrawButton(hdc, ID_TOOL_PREVIEW_RERECORD, self->retryRect_,
                self->hoveredButtonId_ == ID_TOOL_PREVIEW_RERECORD,
                self->pressedButtonId_ == ID_TOOL_PREVIEW_RERECORD);
            self->DrawButton(hdc, ID_TOOL_PREVIEW_EXPORT, self->exportRect_,
                self->hoveredButtonId_ == ID_TOOL_PREVIEW_EXPORT,
                self->pressedButtonId_ == ID_TOOL_PREVIEW_EXPORT);
            self->DrawButton(hdc, ID_TOOL_PREVIEW_CLOSE, self->closeRect_,
                self->hoveredButtonId_ == ID_TOOL_PREVIEW_CLOSE,
                self->pressedButtonId_ == ID_TOOL_PREVIEW_CLOSE);

            EndPaint(hwnd, &ps);
            return 0;
        }
        break;
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTMESSAGE;
    case WM_ACTIVATE:
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
