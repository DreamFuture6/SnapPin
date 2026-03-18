#include "ui/OverlayWindow.h"
#include "app/AppIds.h"
#include "core/AnnotationRenderer.h"
#include "core/Logger.h"
#include "core/ScreenCaptureUtil.h"
#include "common/KnownFolderUtil.h"
#include "common/WindowMessagePayload.h"
#include "ui/GdiObject.h"
#include "ui/WindowUtil.h"
#include <thread>

namespace {
constexpr int kHandleSize = 8;
constexpr int kMinSelection = 4;
constexpr int kSelectionBorderGrab = 10;
constexpr size_t kMaxUndo = 200;
constexpr UINT ID_OVERLAY_TEXT_EDIT = 62001;
constexpr UINT WMU_OVERLAY_TEXT_COMMIT = WM_APP + 401;
constexpr UINT WMU_OVERLAY_TEXT_CANCEL = WM_APP + 402;
constexpr UINT WMU_PREVIEW_EXPORT_DONE = WM_APP + 403;
constexpr UINT_PTR IDT_LONG_CAPTURE = 7301;
constexpr UINT kLongCaptureTickMs = 12;
constexpr DWORD kLongCaptureThumbRefreshMs = 33;
constexpr UINT_PTR IDT_RECORDING_START = 7302;
constexpr UINT_PTR IDT_PREVIEW_PROGRESS = 7303;
constexpr COLORREF kLongCaptureColorKey = RGB(1, 0, 1);
constexpr wchar_t kOverlayWindowClassName[] = L"SnapPinOverlayWindowClass";
constexpr wchar_t kOverlayHudWindowClassName[] = L"SnapPinOverlayHudWindowClass";
constexpr BYTE kCrosshairHudAlpha = 120;
constexpr int kTextEditMinWidth = 80;
constexpr int kTextEditMinHeight = 20;
constexpr int kTextEditPaddingX = AnnotationRenderer::kTextPaddingX;
constexpr int kTextEditPaddingY = AnnotationRenderer::kTextPaddingY;
constexpr LONG kTextMeasureExtent = 32767;

struct TextLayoutMetrics {
    int contentWidth = 0;
    int contentHeight = 0;
    int lineHeight = 0;
};


struct VerticalSsdMatch {
    int shift = 0;
    int overlap = 0;
    double error = 1.0;
    double secondError = 1.0;
    bool matched = false;
};

struct PreviewExportDonePayload {
    bool success = false;
    std::wstring outputPath;
    std::wstring errorMessage;
};

void BuildGrayLuma(const Image& src, std::vector<uint8_t>& outGray) {
    outGray.clear();
    if (!src.IsValid()) {
        return;
    }
    outGray.resize(static_cast<size_t>(src.width) * static_cast<size_t>(src.height));
    const uint8_t* p = src.bgra.data();
    uint8_t* dst = outGray.data();
    const size_t pixelCount = static_cast<size_t>(src.width) * static_cast<size_t>(src.height);
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t b = p[i * 4 + 0];
        const uint8_t g = p[i * 4 + 1];
        const uint8_t r = p[i * 4 + 2];
        dst[i] = static_cast<uint8_t>((77 * r + 150 * g + 29 * b) >> 8);
    }
}

VerticalSsdMatch FindBestVerticalShiftSsdParallel(const Image& prev, const Image& cur, int scrollDirHint) {
    VerticalSsdMatch result{};
    if (!prev.IsValid() || !cur.IsValid() || prev.width != cur.width || prev.height != cur.height) {
        return result;
    }

    const int w = prev.width;
    const int h = prev.height;
    if (w <= 8 || h <= 8) {
        return result;
    }

    std::vector<uint8_t> grayPrev;
    std::vector<uint8_t> grayCur;
    BuildGrayLuma(prev, grayPrev);
    BuildGrayLuma(cur, grayCur);
    if (grayPrev.empty() || grayCur.empty()) {
        return result;
    }

    const int minOverlap = std::max(42, h / 5);
    const int maxShift = std::clamp(h - std::max(24, h / 6), 10, 1800);
    if (maxShift <= 0) {
        return result;
    }

    std::vector<int> candidates;
    candidates.reserve(static_cast<size_t>(maxShift) * 2 + 1);
    candidates.push_back(0);
    if (scrollDirHint > 0) {
        for (int dy = 1; dy <= maxShift; ++dy) {
            candidates.push_back(dy);
        }
    } else if (scrollDirHint < 0) {
        for (int dy = -1; dy >= -maxShift; --dy) {
            candidates.push_back(dy);
        }
    } else {
        for (int dy = 1; dy <= maxShift; ++dy) {
            candidates.push_back(dy);
        }
        for (int dy = -1; dy >= -maxShift; --dy) {
            candidates.push_back(dy);
        }
    }
    if (candidates.empty()) {
        return result;
    }

    const int stepX = std::max(1, w / 180);
    const int stepY = std::max(1, h / 180);
    const double kNormDiv = 65025.0;

    std::vector<double> errors(candidates.size(), std::numeric_limits<double>::max());
    std::vector<int> overlaps(candidates.size(), 0);

    auto evalRange = [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) {
            const int dy = candidates[i];
            const int overlap = h - std::abs(dy);
            if (overlap < minOverlap) {
                continue;
            }

            const int prevY0 = std::max(0, dy);
            const int curY0 = std::max(0, -dy);
            uint64_t ssd = 0;
            uint64_t samples = 0;
            for (int y = 0; y < overlap; y += stepY) {
                const int yp = prevY0 + y;
                const int yc = curY0 + y;
                const size_t prevRow = static_cast<size_t>(yp) * static_cast<size_t>(w);
                const size_t curRow = static_cast<size_t>(yc) * static_cast<size_t>(w);
                for (int x = 0; x < w; x += stepX) {
                    const int d = static_cast<int>(grayPrev[prevRow + static_cast<size_t>(x)]) -
                        static_cast<int>(grayCur[curRow + static_cast<size_t>(x)]);
                    ssd += static_cast<uint64_t>(d * d);
                    ++samples;
                }
            }
            if (samples == 0) {
                continue;
            }
            errors[i] = static_cast<double>(ssd) / (static_cast<double>(samples) * kNormDiv);
            overlaps[i] = overlap;
        }
    };

    int workerCount = static_cast<int>(std::thread::hardware_concurrency());
    workerCount = std::clamp(workerCount, 1, 6);
    if (candidates.size() < static_cast<size_t>(workerCount * 20)) {
        workerCount = 1;
    }

    if (workerCount <= 1) {
        evalRange(0, candidates.size());
    } else {
        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(workerCount - 1));
        const size_t chunk = (candidates.size() + static_cast<size_t>(workerCount) - 1) / static_cast<size_t>(workerCount);
        for (int i = 1; i < workerCount; ++i) {
            const size_t begin = static_cast<size_t>(i) * chunk;
            if (begin >= candidates.size()) {
                break;
            }
            const size_t end = std::min(candidates.size(), begin + chunk);
            workers.emplace_back(evalRange, begin, end);
        }
        evalRange(0, std::min(candidates.size(), chunk));
        for (auto& th : workers) {
            if (th.joinable()) {
                th.join();
            }
        }
    }

    size_t bestIdx = static_cast<size_t>(-1);
    size_t secondIdx = static_cast<size_t>(-1);
    for (size_t i = 0; i < errors.size(); ++i) {
        if (errors[i] == std::numeric_limits<double>::max()) {
            continue;
        }
        if (bestIdx == static_cast<size_t>(-1) || errors[i] < errors[bestIdx]) {
            secondIdx = bestIdx;
            bestIdx = i;
        } else if (secondIdx == static_cast<size_t>(-1) || errors[i] < errors[secondIdx]) {
            secondIdx = i;
        }
    }
    if (bestIdx == static_cast<size_t>(-1)) {
        return result;
    }

    result.shift = candidates[bestIdx];
    result.overlap = overlaps[bestIdx];
    result.error = errors[bestIdx];
    result.secondError = (secondIdx == static_cast<size_t>(-1)) ? 1.0 : errors[secondIdx];

    const bool overlapOk = result.overlap >= minOverlap;
    const double absoluteThreshold = (w * h <= 240 * 240) ? 0.020 : 0.014;
    const bool scoreOk = result.error <= absoluteThreshold;
    const bool uniquenessOk = (secondIdx == static_cast<size_t>(-1)) || (result.secondError >= result.error * 1.03);
    const bool stationaryOk = (result.shift == 0 && result.error <= absoluteThreshold * 0.6);
    result.matched = overlapOk && (stationaryOk || (scoreOk && uniquenessOk));
    return result;
}

RECT InflateRectCopy(RECT rc, int d) {
    rc.left -= d;
    rc.top -= d;
    rc.right += d;
    rc.bottom += d;
    return rc;
}

bool PtInRectInclusive(const RECT& rc, POINT p) {
    return p.x >= rc.left && p.x <= rc.right && p.y >= rc.top && p.y <= rc.bottom;
}

std::wstring_view TrimTrailingCarriageReturn(std::wstring_view text) {
    while (!text.empty() && text.back() == L'\r') {
        text.remove_suffix(1);
    }
    return text;
}

HFONT CreateOverlayTextFontHandle(float textSize, INT textStyle) {
    const int px = -static_cast<int>(std::round(std::max(8.0f, textSize)));
    const int weight = (textStyle & Gdiplus::FontStyleBold) ? FW_BOLD : FW_NORMAL;
    const BOOL italic = (textStyle & Gdiplus::FontStyleItalic) ? TRUE : FALSE;
    return CreateFontW(px, 0, 0, 0, weight, italic, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

TextLayoutMetrics MeasureTextLayout(HDC hdc, HFONT font, std::wstring_view text) {
    TextLayoutMetrics metrics{};
    if (!hdc) {
        return metrics;
    }

    UiGdi::ScopedSelectObject fontSelection(hdc, font);
    TEXTMETRICW tm{};
    if (!GetTextMetricsW(hdc, &tm)) {
        return metrics;
    }
    metrics.lineHeight = std::max(1, static_cast<int>(tm.tmHeight + tm.tmExternalLeading));

    auto measureLineWidth = [&](std::wstring_view line) {
        line = TrimTrailingCarriageReturn(line);
        if (line.empty()) {
            return 0;
        }
        std::wstring lineText(line);
        RECT calc{ 0, 0, kTextMeasureExtent, metrics.lineHeight };
        DrawTextW(hdc, lineText.c_str(), static_cast<int>(lineText.size()), &calc,
            DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX | DT_EDITCONTROL | DT_EXPANDTABS);
        return std::max(0, RectWidth(calc));
    };

    int lineCount = 1;
    if (!text.empty()) {
        lineCount = 0;
        size_t start = 0;
        while (start <= text.size()) {
            const size_t end = text.find(L'\n', start);
            if (end == std::wstring_view::npos) {
                metrics.contentWidth = std::max(metrics.contentWidth, measureLineWidth(text.substr(start)));
                ++lineCount;
                break;
            }
            metrics.contentWidth = std::max(metrics.contentWidth, measureLineWidth(text.substr(start, end - start)));
            ++lineCount;
            start = end + 1;
            if (start == text.size()) {
                ++lineCount;
                break;
            }
        }
    }

    metrics.contentHeight = std::max(metrics.lineHeight, lineCount * metrics.lineHeight);
    return metrics;
}

RECT FitTextRectToSelection(const RECT& selectionRect, POINT anchor, int desiredWidth, int desiredHeight) {
    RECT rc{
        anchor.x,
        anchor.y,
        anchor.x + std::max(kTextEditMinWidth, desiredWidth),
        anchor.y + std::max(kTextEditMinHeight, desiredHeight)
    };

    if (rc.right > selectionRect.right) {
        OffsetRect(&rc, selectionRect.right - rc.right, 0);
    }
    if (rc.bottom > selectionRect.bottom) {
        OffsetRect(&rc, 0, selectionRect.bottom - rc.bottom);
    }
    if (rc.left < selectionRect.left) {
        OffsetRect(&rc, selectionRect.left - rc.left, 0);
    }
    if (rc.top < selectionRect.top) {
        OffsetRect(&rc, 0, selectionRect.top - rc.top);
    }

    rc.left = std::max(rc.left, selectionRect.left);
    rc.top = std::max(rc.top, selectionRect.top);
    rc.right = std::min(rc.right, selectionRect.right);
    rc.bottom = std::min(rc.bottom, selectionRect.bottom);
    return rc;
}

float DpiScaleForScreenPoint(POINT screenPt, UINT fallbackDpi) {
    UINT dpi = fallbackDpi == 0 ? 96 : fallbackDpi;
    HMONITOR monitor = MonitorFromPoint(screenPt, MONITOR_DEFAULTTONEAREST);
    if (monitor) {
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        static GetDpiForMonitorFn getDpiForMonitor = []() -> GetDpiForMonitorFn {
            HMODULE shcore = LoadLibraryW(L"Shcore.dll");
            if (!shcore) {
                return nullptr;
            }
            return reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor"));
        }();
        if (getDpiForMonitor) {
            UINT xDpi = dpi;
            UINT yDpi = dpi;
            if (SUCCEEDED(getDpiForMonitor(monitor, 0, &xDpi, &yDpi)) && xDpi > 0) {
                dpi = xDpi;
            }
        }
    }
    return static_cast<float>(dpi) / 96.0f;
}

std::wstring ColorToHex(COLORREF c) {
    wchar_t buf[16]{};
    swprintf_s(buf, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

std::wstring ColorToRgbText(COLORREF c) {
    wchar_t buf[32]{};
    swprintf_s(buf, L"%d,%d,%d", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

HCURSOR InvisibleCursor() {
    static HCURSOR cursor = []() -> HCURSOR {
        BYTE andMask[32];
        BYTE xorMask[32];
        memset(andMask, 0xFF, sizeof(andMask));
        memset(xorMask, 0x00, sizeof(xorMask));
        return CreateCursor(nullptr, 0, 0, 16, 16, andMask, xorMask);
    }();
    return cursor;
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
    if (text.empty()) {
        return false;
    }
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hText = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hText) {
        return false;
    }
    void* ptr = GlobalLock(hText);
    if (!ptr) {
        GlobalFree(hText);
        return false;
    }
    memcpy(ptr, text.c_str(), bytes);
    GlobalUnlock(hText);

    if (!OpenClipboard(owner)) {
        GlobalFree(hText);
        return false;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, hText)) {
        CloseClipboard();
        GlobalFree(hText);
        return false;
    }
    CloseClipboard();
    return true;
}

void ApplyTextEditFormattingRect(HWND editHwnd) {
    if (!editHwnd) {
        return;
    }

    RECT client{};
    GetClientRect(editHwnd, &client);
    RECT formatRect = AnnotationRenderer::ComputeTextLayoutRect(client);
    SendMessageW(editHwnd, EM_SETRECTNP, 0, reinterpret_cast<LPARAM>(&formatRect));
}

void DrawHandle(Gdiplus::Graphics& g, int x, int y, float dpiScale) {
    const int s = std::max(6, static_cast<int>(std::round(kHandleSize * dpiScale)));
    Gdiplus::SolidBrush fill(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::Pen border(Gdiplus::Color(255, 17, 17, 17), 1.0f);
    Gdiplus::Rect r(x - s / 2, y - s / 2, s, s);
    g.FillRectangle(&fill, r);
    g.DrawRectangle(&border, r);
}

void DrawInfoText(Gdiplus::Graphics& g, const std::wstring& text, int x, int y, float dpiScale) {
    const float fontSize = std::max(13.0f, 13.0f * dpiScale);
    const float padX = 7.0f * dpiScale;
    const float padY = 4.0f * dpiScale;
    const float minW = 64.0f * dpiScale;
    const float minH = 24.0f * dpiScale;

    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font font(&ff, fontSize, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush bg(Gdiplus::Color(190, 20, 20, 20));
    Gdiplus::SolidBrush fg(Gdiplus::Color(255, 255, 255, 255));
    const auto oldTextHint = g.GetTextRenderingHint();
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    Gdiplus::RectF layout;
    g.MeasureString(text.c_str(), -1, &font, Gdiplus::PointF(0, 0), &layout);
    Gdiplus::RectF box(
        static_cast<float>(x),
        static_cast<float>(y),
        std::max(minW, layout.Width + padX * 2.0f),
        std::max(minH, layout.Height + padY * 2.0f)
    );
    g.FillRectangle(&bg, box);
    g.DrawString(text.c_str(), -1, &font, Gdiplus::RectF(box.X + padX, box.Y + padY, box.Width, box.Height), nullptr, &fg);
    g.SetTextRenderingHint(oldTextHint);
}

void DrawSelectionSizeHint(Gdiplus::Graphics& g, const RECT& selectionLocal, const RECT& virtualScreenRect, HWND hwnd) {
    const RECT sr = NormalizeRect(selectionLocal);
    if (RectWidth(sr) <= 0 || RectHeight(sr) <= 0) {
        return;
    }
    const int selW = RectWidth(sr);
    const int selH = RectHeight(sr);
    const int absX = sr.left + virtualScreenRect.left;
    const int absY = sr.top + virtualScreenRect.top;
    POINT labelScreenPt{ absX, absY };
    const UINT fallbackDpi = hwnd ? GetDpiForWindow(hwnd) : 96;
    const float infoDpiScale = DpiScaleForScreenPoint(labelScreenPt, fallbackDpi);
    const int infoOffsetY = std::max(20, static_cast<int>(std::lround(30.0f * infoDpiScale)));
    std::wstringstream ss;
    ss << selW << L"x" << selH;
    DrawInfoText(g, ss.str(), static_cast<int>(sr.left),
        std::max(0, static_cast<int>(sr.top) - infoOffsetY), infoDpiScale);
}

Gdiplus::Rect ToGdiRect(const RECT& rc) {
    RECT n = NormalizeRect(rc);
    return Gdiplus::Rect(n.left, n.top, RectWidth(n), RectHeight(n));
}

POINT MakePoint(int x, int y) {
    POINT p{};
    p.x = x;
    p.y = y;
    return p;
}

void MoveRectBy(RECT& rc, int dx, int dy) {
    rc.left += dx;
    rc.right += dx;
    rc.top += dy;
    rc.bottom += dy;
}

void AppendSmoothPenPoint(std::vector<POINT>& points, POINT next) {
    if (points.empty()) {
        points.push_back(next);
        return;
    }

    POINT last = points.back();
    const int dx = next.x - last.x;
    const int dy = next.y - last.y;
    const double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));
    if (dist < 0.5) {
        return;
    }

    const int steps = std::max(1, static_cast<int>(std::ceil(dist / 1.5)));
    for (int i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(steps);
        POINT p{};
        p.x = static_cast<LONG>(std::lround(last.x + dx * t));
        p.y = static_cast<LONG>(std::lround(last.y + dy * t));
        if (points.empty() || p.x != points.back().x || p.y != points.back().y) {
            points.push_back(p);
        }
    }
}

void DrawMosaicRegion(Gdiplus::Graphics& g, Gdiplus::Bitmap* source, const RECT& rect, int block) {
    if (!source) {
        return;
    }

    RECT r = NormalizeRect(rect);
    if (RectWidth(r) <= 1 || RectHeight(r) <= 1) {
        return;
    }

    const int srcW = static_cast<int>(source->GetWidth());
    const int srcH = static_cast<int>(source->GetHeight());
    r.left = std::clamp(static_cast<int>(r.left), 0, std::max(0, srcW - 1));
    r.top = std::clamp(static_cast<int>(r.top), 0, std::max(0, srcH - 1));
    r.right = std::clamp(static_cast<int>(r.right), static_cast<int>(r.left + 1), srcW);
    r.bottom = std::clamp(static_cast<int>(r.bottom), static_cast<int>(r.top + 1), srcH);
    if (RectWidth(r) <= 1 || RectHeight(r) <= 1) {
        return;
    }

    const int mosaicBlock = std::max(3, block);
    const int smallW = std::max(1, RectWidth(r) / mosaicBlock);
    const int smallH = std::max(1, RectHeight(r) / mosaicBlock);

    Gdiplus::Bitmap reduced(smallW, smallH, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics rg(&reduced);
        rg.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
        rg.DrawImage(source,
            Gdiplus::Rect(0, 0, smallW, smallH),
            r.left, r.top, RectWidth(r), RectHeight(r), Gdiplus::UnitPixel);
    }

    Gdiplus::GraphicsState st = g.Save();
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.DrawImage(&reduced,
        Gdiplus::Rect(r.left, r.top, RectWidth(r), RectHeight(r)),
        0, 0, smallW, smallH, Gdiplus::UnitPixel);
    g.Restore(st);
}

void DrawArrowWithHeadSpacing(Gdiplus::Graphics& g, Gdiplus::Pen& pen,
    const Gdiplus::PointF& p1, const Gdiplus::PointF& p2, const Gdiplus::Color& color, float strokeWidth) {
    AnnotationRenderer::DrawArrow(g, pen, p1, p2, color, strokeWidth);
}

bool IsNearRectBorder(const RECT& rc, POINT p, int thickness) {
    RECT n = NormalizeRect(rc);
    if (!PtInRect(&n, p)) {
        return false;
    }
    RECT inner = n;
    InflateRect(&inner, -thickness, -thickness);
    return !PtInRect(&inner, p);
}

std::filesystem::path DefaultSelectionSavePath() {
    std::filesystem::path out = KnownFolderUtil::GetPathOr(FOLDERID_Desktop, std::filesystem::temp_directory_path());
    out /= (L"SnapPin_" + FormatNowForFile() + L".png");
    return out;
}

LRESULT CALLBACK TextEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR refData) {
    const HWND parent = reinterpret_cast<HWND>(refData);
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_ESCAPE) {
            if (parent) {
                PostMessageW(parent, WMU_OVERLAY_TEXT_CANCEL, 0, 0);
            }
            return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK PreviewHostSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR) {
    auto forwardMouseMessageToParent = [&](UINT forwardMsg) -> LRESULT {
        HWND parent = GetParent(hwnd);
        if (!parent) {
            return 0;
        }
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hwnd, &pt);
        ScreenToClient(parent, &pt);
        return SendMessageW(parent, forwardMsg, wParam, MAKELPARAM(pt.x, pt.y));
    };
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        return TRUE;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
        return forwardMouseMessageToParent(msg);
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, PreviewHostSubclassProc, 1);
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}
}

bool ScaleImageBicubic(const Image& src, int dstW, int dstH, Image& out);

OverlayWindow::OverlayWindow() = default;
OverlayWindow::~OverlayWindow() {
    Close();
}

void OverlayWindow::PreloadUi(HINSTANCE hInstance) {
    static std::once_flag overlayClassOnce;
    static std::once_flag overlayHudClassOnce;
    WindowUtil::RegisterWindowClassOnce(
        overlayClassOnce,
        hInstance,
        kOverlayWindowClassName,
        OverlayWindow::WndProc,
        LoadCursorW(nullptr, IDC_CROSS));
    WindowUtil::RegisterWindowClassOnce(
        overlayHudClassOnce,
        hInstance,
        kOverlayHudWindowClassName,
        OverlayWindow::HudWndProc,
        LoadCursorW(nullptr, IDC_CROSS));
}

bool OverlayWindow::Show(HINSTANCE hInstance, const ScreenCapture& capture, bool fullScreenSelection, FinishedCallback callback,
    const std::optional<RECT>& fullSelectionScreenRect) {
    if (!capture.image.IsValid()) {
        return false;
    }

    hInstance_ = hInstance;
    capture_ = capture;
    callback_ = std::move(callback);
    windows_ = CaptureService().EnumerateWindows();
    hoverWindowRect_.reset();
    followHudEnabled_ = true;
    toolbarHiddenByShiftPrecision_ = false;
    screenRecordingMode_ = false;
    recordingPreviewMode_ = false;
    recordingStartPending_ = false;
    recordingActive_ = false;
    recordingPaused_ = false;

    PreloadUi(hInstance_);

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kOverlayWindowClassName,
        L"SnapPin Overlay",
        WS_POPUP,
        capture_.virtualRect.left,
        capture_.virtualRect.top,
        RectWidth(capture_.virtualRect),
        RectHeight(capture_.virtualRect),
        nullptr,
        nullptr,
        hInstance_,
        this
    );

    if (!hwnd_) {
        return false;
    }

    captureBitmap_ = std::make_unique<Gdiplus::Bitmap>(capture_.image.width, capture_.image.height, PixelFormat32bppARGB);
    if (captureBitmap_) {
        Gdiplus::Rect r(0, 0, capture_.image.width, capture_.image.height);
        Gdiplus::BitmapData data{};
        if (captureBitmap_->LockBits(&r, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) == Gdiplus::Ok) {
            for (int y = 0; y < capture_.image.height; ++y) {
                uint8_t* dst = static_cast<uint8_t*>(data.Scan0) + static_cast<size_t>(data.Stride) * static_cast<size_t>(y);
                const uint8_t* src = capture_.image.bgra.data() + static_cast<size_t>(capture_.image.width) * static_cast<size_t>(y) * 4;
                memcpy(dst, src, static_cast<size_t>(capture_.image.width) * 4);
            }
            captureBitmap_->UnlockBits(&data);
        }
    }

    if (fullScreenSelection) {
        if (fullSelectionScreenRect.has_value() && IsRectValid(*fullSelectionScreenRect)) {
            RECT sr = NormalizeRect(*fullSelectionScreenRect);
            RECT clip{};
            if (IntersectRect(&clip, &sr, &capture_.virtualRect)) {
                selection_ = RECT{
                    clip.left - capture_.virtualRect.left,
                    clip.top - capture_.virtualRect.top,
                    clip.right - capture_.virtualRect.left,
                    clip.bottom - capture_.virtualRect.top
                };
                selection_ = NormalizeRect(selection_);
                selection_.left = std::clamp(selection_.left, 0L, static_cast<LONG>(capture_.image.width - 1));
                selection_.top = std::clamp(selection_.top, 0L, static_cast<LONG>(capture_.image.height - 1));
                selection_.right = std::clamp(selection_.right, selection_.left + 1, static_cast<LONG>(capture_.image.width));
                selection_.bottom = std::clamp(selection_.bottom, selection_.top + 1, static_cast<LONG>(capture_.image.height));
            } else {
                selection_ = RECT{0, 0, capture_.image.width, capture_.image.height};
            }
        } else {
            selection_ = RECT{0, 0, capture_.image.width, capture_.image.height};
        }
        stage_ = Stage::Annotating;
        cursorInfoEnabled_ = true;
        hoverWindowRect_.reset();
        dragMode_ = DragMode::None;
    }

    POINT screenCursor{};
    if (GetCursorPos(&screenCursor)) {
        POINT local{screenCursor.x - capture_.virtualRect.left, screenCursor.y - capture_.virtualRect.top};
        local.x = std::clamp(local.x, 0L, std::max(0L, static_cast<LONG>(capture_.image.width - 1)));
        local.y = std::clamp(local.y, 0L, std::max(0L, static_cast<LONG>(capture_.image.height - 1)));
        lastMouse_ = local;
    } else if (fullScreenSelection) {
        lastMouse_.x = capture_.image.width / 2;
        lastMouse_.y = capture_.image.height / 2;
    }
    precisionLastRaw_ = lastMouse_;
    precisionMouseX_ = static_cast<double>(lastMouse_.x);
    precisionMouseY_ = static_cast<double>(lastMouse_.y);

    staticSceneDirty_ = true;
    EnsureStaticSceneBitmap();

    toolbar_.Create(hwnd_, hInstance_);
    previewBar_.Create(hwnd_, hInstance_);
    previewBar_.Hide();
    previewPlayer_ = std::make_unique<VideoPreviewPlayer>(hwnd_);
    screenRecorder_ = std::make_unique<ScreenRecorder>();
    toolbar_.SetLongCaptureMode(false);
    toolbar_.SetWhiteboardMode(false);
    toolbar_.SetScreenRecordingMode(false);
    toolbar_.SetRecordingState(false, false);
    toolbar_.SetActiveTool(tool_);
    ApplyToolbarStyle();
    EnsureHudWindows();

    ShowWindow(hwnd_, SW_SHOW);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_NOERASE);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);

    if (stage_ == Stage::Annotating && HasSelection()) {
        RefreshToolbarPlacement();
        RefreshFollowHudFromLastMouse();
    } else {
        toolbar_.Hide();
        ClearFollowHud();
    }

    return true;
}

void OverlayWindow::Close() {
    StopWindowTimer(recordingStartTimer_);
    StopWindowTimer(previewProgressTimer_);
    if (screenRecorder_) {
        screenRecorder_->Cancel();
    }
    if (previewPlayer_) {
        previewPlayer_->Close();
    }
    DestroyPreviewHostWindow();
    StopWindowTimer(longCaptureTimer_);

    std::error_code ec;
    if (!recordingTempPath_.empty()) {
        std::filesystem::remove(recordingTempPath_, ec);
    }
    recordingTempPath_.clear();
    lastRecordingResult_.reset();

    longCaptureMode_ = false;
    whiteboardMode_ = false;
    screenRecordingMode_ = false;
    recordingPreviewMode_ = false;
    recordingStartPending_ = false;
    recordingActive_ = false;
    recordingPaused_ = false;
    longCaptureTargetHwnd_ = nullptr;
    preRecordingForegroundHwnd_ = nullptr;
    longCaptureScrollDir_ = 0;
    longCaptureMatchAccepted_ = true;
    longCaptureThumbRect_.reset();
    toolbar_.SetLongCaptureMode(false);
    toolbar_.SetWhiteboardMode(false);
    toolbar_.SetScreenRecordingMode(false);
    toolbar_.SetRecordingState(false, false);
    toolbar_.SetActiveTool(ToolType::None);
    longCaptureThumbCacheReady_ = false;
    longCaptureThumbDirty_ = false;
    longCaptureThumbLastRenderTick_ = 0;
    longCaptureThumbCache_ = {};
    EndTextEdit(false);
    DestroyHudWindows();
    toolbarHiddenByShiftPrecision_ = false;
    previewBar_.Hide();
    if (previewBar_.Hwnd()) {
        previewBar_.Destroy();
    }
    if (toolbar_.Hwnd()) {
        toolbar_.Destroy();
    }
    paintBuffer_.Reset();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void OverlayWindow::StopWindowTimer(UINT_PTR& timerId) {
    if (timerId == 0) {
        return;
    }
    if (hwnd_) {
        KillTimer(hwnd_, timerId);
    }
    timerId = 0;
}

void OverlayWindow::StopOverlayTimers() {
    StopWindowTimer(longCaptureTimer_);
    StopWindowTimer(recordingStartTimer_);
    StopWindowTimer(previewProgressTimer_);
}

bool OverlayWindow::TryHandleColorCopyHotkey() {
    if (!hwnd_ || textEditing_) {
        return false;
    }
    if (!cursorInfoEnabled_ || !followHudEnabled_ || !HasSelection() || dragMode_ != DragMode::None) {
        return false;
    }
    if (!ShouldShowCursorInfoOverlay()) {
        return false;
    }

    POINT mp = lastMouse_;
    if (mp.x < 0 || mp.y < 0 || mp.x >= capture_.image.width || mp.y >= capture_.image.height) {
        return false;
    }

    RECT sr = SelectionRectNormalized();
    if (!PtInRect(&sr, mp)) {
        return false;
    }

    COLORREF c = capture_.image.GetPixel(mp.x, mp.y);
    const std::wstring colorValue = colorHexMode_ ? ColorToHex(c) : ColorToRgbText(c);
    if (!CopyTextToClipboard(hwnd_, colorValue)) {
        return false;
    }
    return true;
}

void OverlayWindow::CancelCapture() {
    if (!hwnd_) {
        return;
    }
    Finish(OverlayAction::Cancel);
}

void OverlayWindow::ExitToCursorMode() {
    if (!hwnd_ || !HasSelection()) {
        return;
    }

    if (longCaptureMode_) {
        Finish(OverlayAction::Cancel);
        return;
    }

    if (textEditing_) {
        EndTextEdit(true);
    }
    if (dragMode_ != DragMode::None) {
        dragMode_ = DragMode::None;
        ReleaseCapture();
    }
    hasCurrentShape_ = false;
    currentShape_ = {};
    selectedShape_ = -1;
    activeShapeHit_ = HitKind::None;
    tool_ = ToolType::None;
    toolbar_.SetActiveTool(tool_);
    ApplyToolbarStyle();
    RefreshToolbarPlacement();
    MarkSceneDirty();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK OverlayWindow::HudWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OverlayWindow* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return TRUE;
    }
    if (!self) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        self->OnHudPaint(hwnd);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

LRESULT OverlayWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (toolbar_.IsColorDialogOpen()) {
        switch (msg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
        case WM_SYSCHAR:
        case WM_COMMAND:
            return 0;
        default:
            break;
        }
    }

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        if (longCaptureMode_) {
            return HTTRANSPARENT;
        }
        if (screenRecordingMode_) {
            if (!HasSelection()) {
                return HTTRANSPARENT;
            }
            POINT screenPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            POINT localPt = ScreenToLocalPoint(screenPt);
            RECT sr = SelectionRectNormalized();
            const HitKind hit = HitTestRectHandles(sr, localPt, kHandleSize + 8);
            const float dpiScale = static_cast<float>(GetDpiForWindow(hwnd_) ? GetDpiForWindow(hwnd_) : 96) / 96.0f;
            const int borderGrab = std::max(6, static_cast<int>(std::round(kSelectionBorderGrab * dpiScale)));
            const bool onBorder = (hit != HitKind::None && hit != HitKind::Inside) || IsNearRectBorder(sr, localPt, borderGrab);
            return onBorder ? HTCLIENT : HTTRANSPARENT;
        }
        if (recordingPreviewMode_) {
            if (!HasSelection()) {
                return HTTRANSPARENT;
            }
            POINT screenPt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            POINT localPt = ScreenToLocalPoint(screenPt);
            RECT sr = SelectionRectNormalized();
            const HitKind hit = HitTestRectHandles(sr, localPt, kHandleSize + 8);
            const float dpiScale = static_cast<float>(GetDpiForWindow(hwnd_) ? GetDpiForWindow(hwnd_) : 96) / 96.0f;
            const int borderGrab = std::max(6, static_cast<int>(std::round(kSelectionBorderGrab * dpiScale)));
            const bool onBorder = (hit != HitKind::None && hit != HitKind::Inside) || IsNearRectBorder(sr, localPt, borderGrab);
            return (onBorder || PtInRect(&sr, localPt)) ? HTCLIENT : HTTRANSPARENT;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_PAINT:
        OnPaint();
        return 0;
    case WM_TIMER:
        if (wParam == IDT_LONG_CAPTURE) {
            OnLongCaptureTimer();
            return 0;
        }
        if (wParam == IDT_RECORDING_START) {
            KillTimer(hwnd_, IDT_RECORDING_START);
            recordingStartTimer_ = 0;
            recordingStartPending_ = false;
            StartScreenRecording();
            return 0;
        }
        if (wParam == IDT_PREVIEW_PROGRESS) {
            UpdateRecordingPreviewProgress();
            return 0;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_LBUTTONDOWN:
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0 && dragMode_ == DragMode::None) {
            return 0;
        }
        OnMouseDown(MakePoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
        return 0;
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0 && dragMode_ == DragMode::None) {
            return 0;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_MOUSEMOVE: {
        if (!mouseLeaveTracking_) {
            TRACKMOUSEEVENT tme{};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd_;
            if (TrackMouseEvent(&tme)) {
                mouseLeaveTracking_ = true;
            }
        }

        POINT pt = MakePoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        WPARAM keys = wParam;
        const bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool shouldCoalesce = !(precisionModeActive_ || shiftDown);
        if (shouldCoalesce) {
            MSG pending{};
            while (PeekMessageW(&pending, hwnd_, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_REMOVE)) {
                pt = MakePoint(GET_X_LPARAM(pending.lParam), GET_Y_LPARAM(pending.lParam));
                keys = pending.wParam;
            }
        }
        OnMouseMove(pt, keys);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        if (longCaptureMode_ || screenRecordingMode_ || recordingPreviewMode_) {
            return MA_NOACTIVATE;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (longCaptureMode_) {
            POINT sp{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (msg == WM_MOUSEWHEEL) {
                const SHORT delta = GET_WHEEL_DELTA_WPARAM(wParam);
                if (delta < 0) {
                    longCaptureScrollDir_ = +1;
                } else if (delta > 0) {
                    longCaptureScrollDir_ = -1;
                }
            }
            HWND wheelTarget = WindowFromPoint(sp);
            if (wheelTarget == hwnd_ ||
                (toolbar_.Hwnd() && (wheelTarget == toolbar_.Hwnd() || IsChild(toolbar_.Hwnd(), wheelTarget)))) {
                wheelTarget = nullptr;
            }

            if (!wheelTarget || !IsWindow(wheelTarget)) {
                wheelTarget = longCaptureTargetHwnd_;
            }
            if (wheelTarget && IsWindow(wheelTarget)) {
                POINT cp = sp;
                ScreenToClient(wheelTarget, &cp);
                HWND child = ChildWindowFromPointEx(wheelTarget, cp, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
                if (child && child != wheelTarget) {
                    wheelTarget = child;
                }
                SendMessageW(wheelTarget, msg, wParam, lParam);
            }
            return 0;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_MOUSELEAVE:
        mouseLeaveTracking_ = false;
        lastMouse_ = POINT{-32768, -32768};
        hoverWindowRect_.reset();
        precisionModeActive_ = false;
        precisionBounds_ = RECT{};
        lastInfoRect_.reset();
        lastMagnifierRect_.reset();
        lastVerticalGuideRect_.reset();
        lastHorizontalGuideRect_.reset();
        UpdateHudWindows(std::nullopt, std::nullopt, std::nullopt, std::nullopt);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            if (recordingPreviewMode_) {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                return TRUE;
            }
            POINT p{};
            GetCursorPos(&p);
            ScreenToClient(hwnd_, &p);
            UpdateCursorVisual(p);
            return TRUE;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_LBUTTONUP:
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0 && dragMode_ == DragMode::None) {
            return 0;
        }
        OnMouseUp(MakePoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
        return 0;
    case WM_RBUTTONUP:
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0 && dragMode_ == DragMode::None) {
            return 0;
        }
        OnRightClick(MakePoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
        return 0;
    case WM_MBUTTONUP:
        if ((GetKeyState(VK_SHIFT) & 0x8000) != 0 && dragMode_ == DragMode::None) {
            return 0;
        }
        OnMiddleClick(MakePoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
        return 0;
    case WM_CONTEXTMENU:
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && stage_ == Stage::Selecting) {
            if (dragMode_ != DragMode::None) {
                dragMode_ = DragMode::None;
                ReleaseCapture();
            }
            Finish(OverlayAction::Cancel);
            return 0;
        }
        OnKeyDown(wParam, lParam);
        return 0;
    case WM_KEYUP:
        if (wParam == VK_SHIFT) {
            const bool wasPrecision = precisionModeActive_;
            if (wasPrecision) {
                SetCursor(InvisibleCursor());
                SnapCursorToCrosshair();
            }
            precisionModeActive_ = false;
            if (toolbarHiddenByShiftPrecision_) {
                if (stage_ == Stage::Annotating && HasSelection() && !longCaptureMode_) {
                    RefreshToolbarPlacement();
                }
                toolbarHiddenByShiftPrecision_ = false;
            }
            UpdateCursorVisual(lastMouse_);
            return 0;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_SYSKEYDOWN:
        if (wParam == VK_ESCAPE && stage_ == Stage::Selecting) {
            if (dragMode_ != DragMode::None) {
                dragMode_ = DragMode::None;
                ReleaseCapture();
            }
            Finish(OverlayAction::Cancel);
            return 0;
        }
        if (wParam == VK_MENU) {
            OnKeyDown(wParam, lParam);
            return 0;
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WMU_OVERLAY_TEXT_COMMIT:
        EndTextEdit(true);
        return 0;
    case WMU_OVERLAY_TEXT_CANCEL:
        EndTextEdit(false);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_OVERLAY_TEXT_EDIT) {
            if (HIWORD(wParam) == EN_CHANGE) {
                ResizeTextEditToFit();
            } else if (HIWORD(wParam) == EN_KILLFOCUS) {
                EndTextEdit(true);
            }
            return 0;
        }
        OnToolbarCommand(LOWORD(wParam), HIWORD(wParam));
        return 0;
    case WMAPP_VIDEO_PREVIEW_EVENT:
        if (wParam == VideoPreviewPlayer::EventReady) {
            const LONGLONG duration = previewPlayer_ ? previewPlayer_->Duration100ns() : (lastRecordingResult_.has_value() ? lastRecordingResult_->duration100ns : 0);
            if (previewPlayer_) {
                previewPlayer_->SetRate(previewBar_.PlaybackRate());
                previewPlayer_->PrimeFirstFrame(0);
            }
            const LONGLONG position = previewPlayer_ ? previewPlayer_->Position100ns() : 0;
            previewBar_.SetPreviewMetrics(duration, position);
            previewBar_.SetPlaying(false);
            previewSeekWarmupNeeded_ = false;
        } else if (wParam == VideoPreviewPlayer::EventEnded) {
            const LONGLONG duration = previewPlayer_ ? previewPlayer_->Duration100ns() : (lastRecordingResult_.has_value() ? lastRecordingResult_->duration100ns : 0);
            previewBar_.SetPlaying(false);
            previewBar_.SetPreviewMetrics(duration, duration);
            previewSeekWarmupNeeded_ = true;
        } else if (wParam == VideoPreviewPlayer::EventError) {
            previewBar_.SetPlaying(false);
            previewSeekWarmupNeeded_ = false;
            MessageBoxW(hwnd_, L"\u89C6\u9891\u9884\u89C8\u5931\u8D25", L"SnapPin", MB_ICONERROR);
        }
        return 0;
    case WMU_PREVIEW_EXPORT_DONE: {
        auto payload = WindowMessagePayload::Take<PreviewExportDonePayload>(lParam);
        previewExporting_ = false;
        if (!payload) {
            return 0;
        }
        if (payload->success) {
            ExitRecordingPreviewMode(true);
            Finish(OverlayAction::Cancel);
        } else {
            const std::wstring message = payload->errorMessage.empty()
                ? L"\u5BFC\u51FA\u5931\u8D25"
                : payload->errorMessage;
            MessageBoxW(hwnd_, message.c_str(), L"SnapPin", MB_ICONERROR);
        }
        return 0;
    }
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lParam) == textEdit_) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, textDraftShape_.color);
            SetBkMode(hdc, OPAQUE);
            SetBrushOrgEx(hdc, 0, 0, nullptr);
            return reinterpret_cast<LRESULT>(textEditBgBrush_ ? textEditBgBrush_ : GetStockObject(WHITE_BRUSH));
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    case WM_DESTROY:
        StopOverlayTimers();
        DestroyPreviewHostWindow();
        DestroyHudWindows();
        DestroyTextEditControl();
        textEditing_ = false;
        paintBuffer_.Reset();
        hwnd_ = nullptr;
        return 0;
    default:
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

void OverlayWindow::MarkSceneDirty() {
    staticSceneDirty_ = true;
}

void OverlayWindow::EnsureStaticSceneBitmap() {
    if (!hwnd_ || capture_.image.width <= 0 || capture_.image.height <= 0) {
        return;
    }

    if (!staticSceneBitmap_ ||
        static_cast<int>(staticSceneBitmap_->GetWidth()) != capture_.image.width ||
        static_cast<int>(staticSceneBitmap_->GetHeight()) != capture_.image.height) {
        staticSceneBitmap_ = std::make_unique<Gdiplus::Bitmap>(capture_.image.width, capture_.image.height, PixelFormat32bppARGB);
        staticSceneDirty_ = true;
    }

    if (!staticSceneDirty_ || !staticSceneBitmap_) {
        return;
    }

    Gdiplus::Graphics sg(staticSceneBitmap_.get());
    sg.SetSmoothingMode(Gdiplus::SmoothingModeHighSpeed);
    sg.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
    sg.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    const UINT dpi = GetDpiForWindow(hwnd_) ? GetDpiForWindow(hwnd_) : 96;
    const float dpiScale = static_cast<float>(dpi) / 96.0f;
    RenderStaticScene(sg, dpiScale);
    staticSceneDirty_ = false;
}

float OverlayWindow::DpiScaleForLocalPoint(POINT localPoint) const {
    const UINT fallbackDpi = GetDpiForWindow(hwnd_) ? GetDpiForWindow(hwnd_) : 96;
    return DpiScaleForScreenPoint(LocalToScreenPoint(localPoint), fallbackDpi);
}

float OverlayWindow::LogicalTextSizeToPixels(float logicalSize, POINT localPoint) const {
    const float logical = std::max(8.0f, logicalSize);
    return logical * DpiScaleForLocalPoint(localPoint);
}

void OverlayWindow::ApplyToolbarStyle() {
    strokeWidth_ = std::clamp(toolbar_.StrokeWidth(), 1.0f, 32.0f);
    currentColor_ = toolbar_.StrokeColor();
    fillEnabled_ = toolbar_.FillEnabled();
    fillColor_ = toolbar_.FillColor();
    textColor_ = toolbar_.TextColor();
    textSize_ = std::clamp(toolbar_.TextSize(), 8.0f, 96.0f);
    textStyle_ = toolbar_.TextStyle();
    if (textEditing_) {
        textDraftShape_.color = textColor_;
        POINT anchor{ textEditRect_.left, textEditRect_.top };
        textDraftShape_.textSize = LogicalTextSizeToPixels(textSize_, anchor);
        textDraftShape_.textStyle = textStyle_;
        UpdateTextEditFont();
    }
    ApplyStyleToSelectedShape();
}

void OverlayWindow::ApplyStyleToSelectedShape() {
    if (selectedShape_ < 0 || selectedShape_ >= static_cast<int>(shapes_.size())) {
        return;
    }

    AnnotationShape& shape = shapes_[selectedShape_];
    bool changed = false;
    bool undoPushed = false;
    auto ensureUndo = [&]() {
        if (!undoPushed) {
            PushUndo();
            undoPushed = true;
        }
    };
    auto applyColor = [&](COLORREF& field, COLORREF value) {
        if (field != value) {
            ensureUndo();
            field = value;
            changed = true;
        }
    };
    auto applyBool = [&](bool& field, bool value) {
        if (field != value) {
            ensureUndo();
            field = value;
            changed = true;
        }
    };
    auto applyInt = [&](INT& field, INT value) {
        if (field != value) {
            ensureUndo();
            field = value;
            changed = true;
        }
    };
    auto applyFloat = [&](float& field, float value) {
        if (std::fabs(field - value) > 0.01f) {
            ensureUndo();
            field = value;
            changed = true;
        }
    };

    switch (shape.type) {
    case ToolType::Rect:
    case ToolType::Ellipse:
        applyColor(shape.color, currentColor_);
        applyFloat(shape.stroke, strokeWidth_);
        applyBool(shape.fillEnabled, fillEnabled_);
        applyColor(shape.fillColor, fillColor_);
        break;
    case ToolType::Line:
    case ToolType::Arrow:
    case ToolType::Pen:
    case ToolType::Mosaic:
    case ToolType::Number:
        applyColor(shape.color, currentColor_);
        applyFloat(shape.stroke, strokeWidth_);
        break;
    case ToolType::Text: {
        applyColor(shape.color, textColor_);
        const POINT anchor{ shape.rect.left, shape.rect.top };
        const float newTextSize = LogicalTextSizeToPixels(textSize_, anchor);
        const INT newTextStyle = textStyle_;
        const bool sizeChanged = std::fabs(shape.textSize - newTextSize) > 0.01f;
        const bool styleChanged = shape.textStyle != newTextStyle;
        applyFloat(shape.textSize, newTextSize);
        applyInt(shape.textStyle, newTextStyle);
        if ((sizeChanged || styleChanged) && !shape.text.empty() && hwnd_) {
            HDC hdc = GetDC(hwnd_);
            if (hdc) {
                UiGdi::ScopedGdiObject<HFONT> measureFont(CreateOverlayTextFontHandle(shape.textSize, shape.textStyle));
                const TextLayoutMetrics metrics = MeasureTextLayout(hdc, measureFont.Get(), shape.text);
                ReleaseDC(hwnd_, hdc);
                const RECT sr = HasSelection()
                    ? SelectionRectNormalized()
                    : RECT{ 0, 0, capture_.image.width, capture_.image.height };
                const RECT fitted = FitTextRectToSelection(sr, anchor,
                    metrics.contentWidth + kTextEditPaddingX * 2,
                    metrics.contentHeight + kTextEditPaddingY * 2);
                if (shape.rect.left != fitted.left || shape.rect.top != fitted.top ||
                    shape.rect.right != fitted.right || shape.rect.bottom != fitted.bottom) {
                    ensureUndo();
                    shape.rect = fitted;
                    changed = true;
                }
            }
        }
        break;
    }
    default:
        break;
    }

    if (changed) {
        selectedShapeBounds_ = ShapeBounds(shape);
        MarkSceneDirty();
    }
}

void OverlayWindow::EnsureHudWindows() {
    if (!hwnd_) {
        return;
    }

    PreloadUi(hInstance_);

    auto createHud = [&](HWND& outHwnd, DWORD extraExStyle, BYTE layeredAlpha) {
        if (outHwnd) {
            return;
        }
        outHwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | extraExStyle,
            kOverlayHudWindowClassName,
            L"",
            WS_POPUP,
            0, 0, 1, 1,
            hwnd_,
            nullptr,
            hInstance_,
            this
        );
        if (outHwnd) {
            if ((extraExStyle & WS_EX_LAYERED) != 0) {
                SetLayeredWindowAttributes(outHwnd, 0, layeredAlpha, LWA_ALPHA);
            }
            ShowWindow(outHwnd, SW_HIDE);
        }
    };

    createHud(verticalGuideHudHwnd_, WS_EX_LAYERED, kCrosshairHudAlpha);
    createHud(horizontalGuideHudHwnd_, WS_EX_LAYERED, kCrosshairHudAlpha);
    createHud(magnifierHudHwnd_, 0, 255);
    createHud(infoHudHwnd_, 0, 255);
}

void OverlayWindow::DestroyHudWindows() {
    if (infoHudHwnd_) {
        DestroyWindow(infoHudHwnd_);
        infoHudHwnd_ = nullptr;
    }
    if (magnifierHudHwnd_) {
        DestroyWindow(magnifierHudHwnd_);
        magnifierHudHwnd_ = nullptr;
    }
    if (verticalGuideHudHwnd_) {
        DestroyWindow(verticalGuideHudHwnd_);
        verticalGuideHudHwnd_ = nullptr;
    }
    if (horizontalGuideHudHwnd_) {
        DestroyWindow(horizontalGuideHudHwnd_);
        horizontalGuideHudHwnd_ = nullptr;
    }
    infoHudBackBuffer_.Reset();
    magnifierHudBackBuffer_.Reset();
    verticalGuideHudBackBuffer_.Reset();
    horizontalGuideHudBackBuffer_.Reset();
}

void OverlayWindow::UpdateHudWindows(std::optional<RECT> infoRect, std::optional<RECT> magnifierRect,
    std::optional<RECT> verticalGuideRect, std::optional<RECT> horizontalGuideRect) {
    struct PendingOp {
        HWND hwnd = nullptr;
        bool show = false;
        bool wasVisible = false;
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        bool moveOrShow = false;
        bool invalidate = false;
    };

    auto build = [&](HWND hud, const std::optional<RECT>& localRect) -> PendingOp {
        PendingOp op{};
        op.hwnd = hud;
        if (!hud) {
            return op;
        }
        op.wasVisible = IsWindowVisible(hud) != FALSE;

        if (!localRect.has_value() || RectWidth(*localRect) <= 0 || RectHeight(*localRect) <= 0) {
            if (op.wasVisible) {
                ShowWindow(hud, SW_HIDE);
            }
            return op;
        }

        RECT lr = *localRect;
        op.x = lr.left + capture_.virtualRect.left;
        op.y = lr.top + capture_.virtualRect.top;
        op.w = RectWidth(lr);
        op.h = RectHeight(lr);
        op.show = true;
        op.invalidate = true;

        RECT currentScreen{};
        bool sameRect = false;
        if (GetWindowRect(hud, &currentScreen)) {
            sameRect = (currentScreen.left == op.x && currentScreen.top == op.y &&
                RectWidth(currentScreen) == op.w && RectHeight(currentScreen) == op.h);
        }
        op.moveOrShow = (!sameRect || !op.wasVisible);
        return op;
    };

    PendingOp magOp = build(magnifierHudHwnd_, magnifierRect);
    PendingOp infoOp = build(infoHudHwnd_, infoRect);
    PendingOp verticalOp = build(verticalGuideHudHwnd_, verticalGuideRect);
    PendingOp horizontalOp = build(horizontalGuideHudHwnd_, horizontalGuideRect);

    int deferCount = 0;
    if (magOp.hwnd && magOp.show && magOp.moveOrShow) {
        ++deferCount;
    }
    if (infoOp.hwnd && infoOp.show && infoOp.moveOrShow) {
        ++deferCount;
    }
    if (verticalOp.hwnd && verticalOp.show && verticalOp.moveOrShow) {
        ++deferCount;
    }
    if (horizontalOp.hwnd && horizontalOp.show && horizontalOp.moveOrShow) {
        ++deferCount;
    }
    if (deferCount > 0) {
        HDWP hdwp = BeginDeferWindowPos(deferCount);
        if (hdwp) {
            if (magOp.hwnd && magOp.show && magOp.moveOrShow) {
                UINT flags = SWP_NOACTIVATE | SWP_NOZORDER;
                if (!magOp.wasVisible) {
                    flags |= SWP_SHOWWINDOW;
                }
                hdwp = DeferWindowPos(hdwp, magOp.hwnd, nullptr, magOp.x, magOp.y, magOp.w, magOp.h, flags);
            }
            if (infoOp.hwnd && infoOp.show && infoOp.moveOrShow) {
                UINT flags = SWP_NOACTIVATE | SWP_NOZORDER;
                if (!infoOp.wasVisible) {
                    flags |= SWP_SHOWWINDOW;
                }
                hdwp = DeferWindowPos(hdwp, infoOp.hwnd, nullptr, infoOp.x, infoOp.y, infoOp.w, infoOp.h, flags);
            }
            if (verticalOp.hwnd && verticalOp.show && verticalOp.moveOrShow) {
                UINT flags = SWP_NOACTIVATE | SWP_NOZORDER;
                if (!verticalOp.wasVisible) {
                    flags |= SWP_SHOWWINDOW;
                }
                hdwp = DeferWindowPos(hdwp, verticalOp.hwnd, nullptr, verticalOp.x, verticalOp.y, verticalOp.w, verticalOp.h, flags);
            }
            if (horizontalOp.hwnd && horizontalOp.show && horizontalOp.moveOrShow) {
                UINT flags = SWP_NOACTIVATE | SWP_NOZORDER;
                if (!horizontalOp.wasVisible) {
                    flags |= SWP_SHOWWINDOW;
                }
                hdwp = DeferWindowPos(hdwp, horizontalOp.hwnd, nullptr, horizontalOp.x, horizontalOp.y, horizontalOp.w, horizontalOp.h, flags);
            }
            if (hdwp) {
                EndDeferWindowPos(hdwp);
            }
        }
    }

    if (magOp.hwnd && magOp.invalidate) {
        InvalidateRect(magOp.hwnd, nullptr, FALSE);
    }
    if (infoOp.hwnd && infoOp.invalidate) {
        InvalidateRect(infoOp.hwnd, nullptr, FALSE);
    }
    if (verticalOp.hwnd && verticalOp.invalidate) {
        InvalidateRect(verticalOp.hwnd, nullptr, FALSE);
    }
    if (horizontalOp.hwnd && horizontalOp.invalidate) {
        InvalidateRect(horizontalOp.hwnd, nullptr, FALSE);
    }
}

void OverlayWindow::RefreshFollowHudFromLastMouse() {
    const bool followUiActive = IsCursorFollowUiActiveAt(lastMouse_);
    const bool showGuides = followUiActive && guideLinesEnabled_;
    const bool showMagnifierOverlay = followUiActive && ShouldShowMagnifierOverlay();
    const bool showInfoOverlay = followUiActive && ShouldShowCursorInfoOverlay();
    if (showGuides || showMagnifierOverlay || showInfoOverlay) {
        std::optional<RECT> infoRect;
        std::optional<RECT> magnifierRect;
        RECT verticalGuide{};
        RECT horizontalGuide{};
        const bool hasGuides = showGuides && ComputeCrosshairGuideRects(lastMouse_, verticalGuide, horizontalGuide);
        if (showInfoOverlay) {
            infoRect = ComputeCursorInfoRect(lastMouse_);
        }
        if (showMagnifierOverlay) {
            magnifierRect = ComputeMagnifierRect(lastMouse_);
        }
        lastInfoRect_ = infoRect;
        lastMagnifierRect_ = magnifierRect;
        lastVerticalGuideRect_ = hasGuides ? std::optional<RECT>(verticalGuide) : std::nullopt;
        lastHorizontalGuideRect_ = hasGuides ? std::optional<RECT>(horizontalGuide) : std::nullopt;
        UpdateHudWindows(infoRect, magnifierRect, lastVerticalGuideRect_, lastHorizontalGuideRect_);
    } else {
        lastInfoRect_.reset();
        lastMagnifierRect_.reset();
        lastVerticalGuideRect_.reset();
        lastHorizontalGuideRect_.reset();
        UpdateHudWindows(std::nullopt, std::nullopt, std::nullopt, std::nullopt);
    }
}

void OverlayWindow::ClearFollowHud() {
    lastInfoRect_.reset();
    lastMagnifierRect_.reset();
    lastVerticalGuideRect_.reset();
    lastHorizontalGuideRect_.reset();
    UpdateHudWindows(std::nullopt, std::nullopt, std::nullopt, std::nullopt);
}

void OverlayWindow::RefreshToolbarPlacement(bool forceRedraw) {
    if (stage_ != Stage::Annotating || !HasSelection()) {
        toolbar_.Hide();
        previewBar_.Hide();
        if (previewVideoHostHwnd_) {
            ShowWindow(previewVideoHostHwnd_, SW_HIDE);
        }
        return;
    }
    if (recordingPreviewMode_) {
        toolbar_.Hide();
        RefreshPreviewPlacement();
        if (forceRedraw && previewBar_.Hwnd()) {
            RedrawWindow(previewBar_.Hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_NOERASE);
        }
        return;
    }
    previewBar_.Hide();
    if (previewVideoHostHwnd_) {
        ShowWindow(previewVideoHostHwnd_, SW_HIDE);
    }
    toolbar_.ShowNear(SelectionRectNormalized(), RECT{0, 0, capture_.image.width, capture_.image.height});
    if (forceRedraw && toolbar_.Hwnd()) {
        RedrawWindow(toolbar_.Hwnd(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_NOERASE);
    }
}

void OverlayWindow::DrawMagnifierPanel(Gdiplus::Graphics& g, const RECT& panelRect, POINT centerPt, float dpiScale) const {
    if (!capture_.image.IsValid()) {
        return;
    }
    const int dstW = std::max(1, RectWidth(panelRect));
    const int dstH = std::max(1, RectHeight(panelRect));
    if (dstW <= 2 || dstH <= 2) {
        return;
    }

    const float targetCellPx = std::max(14.0f, 16.0f * dpiScale);
    int srcW = std::max(5, static_cast<int>(std::round(static_cast<float>(dstW) / targetCellPx)));
    int srcH = std::max(5, static_cast<int>(std::round(static_cast<float>(dstH) / targetCellPx)));
    if ((srcW & 1) == 0) {
        ++srcW;
    }
    if ((srcH & 1) == 0) {
        ++srcH;
    }

    RECT srcRc{
        centerPt.x - (srcW / 2),
        centerPt.y - (srcH / 2),
        centerPt.x + (srcW / 2) + 1,
        centerPt.y + (srcH / 2) + 1
    };
    if (srcRc.left < 0) {
        OffsetRect(&srcRc, -srcRc.left, 0);
    }
    if (srcRc.top < 0) {
        OffsetRect(&srcRc, 0, -srcRc.top);
    }
    if (srcRc.right > capture_.image.width) {
        OffsetRect(&srcRc, capture_.image.width - srcRc.right, 0);
    }
    if (srcRc.bottom > capture_.image.height) {
        OffsetRect(&srcRc, 0, capture_.image.height - srcRc.bottom);
    }
    srcRc.left = std::max(0L, srcRc.left);
    srcRc.top = std::max(0L, srcRc.top);
    srcRc.right = std::min(static_cast<LONG>(capture_.image.width), srcRc.right);
    srcRc.bottom = std::min(static_cast<LONG>(capture_.image.height), srcRc.bottom);

    const int srcCols = RectWidth(srcRc);
    const int srcRows = RectHeight(srcRc);
    if (srcCols <= 1 || srcRows <= 1) {
        return;
    }

    const int centerCol = std::clamp(static_cast<int>(centerPt.x) - static_cast<int>(srcRc.left), 0, srcCols - 1);
    const int centerRow = std::clamp(static_cast<int>(centerPt.y) - static_cast<int>(srcRc.top), 0, srcRows - 1);
    auto cellRect = [&](int col, int row) {
        const int x0 = panelRect.left + (col * dstW) / srcCols;
        const int x1 = panelRect.left + ((col + 1) * dstW) / srcCols;
        const int y0 = panelRect.top + (row * dstH) / srcRows;
        const int y1 = panelRect.top + ((row + 1) * dstH) / srcRows;
        return RECT{ x0, y0, x1, y1 };
    };

    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);

    Gdiplus::SolidBrush panelBg(Gdiplus::Color(255, 0, 0, 0));
    g.FillRectangle(&panelBg, ToGdiRect(panelRect));
    if (capture_.image.IsValid() && !capture_.image.bgra.empty()) {
        const uint8_t* srcPixels = capture_.image.bgra.data();
        const size_t strideBytes = static_cast<size_t>(capture_.image.width) * 4;
        for (int row = 0; row < srcRows; ++row) {
            const int sy = static_cast<int>(srcRc.top) + row;
            if (sy < 0 || sy >= capture_.image.height) {
                continue;
            }
            const uint8_t* srcRow = srcPixels + static_cast<size_t>(sy) * strideBytes;
            for (int col = 0; col < srcCols; ++col) {
                const int sx = static_cast<int>(srcRc.left) + col;
                if (sx < 0 || sx >= capture_.image.width) {
                    continue;
                }
                RECT cell = cellRect(col, row);
                const int cw = RectWidth(cell);
                const int ch = RectHeight(cell);
                if (cw <= 0 || ch <= 0) {
                    continue;
                }
                const uint8_t* px = srcRow + static_cast<size_t>(sx) * 4;
                Gdiplus::SolidBrush pixelBrush(Gdiplus::Color(255, px[2], px[1], px[0]));
                g.FillRectangle(&pixelBrush, cell.left, cell.top, cw, ch);
            }
        }
    }

    Gdiplus::Pen gridPen(Gdiplus::Color(48, 150, 150, 150), 1.0f);
    for (int i = 1; i < srcCols; ++i) {
        const float x = static_cast<float>(panelRect.left + (i * dstW) / srcCols);
        g.DrawLine(&gridPen, x, static_cast<float>(panelRect.top), x, static_cast<float>(panelRect.bottom));
    }
    for (int i = 1; i < srcRows; ++i) {
        const float y = static_cast<float>(panelRect.top + (i * dstH) / srcRows);
        g.DrawLine(&gridPen, static_cast<float>(panelRect.left), y, static_cast<float>(panelRect.right), y);
    }

    RECT center = cellRect(centerCol, centerRow);
    Gdiplus::Pen centerBorderOuter(Gdiplus::Color(245, 255, 255, 255), std::max(1.0f, 1.8f * dpiScale));
    Gdiplus::Pen centerBorderInner(Gdiplus::Color(220, 20, 22, 26), 1.0f);
    g.DrawRectangle(&centerBorderOuter, ToGdiRect(center));
    RECT inner = center;
    InflateRect(&inner, -1, -1);
    if (RectWidth(inner) > 0 && RectHeight(inner) > 0) {
        g.DrawRectangle(&centerBorderInner, ToGdiRect(inner));
    }

    Gdiplus::Pen panelBorder(Gdiplus::Color(220, 0, 0, 0), std::max(1.0f, 1.0f * dpiScale));
    const float l = static_cast<float>(panelRect.left);
    const float t = static_cast<float>(panelRect.top);
    const float r = static_cast<float>(panelRect.right - 1);
    const float b = static_cast<float>(panelRect.bottom - 1);
    if (r > l && b > t) {
        g.DrawLine(&panelBorder, l, t, r, t);
        g.DrawLine(&panelBorder, l, b, r, b);
        g.DrawLine(&panelBorder, l, t, l, b);
        g.DrawLine(&panelBorder, r, t, r, b);
    }
}

void OverlayWindow::DrawCursorInfoPanel(Gdiplus::Graphics& g, const RECT& panelRect, POINT centerPt, float dpiScale) const {
    if (!capture_.image.IsValid() || centerPt.x < 0 || centerPt.y < 0 ||
        centerPt.x >= capture_.image.width || centerPt.y >= capture_.image.height) {
        return;
    }

    COLORREF c = capture_.image.GetPixel(centerPt.x, centerPt.y);
    const std::wstring colorText = colorHexMode_ ? ColorToHex(c) : ColorToRgbText(c);
    std::wstring colorLine = (colorHexMode_ ? L"HEX:  " : L"RGB:  ") + colorText;
    const int absX = centerPt.x + capture_.virtualRect.left;
    const int absY = centerPt.y + capture_.virtualRect.top;
    std::wstring coord = L"(" + std::to_wstring(absX) + L", " + std::to_wstring(absY) + L")";

    Gdiplus::Rect panel(panelRect.left, panelRect.top, RectWidth(panelRect), RectHeight(panelRect));
    Gdiplus::SolidBrush panelBg(Gdiplus::Color(228, 8, 8, 10));
    g.FillRectangle(&panelBg, panel);

    Gdiplus::Pen panelBorder(Gdiplus::Color(210, 38, 38, 38), 1.0f);
    g.DrawRectangle(&panelBorder, panel);

    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font titleFont(&ff, std::max(12.0f, 14.0f * dpiScale), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font textFont(&ff, std::max(11.0f, 12.5f * dpiScale), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush white(Gdiplus::Color(245, 245, 245, 245));
    Gdiplus::SolidBrush gray(Gdiplus::Color(220, 205, 205, 205));
    const auto oldTextHint = g.GetTextRenderingHint();
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    const float topPadding = std::max(6.0f, 7.0f * dpiScale);
    const float rowGap = std::max(2.0f, 3.0f * dpiScale);
    const float totalGap = rowGap * 3.0f;
    const float rowH = std::max(16.0f, (static_cast<float>(panel.Height) - topPadding * 2.0f - totalGap) / 4.0f);
    const float panelLeft = static_cast<float>(panel.X);
    const float panelTop = static_cast<float>(panel.Y);
    const float panelWidth = static_cast<float>(panel.Width);

    auto rowRect = [&](int row) {
        const float y = panelTop + topPadding + static_cast<float>(row) * (rowH + rowGap);
        return Gdiplus::RectF(panelLeft, y, panelWidth, rowH);
    };

    Gdiplus::StringFormat centerFmt;
    centerFmt.SetAlignment(Gdiplus::StringAlignmentCenter);
    centerFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::StringFormat leftFmt;
    leftFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    leftFmt.SetAlignment(Gdiplus::StringAlignmentNear);

    g.DrawString(coord.c_str(), -1, &titleFont, rowRect(0), &centerFmt, &white);

    const float swatchSize = std::max(10.0f, 11.0f * dpiScale);
    const float swatchGap = std::max(8.0f, 10.0f * dpiScale);
    const Gdiplus::RectF row2 = rowRect(1);
    const float colorSlotW = std::max(120.0f * dpiScale, row2.Width * 0.50f);
    const float groupW = swatchSize + swatchGap + colorSlotW;
    const float groupStartX = row2.X + std::max(0.0f, (row2.Width - groupW) * 0.5f);
    const float swatchY = row2.Y + (row2.Height - swatchSize) * 0.5f;

    Gdiplus::SolidBrush swatchBrush(Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)));
    g.FillRectangle(&swatchBrush, groupStartX, swatchY, swatchSize, swatchSize);
    Gdiplus::Pen swatchBorder(Gdiplus::Color(235, 255, 255, 255), 1.0f);
    g.DrawRectangle(&swatchBorder, groupStartX, swatchY, swatchSize, swatchSize);

    const float textX = groupStartX + swatchSize + swatchGap;
    Gdiplus::RectF colorTextRect(textX, row2.Y, std::min(colorSlotW, std::max(0.0f, row2.GetRight() - textX)), row2.Height);
    g.DrawString(colorLine.c_str(), -1, &titleFont, colorTextRect, &leftFmt, &white);

    g.DrawString(L"C: \u590D\u5236\u989C\u8272\u503C", -1, &textFont, rowRect(2), &centerFmt, &gray);
    g.DrawString(L"Alt: \u5207\u6362\u989C\u8272\u683C\u5F0F", -1, &textFont, rowRect(3), &centerFmt, &gray);
    g.SetTextRenderingHint(oldTextHint);
}

void OverlayWindow::DrawEditingShortcutHint(Gdiplus::Graphics& g, float dpiScale, const RECT& selectionRect) const {
    const bool selectionTransforming =
        dragMode_ == DragMode::MoveSelection || dragMode_ == DragMode::ResizeSelection;
    if (stage_ != Stage::Annotating || longCaptureMode_ || whiteboardMode_ || screenRecordingMode_ || !HasSelection() || selectionTransforming) {
        return;
    }

    POINT sp{};
    if (!GetCursorPos(&sp)) {
        sp = LocalToScreenPoint(lastMouse_);
    }

    HMONITOR monitor = MonitorFromPoint(sp, MONITOR_DEFAULTTONEAREST);
    if (!monitor) {
        return;
    }

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) {
        return;
    }

    struct ShortcutHintItem {
        const wchar_t* key;
        const wchar_t* desc;
    };
    static constexpr ShortcutHintItem kItems[] = {
        {L"Esc", L"\u9000\u51FA\u622A\u56FE"},
        {L"\u9F20\u6807\u4E2D\u952E", L"\u56FA\u5B9A\u8D34\u56FE"},
        {L"\u9F20\u6807\u53F3\u952E", L"\u91CD\u9009\u622A\u56FE\u533A\u57DF"},
        {L"Tab", L"\u663E\u793A\u002F\u9690\u85CF\u8F85\u52A9\u4FE1\u606F\u6846"},
        {L"\u6309\u4F4F\u0053\u0068\u0069\u0066\u0074", L"\u7F13\u6162\u79FB\u52A8\u5149\u6807"},
        {L"Ctrl+Z", L"\u64A4\u9500"},
        {L"Ctrl+Y", L"\u91CD\u505A"},
        {L"Space", L"\u5B8C\u6210\u622A\u56FE"},
        {L"Ctrl+C", L"\u590D\u5236\u5230\u526A\u8D34\u677F"},
        {L"Ctrl+S", L"\u4FDD\u5B58\u4E3A\u6587\u4EF6"},
    };
    constexpr int kLineCount = static_cast<int>(std::size(kItems));

    const int margin = std::max(10, static_cast<int>(std::lround(12.0f * dpiScale)));
    const int padX = std::max(12, static_cast<int>(std::lround(14.0f * dpiScale)));
    const int padY = std::max(10, static_cast<int>(std::lround(11.0f * dpiScale)));
    const int rowGap = std::max(2, static_cast<int>(std::lround(4.0f * dpiScale)));
    const int rowH = std::max(19, static_cast<int>(std::lround(22.0f * dpiScale)));
    const int keyDescGap = std::max(12, static_cast<int>(std::lround(16.0f * dpiScale)));
    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font font(&ff, std::max(13.0f, 15.5f * dpiScale), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    float maxKeyW = 0.0f;
    float maxDescW = 0.0f;
    for (int i = 0; i < kLineCount; ++i) {
        Gdiplus::RectF keyMeasured{};
        Gdiplus::RectF descMeasured{};
        g.MeasureString(kItems[i].key, -1, &font, Gdiplus::PointF(0, 0), &keyMeasured);
        g.MeasureString(kItems[i].desc, -1, &font, Gdiplus::PointF(0, 0), &descMeasured);
        maxKeyW = std::max(maxKeyW, keyMeasured.Width);
        maxDescW = std::max(maxDescW, descMeasured.Width);
    }
    const int widthSlack = std::max(6, static_cast<int>(std::lround(8.0f * dpiScale)));
    const int keyColW = static_cast<int>(std::ceil(maxKeyW));
    const int descColW = static_cast<int>(std::ceil(maxDescW));
    const int desiredW = padX * 2 + keyColW + keyDescGap + descColW + widthSlack;
    const int minW = std::max(220, static_cast<int>(std::lround(250.0f * dpiScale)));
    const int monitorW = static_cast<int>(mi.rcMonitor.right - mi.rcMonitor.left);
    const int maxW = std::max(minW, monitorW - margin * 2);
    const int boxW = std::clamp(std::max(minW, desiredW), minW, maxW);
    const int boxH = padY * 2 + rowH * kLineCount + rowGap * (kLineCount - 1);

    RECT screenRc{
        mi.rcMonitor.left + margin,
        mi.rcMonitor.bottom - margin - boxH,
        mi.rcMonitor.left + margin + boxW,
        mi.rcMonitor.bottom - margin
    };

    RECT localRc = ScreenToLocalRect(screenRc);
    localRc.left = std::clamp(localRc.left, 0L, std::max(0L, static_cast<LONG>(capture_.image.width - boxW)));
    localRc.top = std::clamp(localRc.top, 0L, std::max(0L, static_cast<LONG>(capture_.image.height - boxH)));
    localRc.right = localRc.left + boxW;
    localRc.bottom = localRc.top + boxH;

    RECT inter{};
    RECT sr = NormalizeRect(selectionRect);
    if (IntersectRect(&inter, &localRc, &sr)) {
        return;
    }

    Gdiplus::Rect panel = ToGdiRect(localRc);
    Gdiplus::SolidBrush bg(Gdiplus::Color(172, 12, 12, 12));
    Gdiplus::Pen border(Gdiplus::Color(210, 70, 70, 70), 1.0f);
    g.FillRectangle(&bg, panel);
    g.DrawRectangle(&border, panel);

    Gdiplus::SolidBrush keyBrush(Gdiplus::Color(248, 248, 248, 248));
    Gdiplus::SolidBrush descBrush(Gdiplus::Color(232, 232, 232, 232));
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(Gdiplus::StringAlignmentNear);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    const auto oldTextHint = g.GetTextRenderingHint();
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    float y = static_cast<float>(localRc.top + padY);
    const float keyX = static_cast<float>(localRc.left + padX);
    const float descX = keyX + static_cast<float>(keyColW + keyDescGap);
    const float keyW = static_cast<float>(keyColW);
    const float descW = std::max(0.0f, static_cast<float>(RectWidth(localRc)) - (descX - static_cast<float>(localRc.left)) - static_cast<float>(padX));
    for (int i = 0; i < kLineCount; ++i) {
        Gdiplus::RectF keyRow(keyX, y, keyW, static_cast<float>(rowH));
        Gdiplus::RectF descRow(descX, y, descW, static_cast<float>(rowH));
        g.DrawString(kItems[i].key, -1, &font, keyRow, &fmt, &keyBrush);
        g.DrawString(kItems[i].desc, -1, &font, descRow, &fmt, &descBrush);
        y += static_cast<float>(rowH + rowGap);
    }
    g.SetTextRenderingHint(oldTextHint);
}

void OverlayWindow::OnHudPaint(HWND hudHwnd) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hudHwnd, &ps);
    RECT client{};
    GetClientRect(hudHwnd, &client);
    if (RectWidth(client) <= 0 || RectHeight(client) <= 0) {
        EndPaint(hudHwnd, &ps);
        return;
    }
    GdiBitmapBuffer* backBuffer = nullptr;
    if (hudHwnd == magnifierHudHwnd_) {
        backBuffer = &magnifierHudBackBuffer_;
    } else if (hudHwnd == infoHudHwnd_) {
        backBuffer = &infoHudBackBuffer_;
    } else if (hudHwnd == verticalGuideHudHwnd_) {
        backBuffer = &verticalGuideHudBackBuffer_;
    } else if (hudHwnd == horizontalGuideHudHwnd_) {
        backBuffer = &horizontalGuideHudBackBuffer_;
    }

    HDC paintDc = hdc;
    HGDIOBJ oldObj = nullptr;
    if (backBuffer && backBuffer->Ensure(hdc, RectWidth(client), RectHeight(client)) &&
        backBuffer->dc() && backBuffer->bitmap()) {
        paintDc = backBuffer->dc();
        oldObj = SelectObject(backBuffer->dc(), backBuffer->bitmap());
    }

    Gdiplus::Graphics g(paintDc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
    g.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
    const float dpiScale = static_cast<float>(GetDpiForWindow(hudHwnd)) / 96.0f;
    RECT panel{ 0, 0, RectWidth(client), RectHeight(client) };
    POINT centerPt = lastMouse_;

    if (hudHwnd == magnifierHudHwnd_) {
        DrawMagnifierPanel(g, panel, centerPt, dpiScale);
    } else if (hudHwnd == infoHudHwnd_) {
        DrawCursorInfoPanel(g, panel, centerPt, dpiScale);
    } else if (hudHwnd == verticalGuideHudHwnd_ || hudHwnd == horizontalGuideHudHwnd_) {
        Gdiplus::SolidBrush guideBrush(Gdiplus::Color(255, 255, 255, 255));
        g.FillRectangle(&guideBrush, 0, 0, RectWidth(panel), RectHeight(panel));
    }

    if (paintDc != hdc) {
        BitBlt(hdc, 0, 0, RectWidth(client), RectHeight(client), paintDc, 0, 0, SRCCOPY);
    }
    if (oldObj && backBuffer && backBuffer->dc()) {
        SelectObject(backBuffer->dc(), oldObj);
    }
    EndPaint(hudHwnd, &ps);
}

void OverlayWindow::DrawCurrentShapePreview(Gdiplus::Graphics& g, const RECT& selectionRect) {
    if (!hasCurrentShape_ || stage_ != Stage::Annotating) {
        return;
    }

    const auto oldSmoothing = g.GetSmoothingMode();
    const auto oldPixelOffset = g.GetPixelOffsetMode();
    auto gr = ToGdiRect(selectionRect);
    Gdiplus::GraphicsState st = g.Save();
    g.SetClip(gr, Gdiplus::CombineModeIntersect);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    const auto& s = currentShape_;
    Gdiplus::Color previewColor(220, GetRValue(s.color), GetGValue(s.color), GetBValue(s.color));
    Gdiplus::Pen pen(previewColor, s.stroke);
    pen.SetDashStyle(Gdiplus::DashStyleDash);
    RECT rr = NormalizeRect(s.rect);
    if (s.type == ToolType::Rect) {
        if (s.fillEnabled) {
            Gdiplus::SolidBrush fill(Gdiplus::Color(255, GetRValue(s.fillColor), GetGValue(s.fillColor), GetBValue(s.fillColor)));
            g.FillRectangle(&fill, ToGdiRect(rr));
        }
        g.DrawRectangle(&pen, ToGdiRect(rr));
    } else if (s.type == ToolType::Mosaic) {
        g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
        DrawMosaicRegion(g, captureBitmap_.get(), rr, std::max(4, static_cast<int>(s.stroke * 3.0f)));
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        // 浣跨敤鍥哄畾鐨勭瑪瀹藉害缁樺埗杈规锛屼笉鍙楅┈璧涘厠澶у皬(stroke)鐨勫奖鍝?
        Gdiplus::Pen mosaicBorderPen(previewColor, 2.0f);
        mosaicBorderPen.SetDashStyle(Gdiplus::DashStyleDash);
        g.DrawRectangle(&mosaicBorderPen, ToGdiRect(rr));
    } else if (s.type == ToolType::Ellipse) {
        if (s.fillEnabled) {
            Gdiplus::SolidBrush fill(Gdiplus::Color(255, GetRValue(s.fillColor), GetGValue(s.fillColor), GetBValue(s.fillColor)));
            g.FillEllipse(&fill, ToGdiRect(rr));
        }
        g.DrawEllipse(&pen, ToGdiRect(rr));
    } else if ((s.type == ToolType::Line || s.type == ToolType::Arrow) && s.points.size() >= 2) {
        auto p1 = Gdiplus::PointF(static_cast<float>(s.points[0].x), static_cast<float>(s.points[0].y));
        auto p2 = Gdiplus::PointF(static_cast<float>(s.points[1].x), static_cast<float>(s.points[1].y));
        if (s.type == ToolType::Arrow) {
            DrawArrowWithHeadSpacing(g, pen, p1, p2, previewColor, s.stroke);
        } else {
            g.DrawLine(&pen, p1, p2);
        }
    } else if (s.type == ToolType::Pen && s.points.size() >= 2) {
        std::vector<Gdiplus::Point> pts;
        pts.reserve(s.points.size());
        for (const auto& p : s.points) {
            pts.emplace_back(p.x, p.y);
        }
        pen.SetDashStyle(Gdiplus::DashStyleSolid);
        if (pts.size() >= 3) {
            g.DrawCurve(&pen, pts.data(), static_cast<INT>(pts.size()), 0.5f);
        } else {
            g.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
        }
    }

    g.Restore(st);
    g.SetSmoothingMode(oldSmoothing);
    g.SetPixelOffsetMode(oldPixelOffset);
}

void OverlayWindow::DrawCommittedShapesInSelection(Gdiplus::Graphics& g, const RECT& selectionRect, float dpiScale, bool selectionDragging) const {
    if (shapes_.empty()) {
        return;
    }

    const auto gr = ToGdiRect(selectionRect);
    const auto oldSmoothing = g.GetSmoothingMode();
    const auto oldPixelOffset = g.GetPixelOffsetMode();
    const auto oldTextHint = g.GetTextRenderingHint();
    g.SetSmoothingMode(selectionDragging ? Gdiplus::SmoothingModeHighSpeed : Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(selectionDragging ? Gdiplus::PixelOffsetModeNone : Gdiplus::PixelOffsetModeHalf);

    Gdiplus::GraphicsState st = g.Save();
    g.SetClip(gr, Gdiplus::CombineModeIntersect);

    for (size_t i = 0; i < shapes_.size(); ++i) {
        const auto& s = shapes_[i];
        RECT sbounds = ShapeBounds(s);
        RECT inter{};
        if (!IntersectRect(&inter, &sbounds, &selectionRect)) {
            continue;
        }

        switch (s.type) {
        case ToolType::Mosaic:
        {
            const RECT rr = NormalizeRect(s.rect);
            g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
            DrawMosaicRegion(g, captureBitmap_.get(), rr, std::max(4, static_cast<int>(s.stroke * 3.0f)));
            g.SetSmoothingMode(selectionDragging ? Gdiplus::SmoothingModeHighSpeed : Gdiplus::SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(selectionDragging ? Gdiplus::PixelOffsetModeNone : Gdiplus::PixelOffsetModeHalf);
            break;
        }
        default:
            AnnotationRenderer::DrawShape(g, s);
            break;
        }

        if (static_cast<int>(i) == selectedShape_) {
            RECT sb = ShapeBounds(s);
            Gdiplus::Pen selPen(Gdiplus::Color(255, 0, 150, 255), 1.0f);
            selPen.SetDashStyle(Gdiplus::DashStyleDash);
            g.DrawRectangle(&selPen, ToGdiRect(sb));
            DrawHandle(g, sb.left, sb.top, dpiScale);
            DrawHandle(g, sb.right, sb.top, dpiScale);
            DrawHandle(g, sb.left, sb.bottom, dpiScale);
            DrawHandle(g, sb.right, sb.bottom, dpiScale);
        }
    }

    g.Restore(st);
    g.SetSmoothingMode(oldSmoothing);
    g.SetPixelOffsetMode(oldPixelOffset);
    g.SetTextRenderingHint(oldTextHint);
}

void OverlayWindow::DrawWhiteboardLayer(Gdiplus::Graphics& g, float dpiScale) {
    if (!whiteboardMode_ || !HasSelection()) {
        return;
    }

    const RECT sr = SelectionRectNormalized();
    const auto gr = ToGdiRect(sr);

    Gdiplus::SolidBrush whiteBrush(Gdiplus::Color(255, 255, 255, 255));
    g.FillRectangle(&whiteBrush, gr);

    Gdiplus::Pen border(Gdiplus::Color(255, 0, 204, 255), 1.5f);
    border.SetAlignment(Gdiplus::PenAlignmentInset);
    g.DrawRectangle(&border, gr);

    if (stage_ != Stage::Annotating) {
        return;
    }

    const bool selectionDragging =
        (dragMode_ == DragMode::SelectingNew || dragMode_ == DragMode::MoveSelection || dragMode_ == DragMode::ResizeSelection);
    DrawCommittedShapesInSelection(g, sr, dpiScale, selectionDragging);
}

void OverlayWindow::RenderStaticScene(Gdiplus::Graphics& g, float dpiScale) {
    if (captureBitmap_) {
        g.DrawImage(captureBitmap_.get(), 0, 0, capture_.image.width, capture_.image.height);
    }

    if (!longCaptureMode_) {
        const BYTE maskAlpha = (stage_ == Stage::Selecting && !HasSelection()) ? 90 : 140;
        Gdiplus::SolidBrush darkMask(Gdiplus::Color(maskAlpha, 0, 0, 0));
        g.FillRectangle(&darkMask, 0, 0, capture_.image.width, capture_.image.height);
    }

    if (HasSelection()) {
        RECT sr = SelectionRectNormalized();
        auto gr = ToGdiRect(sr);
        if (whiteboardMode_) {
            // Whiteboard layer is drawn dynamically in OnPaint to keep drag smooth.
        } else {
            if (longCaptureMode_ && longCaptureLastFrame_.IsValid()) {
                Gdiplus::Bitmap liveFrame(longCaptureLastFrame_.width, longCaptureLastFrame_.height,
                    longCaptureLastFrame_.width * 4, PixelFormat32bppARGB,
                    const_cast<BYTE*>(longCaptureLastFrame_.bgra.data()));
                g.DrawImage(&liveFrame, gr, 0, 0, longCaptureLastFrame_.width, longCaptureLastFrame_.height, Gdiplus::UnitPixel);
            } else if (captureBitmap_) {
                g.DrawImage(captureBitmap_.get(), gr, sr.left, sr.top, RectWidth(sr), RectHeight(sr), Gdiplus::UnitPixel);
            } else {
                Gdiplus::SolidBrush fallbackBrush(Gdiplus::Color(255, 255, 255, 255));
                g.FillRectangle(&fallbackBrush, gr);
            }

            if (longCaptureMode_) {
                Gdiplus::Pen border(Gdiplus::Color(255, 232, 70, 70), std::max(1.6f, 2.0f * dpiScale));
                border.SetAlignment(Gdiplus::PenAlignmentInset);
                g.DrawRectangle(&border, gr);
            } else {
                Gdiplus::Pen border(Gdiplus::Color(255, 0, 204, 255), 1.5f);
                border.SetAlignment(Gdiplus::PenAlignmentInset);
                g.DrawRectangle(&border, gr);

                DrawHandle(g, sr.left, sr.top, dpiScale);
                DrawHandle(g, sr.right, sr.top, dpiScale);
                DrawHandle(g, sr.left, sr.bottom, dpiScale);
                DrawHandle(g, sr.right, sr.bottom, dpiScale);
                DrawHandle(g, (sr.left + sr.right) / 2, sr.top, dpiScale);
                DrawHandle(g, (sr.left + sr.right) / 2, sr.bottom, dpiScale);
                DrawHandle(g, sr.left, (sr.top + sr.bottom) / 2, dpiScale);
                DrawHandle(g, sr.right, (sr.top + sr.bottom) / 2, dpiScale);
            }
        }

        if (stage_ == Stage::Annotating && !longCaptureMode_ && !whiteboardMode_) {
            DrawCommittedShapesInSelection(g, sr, dpiScale, false);
        }

        if (!longCaptureMode_ && !whiteboardMode_) {
            DrawSelectionSizeHint(g, sr, capture_.virtualRect, hwnd_);
        }
    }

    if (stage_ == Stage::Selecting && hoverWindowRect_.has_value() && !HasSelection()) {
        RECT hr = NormalizeRect(*hoverWindowRect_);
        if (RectWidth(hr) >= 2 && RectHeight(hr) >= 2) {
            Gdiplus::Pen borderPen(Gdiplus::Color(255, 255, 190, 0), std::max(1.2f, 2.0f * dpiScale));
            borderPen.SetDashStyle(Gdiplus::DashStyleDashDot);
            g.DrawRectangle(&borderPen, ToGdiRect(hr));
        }
    }
}

void OverlayWindow::OnPaint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT client{};
    GetClientRect(hwnd_, &client);
    RECT paintRect = ps.rcPaint;
    if (RectWidth(paintRect) <= 0 || RectHeight(paintRect) <= 0) {
        paintRect = client;
    }

    const int width = std::max(1, RectWidth(paintRect));
    const int height = std::max(1, RectHeight(paintRect));
    HDC memDc = nullptr;
    HGDIOBJ oldObj = nullptr;
    if (paintBuffer_.Ensure(hdc, width, height) && paintBuffer_.dc() && paintBuffer_.bitmap()) {
        memDc = paintBuffer_.dc();
        oldObj = SelectObject(memDc, paintBuffer_.bitmap());
        SetViewportOrgEx(memDc, -paintRect.left, -paintRect.top, nullptr);
    }

    HDC paintDc = memDc ? memDc : hdc;
    if (paintDc == memDc) {
        const COLORREF bgColor = (longCaptureMode_ || screenRecordingMode_ || recordingPreviewMode_) ? kLongCaptureColorKey : RGB(0, 0, 0);
        HBRUSH bg = CreateSolidBrush(bgColor);
        FillRect(paintDc, &paintRect, bg);
        DeleteObject(bg);
    }

    Gdiplus::Graphics g(paintDc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighSpeed);
    g.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.SetInterpolationMode(Gdiplus::InterpolationModeLowQuality);
    const float dpiScale = static_cast<float>(GetDpiForWindow(hwnd_) ? GetDpiForWindow(hwnd_) : 96) / 96.0f;

    if (longCaptureMode_) {
        if (paintDc != memDc) {
            HBRUSH bg = CreateSolidBrush(kLongCaptureColorKey);
            FillRect(paintDc, &paintRect, bg);
            DeleteObject(bg);
        }

        lastInfoRect_.reset();
        lastMagnifierRect_.reset();
        lastVerticalGuideRect_.reset();
        lastHorizontalGuideRect_.reset();
        UpdateHudWindows(std::nullopt, std::nullopt, std::nullopt, std::nullopt);

        if (HasSelection()) {
            RECT sr = SelectionRectNormalized();
            RECT borderRc = sr;
            const int borderPad = std::max(2, static_cast<int>(std::ceil(2.0f * dpiScale)));
            InflateRect(&borderRc, borderPad, borderPad);
            Gdiplus::Pen border(Gdiplus::Color(255, 232, 70, 70), std::max(1.6f, 2.0f * dpiScale));
            border.SetAlignment(Gdiplus::PenAlignmentInset);
            g.DrawRectangle(&border, ToGdiRect(borderRc));
        }
        DrawLongCaptureThumbnail(g, dpiScale);

        if (paintDc == memDc) {
            BitBlt(hdc,
                paintRect.left, paintRect.top, RectWidth(paintRect), RectHeight(paintRect),
                memDc, 0, 0, SRCCOPY);
            SetViewportOrgEx(memDc, 0, 0, nullptr);
        }
        if (oldObj) {
            SelectObject(memDc, oldObj);
        }
        EndPaint(hwnd_, &ps);
        return;
    }

    if (recordingPreviewMode_) {
        ClearFollowHud();
        if (paintDc != memDc) {
            HBRUSH bg = CreateSolidBrush(kLongCaptureColorKey);
            FillRect(paintDc, &paintRect, bg);
            DeleteObject(bg);
        }
        if (HasSelection()) {
            RECT sr = SelectionRectNormalized();
            Gdiplus::Pen border(Gdiplus::Color(255, 95, 170, 240), std::max(1.8f, 2.2f * dpiScale));
            border.SetAlignment(Gdiplus::PenAlignmentInset);
            g.DrawRectangle(&border, ToGdiRect(sr));
        }

        if (paintDc == memDc) {
            BitBlt(hdc,
                paintRect.left, paintRect.top, RectWidth(paintRect), RectHeight(paintRect),
                memDc, 0, 0, SRCCOPY);
            SetViewportOrgEx(memDc, 0, 0, nullptr);
        }
        if (oldObj) {
            SelectObject(memDc, oldObj);
        }
        EndPaint(hwnd_, &ps);
        return;
    }

    if (screenRecordingMode_) {
        if (paintDc != memDc) {
            HBRUSH bg = CreateSolidBrush(kLongCaptureColorKey);
            FillRect(paintDc, &paintRect, bg);
            DeleteObject(bg);
        }

        ClearFollowHud();
        if (HasSelection()) {
            RECT sr = SelectionRectNormalized();
            const Gdiplus::Color borderColor = recordingPaused_
                ? Gdiplus::Color(255, 255, 214, 10)
                : (recordingActive_ ? Gdiplus::Color(255, 232, 70, 70) : Gdiplus::Color(255, 0, 204, 255));
            Gdiplus::Pen outerBorder(borderColor, std::max(1.8f, 2.2f * dpiScale));
            outerBorder.SetAlignment(Gdiplus::PenAlignmentInset);
            g.DrawRectangle(&outerBorder, ToGdiRect(sr));
        }

        if (paintDc == memDc) {
            BitBlt(hdc,
                paintRect.left, paintRect.top, RectWidth(paintRect), RectHeight(paintRect),
                memDc, 0, 0, SRCCOPY);
            SetViewportOrgEx(memDc, 0, 0, nullptr);
        }
        if (oldObj) {
            SelectObject(memDc, oldObj);
        }
        EndPaint(hwnd_, &ps);
        return;
    }

    const bool fastSelectionDrag =
        !whiteboardMode_ &&
        (dragMode_ == DragMode::SelectingNew || dragMode_ == DragMode::MoveSelection || dragMode_ == DragMode::ResizeSelection);

    if (fastSelectionDrag) {
        const Gdiplus::Rect paintDst(paintRect.left, paintRect.top, RectWidth(paintRect), RectHeight(paintRect));
        if (captureBitmap_) {
            g.DrawImage(captureBitmap_.get(), paintDst, paintRect.left, paintRect.top, RectWidth(paintRect), RectHeight(paintRect), Gdiplus::UnitPixel);
        }
        const BYTE dragMaskAlpha = (stage_ == Stage::Selecting) ? 90 : 140;
        Gdiplus::SolidBrush darkMask(Gdiplus::Color(dragMaskAlpha, 0, 0, 0));
        g.FillRectangle(&darkMask, paintDst);

        if (HasSelection()) {
            RECT sr = SelectionRectNormalized();
            if (captureBitmap_) {
                RECT inter{};
                if (IntersectRect(&inter, &sr, &paintRect)) {
                    auto gr = ToGdiRect(inter);
                    g.DrawImage(captureBitmap_.get(), gr, inter.left, inter.top, RectWidth(inter), RectHeight(inter), Gdiplus::UnitPixel);
                }
            }
            Gdiplus::Pen border(Gdiplus::Color(255, 0, 204, 255), 1.5f);
            border.SetAlignment(Gdiplus::PenAlignmentInset);
            g.DrawRectangle(&border, ToGdiRect(sr));

            if (stage_ == Stage::Annotating) {
                DrawCommittedShapesInSelection(g, sr, dpiScale, true);
            }
            DrawSelectionSizeHint(g, sr, capture_.virtualRect, hwnd_);
        }
    } else {
        EnsureStaticSceneBitmap();

        if (staticSceneBitmap_) {
            g.DrawImage(staticSceneBitmap_.get(),
                Gdiplus::Rect(paintRect.left, paintRect.top, RectWidth(paintRect), RectHeight(paintRect)),
                paintRect.left, paintRect.top, RectWidth(paintRect), RectHeight(paintRect),
                Gdiplus::UnitPixel);
        } else {
            RenderStaticScene(g, dpiScale);
        }

        if (stage_ == Stage::Annotating && HasSelection() && !whiteboardMode_) {
            DrawCurrentShapePreview(g, SelectionRectNormalized());
        }
    }

    if (whiteboardMode_ && HasSelection()) {
        DrawWhiteboardLayer(g, dpiScale);
        if (stage_ == Stage::Annotating) {
            DrawCurrentShapePreview(g, SelectionRectNormalized());
        }
    }

    POINT mp = lastMouse_;
    RECT sr = SelectionRectNormalized();
    if (stage_ == Stage::Annotating && HasSelection() && !longCaptureMode_ && !whiteboardMode_) {
        DrawEditingShortcutHint(g, dpiScale, sr);
    }
    const bool penDrawing = (dragMode_ == DragMode::DrawShape && hasCurrentShape_ && currentShape_.type == ToolType::Pen);
    const bool showCrosshair = IsCursorFollowUiActiveAt(mp);
    const bool followUiVisible = showCrosshair && dragMode_ == DragMode::None && !penDrawing;
    const bool showGuides = followUiVisible && guideLinesEnabled_;
    const bool showMagnifierOverlay = followUiVisible && ShouldShowMagnifierOverlay();
    const bool showInfoOverlay = followUiVisible && ShouldShowCursorInfoOverlay();
    std::optional<RECT> infoRect;
    std::optional<RECT> magnifierRect;
    std::optional<RECT> verticalGuideRect;
    std::optional<RECT> horizontalGuideRect;

    if (showGuides) {
        RECT verticalGuide{};
        RECT horizontalGuide{};
        if (ComputeCrosshairGuideRects(mp, verticalGuide, horizontalGuide)) {
            verticalGuideRect = verticalGuide;
            horizontalGuideRect = horizontalGuide;
        }
        lastVerticalGuideRect_ = verticalGuideRect;
        lastHorizontalGuideRect_ = horizontalGuideRect;
    } else {
        const bool hadGuides = lastVerticalGuideRect_.has_value() || lastHorizontalGuideRect_.has_value();
        lastVerticalGuideRect_.reset();
        lastHorizontalGuideRect_.reset();
        if (hadGuides) {
            UpdateHudWindows(lastInfoRect_, lastMagnifierRect_, std::nullopt, std::nullopt);
        }
    }

    if (showInfoOverlay) {
        infoRect = ComputeCursorInfoRect(mp);
        lastInfoRect_ = infoRect;
    } else {
        const bool hadInfo = lastInfoRect_.has_value();
        lastInfoRect_.reset();
        if (hadInfo) {
            UpdateHudWindows(std::nullopt, lastMagnifierRect_, lastVerticalGuideRect_, lastHorizontalGuideRect_);
        }
    }

    if (showMagnifierOverlay) {
        magnifierRect = ComputeMagnifierRect(mp);
        lastMagnifierRect_ = magnifierRect;
    } else {
        const bool hadMagnifier = lastMagnifierRect_.has_value();
        lastMagnifierRect_.reset();
        if (hadMagnifier) {
            UpdateHudWindows(lastInfoRect_, std::nullopt, lastVerticalGuideRect_, lastHorizontalGuideRect_);
        }
    }

    longCaptureThumbRect_.reset();

    if (paintDc == memDc) {
        BitBlt(hdc,
            paintRect.left, paintRect.top, RectWidth(paintRect), RectHeight(paintRect),
            memDc, 0, 0, SRCCOPY);
        SetViewportOrgEx(memDc, 0, 0, nullptr);
    }
    if (oldObj) {
        SelectObject(memDc, oldObj);
    }
    EndPaint(hwnd_, &ps);
}

void OverlayWindow::OnMouseDown(POINT p) {
    if (longCaptureMode_) {
        return;
    }
    if (screenRecordingMode_ && (recordingActive_ || recordingStartPending_)) {
        return;
    }
    SetCapture(hwnd_);
    dragStart_ = p;
    lastMouse_ = p;
    precisionModeActive_ = false;
    precisionBounds_ = RECT{};
    precisionLastRaw_ = p;
    precisionMouseX_ = static_cast<double>(p.x);
    precisionMouseY_ = static_cast<double>(p.y);
    if (recordingPreviewMode_) {
        ClearFollowHud();
    }
    initialSelection_ = selection_;

    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const float dpiScale = DpiScaleForLocalPoint(p);
    if (stage_ == Stage::Annotating && HasSelection() && !screenRecordingMode_ && !recordingPreviewMode_) {
        cursorInfoEnabled_ = true;
    }

    if (stage_ == Stage::Selecting) {
        if (HasSelection()) {
            const RECT sr = SelectionRectNormalized();
            HitKind hit = HitTestRectHandles(sr, p, kHandleSize + 3);
            if (hit != HitKind::None) {
                activeHit_ = hit;
                dragMode_ = (hit == HitKind::Inside) ? DragMode::MoveSelection : DragMode::ResizeSelection;
                UpdateCursorVisual(p);
                return;
            }
        }

        selection_ = RECT{p.x, p.y, p.x, p.y};
        dragMode_ = DragMode::SelectingNew;
        activeHit_ = HitKind::RightBottom;
        UpdateCursorVisual(p);
        return;
    }

    if (stage_ == Stage::Annotating && HasSelection()) {
        const RECT sr = SelectionRectNormalized();
        HitKind hit = HitTestRectHandles(sr, p, kHandleSize + 3);
        if (!whiteboardMode_ && hit != HitKind::None && hit != HitKind::Inside) {
            activeHit_ = hit;
            dragMode_ = DragMode::ResizeSelection;
            toolbar_.Hide();
            InvalidateRect(hwnd_, nullptr, FALSE);
            UpdateCursorVisual(p);
            return;
        }

        if (hit == HitKind::Inside) {
            const int borderGrab = std::max(6, static_cast<int>(std::round(kSelectionBorderGrab * dpiScale)));
            const bool shouldMove = screenRecordingMode_
                ? IsNearRectBorder(sr, p, borderGrab)
                : (recordingPreviewMode_ ? true : (ctrl || IsNearRectBorder(sr, p, borderGrab)));
            if (shouldMove) {
                activeHit_ = HitKind::Inside;
                dragMode_ = DragMode::MoveSelection;
                toolbar_.Hide();
                InvalidateRect(hwnd_, nullptr, FALSE);
                UpdateCursorVisual(p);
                return;
            }
        }
    }

    if (recordingPreviewMode_) {
        if (dragMode_ == DragMode::MoveSelection || dragMode_ == DragMode::ResizeSelection) {
            return;
        }
        ReleaseCapture();
        dragMode_ = DragMode::None;
        return;
    }

    if (screenRecordingMode_) {
        ReleaseCapture();
        dragMode_ = DragMode::None;
        return;
    }

    if (tool_ == ToolType::Eraser) {
        HitKind hit = HitKind::None;
        int idx = HitTestShape(p, &hit);
        if (idx >= 0) {
            PushUndo();
            shapes_.erase(shapes_.begin() + idx);
            selectedShape_ = -1;
            MarkSceneDirty();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }

    if (ctrl) {
        HitKind hit = HitKind::None;
        int idx = HitTestShape(p, &hit);
        if (idx >= 0) {
            selectedShape_ = idx;
            activeShapeHit_ = hit;
            dragMode_ = (hit == HitKind::Inside) ? DragMode::MoveShape : DragMode::ResizeShape;
            selectedShapeBounds_ = ShapeBounds(shapes_[idx]);
            PushUndo();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }

    if (!HasSelection()) {
        return;
    }

    RECT sr = SelectionRectNormalized();
    if (!PtInRect(&sr, p)) {
        return;
    }

    if (tool_ == ToolType::Text) {
        ReleaseCapture();
        dragMode_ = DragMode::None;
        BeginTextEdit(p);
        return;
    }

    if (tool_ == ToolType::Number) {
        PushUndo();
        AnnotationShape s;
        s.type = ToolType::Number;
        s.color = currentColor_;
        s.stroke = strokeWidth_;
        s.rect = RECT{p.x, p.y, p.x, p.y};
        s.number = nextNumber_++;
        shapes_.push_back(std::move(s));
        selectedShape_ = static_cast<int>(shapes_.size()) - 1;
        MarkSceneDirty();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (tool_ == ToolType::None) {
        return;
    }

    hasCurrentShape_ = true;
    currentShape_ = {};
    currentShape_.type = tool_;
    currentShape_.color = currentColor_;
    currentShape_.stroke = strokeWidth_;
    currentShape_.fillEnabled = fillEnabled_ && (tool_ == ToolType::Rect || tool_ == ToolType::Ellipse);
    currentShape_.fillColor = fillColor_;
    currentShape_.textSize = textSize_;
    currentShape_.textStyle = textStyle_;
    currentShape_.rect = RECT{p.x, p.y, p.x, p.y};
    currentShape_.points.push_back(p);
    currentShape_.points.push_back(p);
    dragMode_ = DragMode::DrawShape;
    if (currentShape_.type == ToolType::Pen) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    PushUndo();
}

void OverlayWindow::OnMouseMove(POINT p, WPARAM keys) {
    if (longCaptureMode_) {
        return;
    }
    POINT rawPoint = p;
    auto capturePrecisionBounds = [&]() -> RECT {
        if (HasSelection()) {
            RECT sr = SelectionRectNormalized();
            if (RectWidth(sr) > 1 && RectHeight(sr) > 1) {
                return sr;
            }
        }
        return RECT{ 0, 0, capture_.image.width, capture_.image.height };
    };

    const bool shiftPrecision = ((GetKeyState(VK_SHIFT) & 0x8000) != 0) &&
        dragMode_ == DragMode::None &&
        (keys & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) == 0;
    if (shiftPrecision && stage_ == Stage::Annotating && HasSelection()) {
        if (!toolbarHiddenByShiftPrecision_ && toolbar_.Hwnd() && IsWindowVisible(toolbar_.Hwnd())) {
            toolbar_.Hide();
            toolbarHiddenByShiftPrecision_ = true;
        }
    } else if (toolbarHiddenByShiftPrecision_) {
        if (stage_ == Stage::Annotating && HasSelection()) {
            RefreshToolbarPlacement();
        }
        toolbarHiddenByShiftPrecision_ = false;
    }
    if (shiftPrecision) {
        precisionBounds_ = capturePrecisionBounds();
        if (!precisionModeActive_) {
            precisionModeActive_ = true;
            precisionLastRaw_ = rawPoint;
            precisionMouseX_ = static_cast<double>(lastMouse_.x);
            precisionMouseY_ = static_cast<double>(lastMouse_.y);
            if (precisionMouseX_ < static_cast<double>(precisionBounds_.left) || precisionMouseY_ < static_cast<double>(precisionBounds_.top) ||
                precisionMouseX_ >= static_cast<double>(precisionBounds_.right) ||
                precisionMouseY_ >= static_cast<double>(precisionBounds_.bottom)) {
                precisionMouseX_ = static_cast<double>(rawPoint.x);
                precisionMouseY_ = static_cast<double>(rawPoint.y);
            }
            const double minX = static_cast<double>(precisionBounds_.left);
            const double maxX = static_cast<double>(std::max(precisionBounds_.left, precisionBounds_.right - 1));
            const double minY = static_cast<double>(precisionBounds_.top);
            const double maxY = static_cast<double>(std::max(precisionBounds_.top, precisionBounds_.bottom - 1));
            precisionMouseX_ = std::clamp(precisionMouseX_, minX, maxX);
            precisionMouseY_ = std::clamp(precisionMouseY_, minY, maxY);
        } else {
            constexpr double kPrecisionFactor = 0.16;
            const int rawDx = std::clamp(static_cast<int>(rawPoint.x - precisionLastRaw_.x), -20, 20);
            const int rawDy = std::clamp(static_cast<int>(rawPoint.y - precisionLastRaw_.y), -20, 20);
            precisionMouseX_ += static_cast<double>(rawDx) * kPrecisionFactor;
            precisionMouseY_ += static_cast<double>(rawDy) * kPrecisionFactor;
        }
        p.x = static_cast<LONG>(std::lround(precisionMouseX_));
        p.y = static_cast<LONG>(std::lround(precisionMouseY_));
        p.x = std::clamp(p.x, precisionBounds_.left, std::max(precisionBounds_.left, precisionBounds_.right - 1));
        p.y = std::clamp(p.y, precisionBounds_.top, std::max(precisionBounds_.top, precisionBounds_.bottom - 1));
        bool recentered = false;
        POINT cursorScreen{};
        if (GetCursorPos(&cursorScreen)) {
            HMONITOR monitor = nullptr;
            if (HasSelection()) {
                RECT sr = SelectionRectNormalized();
                POINT selectionCenterLocal{
                    (sr.left + sr.right) / 2,
                    (sr.top + sr.bottom) / 2
                };
                POINT selectionCenterScreen = LocalToScreenPoint(selectionCenterLocal);
                monitor = MonitorFromPoint(selectionCenterScreen, MONITOR_DEFAULTTONEAREST);
            }
            if (!monitor) {
                monitor = MonitorFromPoint(cursorScreen, MONITOR_DEFAULTTONEAREST);
            }
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (monitor && GetMonitorInfoW(monitor, &mi)) {
                POINT centerScreen{
                    (mi.rcMonitor.left + mi.rcMonitor.right) / 2,
                    (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2
                };
                SetCursorPos(centerScreen.x, centerScreen.y);
                precisionLastRaw_ = ScreenToLocalPoint(centerScreen);
                recentered = true;
            }
        }
        if (!recentered) {
            precisionLastRaw_ = rawPoint;
        }
    } else {
        precisionModeActive_ = false;
        precisionBounds_ = RECT{};
        precisionLastRaw_ = rawPoint;
        precisionMouseX_ = static_cast<double>(rawPoint.x);
        precisionMouseY_ = static_cast<double>(rawPoint.y);
    }

    if (dragMode_ == DragMode::None && shiftPrecision) {
        RECT followBounds = HasSelection() ? SelectionRectNormalized() : RECT{0, 0, capture_.image.width, capture_.image.height};
        if (RectWidth(followBounds) > 0 && RectHeight(followBounds) > 0) {
            p.x = std::clamp(p.x, followBounds.left, std::max(followBounds.left, followBounds.right - 1));
            p.y = std::clamp(p.y, followBounds.top, std::max(followBounds.top, followBounds.bottom - 1));
            const double minX = static_cast<double>(followBounds.left);
            const double maxX = static_cast<double>(std::max(followBounds.left, followBounds.right - 1));
            const double minY = static_cast<double>(followBounds.top);
            const double maxY = static_cast<double>(std::max(followBounds.top, followBounds.bottom - 1));
            precisionMouseX_ = std::clamp(precisionMouseX_, minX, maxX);
            precisionMouseY_ = std::clamp(precisionMouseY_, minY, maxY);
        }
    }

    const RECT oldSelection = selection_;
    lastMouse_ = p;
    const POINT virtualPoint = lastMouse_;

    if (stage_ == Stage::Selecting && dragMode_ == DragMode::None) {
        auto rectEqual = [](const std::optional<RECT>& a, const std::optional<RECT>& b) {
            if (a.has_value() != b.has_value()) {
                return false;
            }
            if (!a.has_value()) {
                return true;
            }
            return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom;
        };
        auto oldHover = hoverWindowRect_;
        POINT sp = LocalToScreenPoint(virtualPoint);
        UpdateHoverWindow(sp);
        UpdateCursorVisual(virtualPoint);
        if (!rectEqual(oldHover, hoverWindowRect_)) {
            MarkSceneDirty();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    if (stage_ == Stage::Annotating && dragMode_ == DragMode::None) {
        UpdateCursorVisual(virtualPoint);
        if (recordingPreviewMode_) {
            ClearFollowHud();
            return;
        }
        if (IsCursorFollowUiActiveAt(virtualPoint)) {
            std::optional<RECT> infoRect;
            std::optional<RECT> magnifierRect;
            RECT verticalGuide{};
            RECT horizontalGuide{};
            const bool hasGuides = guideLinesEnabled_ && ComputeCrosshairGuideRects(virtualPoint, verticalGuide, horizontalGuide);
            if (ShouldShowCursorInfoOverlay()) {
                infoRect = ComputeCursorInfoRect(virtualPoint);
            }
            if (ShouldShowMagnifierOverlay()) {
                magnifierRect = ComputeMagnifierRect(virtualPoint);
            }
            lastInfoRect_ = infoRect;
            lastMagnifierRect_ = magnifierRect;
            lastVerticalGuideRect_ = hasGuides ? std::optional<RECT>(verticalGuide) : std::nullopt;
            lastHorizontalGuideRect_ = hasGuides ? std::optional<RECT>(horizontalGuide) : std::nullopt;
            UpdateHudWindows(infoRect, magnifierRect, lastVerticalGuideRect_, lastHorizontalGuideRect_);
        } else {
            lastInfoRect_.reset();
            lastMagnifierRect_.reset();
            lastVerticalGuideRect_.reset();
            lastHorizontalGuideRect_.reset();
            UpdateHudWindows(std::nullopt, std::nullopt, std::nullopt, std::nullopt);
        }
        return;
    }

    if ((keys & MK_LBUTTON) == 0 && dragMode_ != DragMode::None) {
        const bool restoreToolbar =
            (dragMode_ == DragMode::MoveSelection || dragMode_ == DragMode::ResizeSelection || dragMode_ == DragMode::SelectingNew) &&
            stage_ == Stage::Annotating && HasSelection();
        dragMode_ = DragMode::None;
        ReleaseCapture();
        if (restoreToolbar) {
            RefreshToolbarPlacement();
        }
        UpdateCursorVisual(p);
        return;
    }

    const int dx = p.x - dragStart_.x;
    const int dy = p.y - dragStart_.y;
    const bool shift = (keys & MK_SHIFT) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const float dpiScale = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
   auto invalidateSelectionDelta = [&](const RECT& before, const RECT& after) {
        bool hasDirty = false;
        RECT dirty{};
        auto appendRect = [&](RECT rc) {
            rc = NormalizeRect(rc);
            if (RectWidth(rc) <= 0 || RectHeight(rc) <= 0) {
                return;
            }
            if (!hasDirty) {
                dirty = rc;
                hasDirty = true;
                return;
            }
            dirty.left = std::min(dirty.left, rc.left);
            dirty.top = std::min(dirty.top, rc.top);
            dirty.right = std::max(dirty.right, rc.right);
            dirty.bottom = std::max(dirty.bottom, rc.bottom);
        };
        auto appendSelectionInfo = [&](const RECT& rc) {
            RECT n = NormalizeRect(rc);
            if (RectWidth(n) < kMinSelection || RectHeight(n) < kMinSelection) {
                return;
            }
            const int infoOffsetY = std::max(28, static_cast<int>(std::round(32.0f * dpiScale)));
            const int infoW = std::max(160, static_cast<int>(std::round(240.0f * dpiScale)));
            const int infoH = std::max(24, static_cast<int>(std::round(28.0f * dpiScale)));
            const int infoTop = std::max(0, static_cast<int>(n.top) - infoOffsetY);
            RECT info{
                n.left,
                infoTop,
                static_cast<LONG>(n.left + infoW),
                static_cast<LONG>(infoTop + infoH)
            };
            appendRect(info);
        };
        appendRect(before);
        appendRect(after);
        if (!whiteboardMode_ && !screenRecordingMode_ && !recordingPreviewMode_) {
            appendSelectionInfo(before);
            appendSelectionInfo(after);
        }
        if (hasDirty) {
            const int dirtyPad = recordingPreviewMode_
                ? std::max(4, static_cast<int>(std::round(4.0f * dpiScale)))
                : 16;
            InflateRect(&dirty, dirtyPad, dirtyPad);
            InvalidateRect(hwnd_, &dirty, FALSE);
        } else {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    };

    if (dragMode_ == DragMode::SelectingNew) {
        if (alt) {
            selection_.left = dragStart_.x - dx;
            selection_.right = dragStart_.x + dx;
            selection_.top = dragStart_.y - dy;
            selection_.bottom = dragStart_.y + dy;
        } else {
            selection_.left = dragStart_.x;
            selection_.top = dragStart_.y;
            selection_.right = p.x;
            selection_.bottom = p.y;
        }

        if (shift) {
            RECT n = NormalizeRect(selection_);
            const int w = RectWidth(n);
            const int h = RectHeight(n);
            const int side = std::max(w, h);
            n.right = n.left + side;
            n.bottom = n.top + side;
            selection_ = n;
        }

        ClampSelectionToBounds();
        invalidateSelectionDelta(oldSelection, selection_);
        return;
    }

    if (dragMode_ == DragMode::MoveSelection) {
        RECT prevSelection = selection_;
        MoveRectBy(selection_, dx, dy);
        if (!recordingPreviewMode_) {
            ClampSelectionToBounds();
        }
        const int appliedDx = selection_.left - prevSelection.left;
        const int appliedDy = selection_.top - prevSelection.top;
        if ((appliedDx != 0 || appliedDy != 0) && !recordingPreviewMode_) {
            for (auto& s : shapes_) {
                MoveShape(s, appliedDx, appliedDy);
            }
            if (hasCurrentShape_) {
                MoveShape(currentShape_, appliedDx, appliedDy);
            }
        }
        dragStart_ = p;
        UpdateCursorVisual(p);
        if (recordingPreviewMode_) {
            RefreshPreviewPlacement();
        }
        invalidateSelectionDelta(oldSelection, selection_);
        return;
    }

    if (dragMode_ == DragMode::ResizeSelection) {
        selection_ = initialSelection_;
        switch (activeHit_) {
        case HitKind::Left: selection_.left = initialSelection_.left + dx; break;
        case HitKind::Top: selection_.top = initialSelection_.top + dy; break;
        case HitKind::Right: selection_.right = initialSelection_.right + dx; break;
        case HitKind::Bottom: selection_.bottom = initialSelection_.bottom + dy; break;
        case HitKind::LeftTop: selection_.left += dx; selection_.top += dy; break;
        case HitKind::RightTop: selection_.right += dx; selection_.top += dy; break;
        case HitKind::LeftBottom: selection_.left += dx; selection_.bottom += dy; break;
        case HitKind::RightBottom: selection_.right += dx; selection_.bottom += dy; break;
        default: break;
        }

        if (shift) {
            RECT n = NormalizeRect(selection_);
            const int side = std::max(RectWidth(n), RectHeight(n));
            n.right = n.left + side;
            n.bottom = n.top + side;
            selection_ = n;
        }

        if (!recordingPreviewMode_) {
            ClampSelectionToBounds();
        }
        UpdateCursorVisual(p);
        if (recordingPreviewMode_) {
            RefreshPreviewPlacement();
        }
        invalidateSelectionDelta(oldSelection, selection_);
        return;
    }

    if (dragMode_ == DragMode::DrawShape && hasCurrentShape_) {
        const RECT beforeBounds = NormalizeRect(ShapeBounds(currentShape_));
        RECT sr = SelectionRectNormalized();
        POINT cp{
            std::clamp(static_cast<int>(p.x), static_cast<int>(sr.left), static_cast<int>(sr.right)),
            std::clamp(static_cast<int>(p.y), static_cast<int>(sr.top), static_cast<int>(sr.bottom))
        };

        if (currentShape_.type == ToolType::Pen) {
            POINT prev = currentShape_.points.empty() ? cp : currentShape_.points.back();
            AppendSmoothPenPoint(currentShape_.points, cp);

            RECT b = currentShape_.rect;
            b.left = std::min(b.left, static_cast<LONG>(cp.x));
            b.top = std::min(b.top, static_cast<LONG>(cp.y));
            b.right = std::max(b.right, static_cast<LONG>(cp.x));
            b.bottom = std::max(b.bottom, static_cast<LONG>(cp.y));
            currentShape_.rect = b;

            RECT dirty{
                std::min(prev.x, cp.x),
                std::min(prev.y, cp.y),
                std::max(prev.x, cp.x),
                std::max(prev.y, cp.y)
            };
            const int pad = std::max(6, static_cast<int>(std::ceil(currentShape_.stroke * 2.5f)));
            InflateRect(&dirty, pad, pad);
            InvalidateRect(hwnd_, &dirty, FALSE);
            return;
        } else {
            currentShape_.rect.right = cp.x;
            currentShape_.rect.bottom = cp.y;
            if (currentShape_.points.size() < 2) {
                currentShape_.points.push_back(cp);
            } else {
                currentShape_.points[1] = cp;
            }

            if (shift && (currentShape_.type == ToolType::Rect || currentShape_.type == ToolType::Ellipse || currentShape_.type == ToolType::Mosaic)) {
                RECT r = NormalizeRect(currentShape_.rect);
                const int side = std::max(RectWidth(r), RectHeight(r));
                r.right = r.left + side;
                r.bottom = r.top + side;
                currentShape_.rect = r;
            }
        }

        RECT afterBounds = NormalizeRect(ShapeBounds(currentShape_));
        RECT dirty = beforeBounds;
        if (RectWidth(dirty) <= 0 || RectHeight(dirty) <= 0) {
            dirty = afterBounds;
        } else if (RectWidth(afterBounds) > 0 && RectHeight(afterBounds) > 0) {
            dirty.left = std::min(dirty.left, afterBounds.left);
            dirty.top = std::min(dirty.top, afterBounds.top);
            dirty.right = std::max(dirty.right, afterBounds.right);
            dirty.bottom = std::max(dirty.bottom, afterBounds.bottom);
        }

        const int pad = std::max(10, static_cast<int>(std::ceil(currentShape_.stroke * 6.0f)));
        InflateRect(&dirty, pad, pad);
        if (RectWidth(dirty) > 0 && RectHeight(dirty) > 0) {
            InvalidateRect(hwnd_, &dirty, FALSE);
        } else {
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }

    if (dragMode_ == DragMode::MoveShape && selectedShape_ >= 0 && selectedShape_ < static_cast<int>(shapes_.size())) {
        MoveShape(shapes_[selectedShape_], dx, dy);
        dragStart_ = p;
        MarkSceneDirty();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (dragMode_ == DragMode::ResizeShape && selectedShape_ >= 0 && selectedShape_ < static_cast<int>(shapes_.size())) {
        POINT anchor = MakePoint(selectedShapeBounds_.left, selectedShapeBounds_.top);
        ResizeShape(shapes_[selectedShape_], activeShapeHit_, anchor, p, shift);
        MarkSceneDirty();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
}

void OverlayWindow::OnMouseUp(POINT p) {
    if (longCaptureMode_) {
        return;
    }
    lastMouse_ = p;
    precisionModeActive_ = false;
    precisionBounds_ = RECT{};
    precisionLastRaw_ = p;
    precisionMouseX_ = static_cast<double>(p.x);
    precisionMouseY_ = static_cast<double>(p.y);
    const bool endedSelectionDrag =
        (dragMode_ == DragMode::SelectingNew || dragMode_ == DragMode::MoveSelection || dragMode_ == DragMode::ResizeSelection);

    if (endedSelectionDrag) {
        if (!recordingPreviewMode_) {
            ClampSelectionToBounds();
        }
        RECT sr = SelectionRectNormalized();
        if (stage_ == Stage::Selecting && dragMode_ == DragMode::SelectingNew) {
            if ((RectWidth(sr) < kMinSelection || RectHeight(sr) < kMinSelection) && hoverWindowRect_.has_value()) {
                selection_ = *hoverWindowRect_;
                sr = SelectionRectNormalized();
            }
        }
        if (RectWidth(sr) >= kMinSelection && RectHeight(sr) >= kMinSelection) {
            if (stage_ == Stage::Selecting) {
                stage_ = Stage::Annotating;
            }
            RefreshToolbarPlacement(false);
            cursorInfoEnabled_ = !whiteboardMode_ && !screenRecordingMode_;
            if (whiteboardMode_ || screenRecordingMode_) {
                followHudEnabled_ = false;
                ClearFollowHud();
            } else {
                RefreshFollowHudFromLastMouse();
            }
        }
        MarkSceneDirty();
    }

    if (dragMode_ == DragMode::DrawShape) {
        CommitCurrentShape();
    }

    dragMode_ = DragMode::None;
    activeHit_ = HitKind::None;
    activeShapeHit_ = HitKind::None;
    ReleaseCapture();
    if (recordingPreviewMode_) {
        ClearFollowHud();
    }
    if (recordingPreviewMode_) {
        RECT sr = SelectionRectNormalized();
        if (PtInRect(&sr, p)) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        } else {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        }
    } else {
        UpdateCursorVisual(p);
    }
    if (endedSelectionDrag) {
        if (recordingPreviewMode_) {
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else {
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
        }
    } else {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void OverlayWindow::OnRightClick(POINT) {
    if (longCaptureMode_ || screenRecordingMode_ || recordingPreviewMode_) {
        Finish(OverlayAction::Cancel);
        return;
    }
    if (textEditing_) {
        EndTextEdit(false);
        return;
    }
    if (dragMode_ != DragMode::None) {
        dragMode_ = DragMode::None;
        ReleaseCapture();
    }

    if (hasCurrentShape_) {
        hasCurrentShape_ = false;
        currentShape_ = {};
        MarkSceneDirty();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (stage_ == Stage::Annotating) {
        if (!shapes_.empty()) {
            Undo();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }

        stage_ = Stage::Selecting;
        selection_ = RECT{};
        selectedShape_ = -1;
        cursorInfoEnabled_ = false;
        hoverWindowRect_.reset();
        toolbar_.Hide();
        MarkSceneDirty();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (HasSelection()) {
        selection_ = RECT{};
        cursorInfoEnabled_ = false;
        hoverWindowRect_.reset();
        MarkSceneDirty();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    Finish(OverlayAction::Cancel);
}

void OverlayWindow::OnMiddleClick(POINT p) {
    if (longCaptureMode_ || screenRecordingMode_ || recordingPreviewMode_) {
        return;
    }
    lastMouse_ = p;

    if (stage_ == Stage::Selecting) {
        if (!HasSelection()) {
            if (hoverWindowRect_.has_value()) {
                selection_ = *hoverWindowRect_;
            } else {
                return;
            }
        }
        stage_ = Stage::Annotating;
    }

    if (!HasSelection()) {
        return;
    }
    EndTextEdit(true);
    if (hasCurrentShape_) {
        CommitCurrentShape();
    }
    Finish(OverlayAction::Pin);
}

void OverlayWindow::OnKeyDown(WPARAM vk, LPARAM lParam) {
    if (recordingPreviewMode_) {
        return;
    }
    const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool firstPress = (lParam & (1 << 30)) == 0;

    if (vk == VK_TAB && firstPress && HasSelection() && !longCaptureMode_ && !whiteboardMode_ && !screenRecordingMode_) {
        followHudEnabled_ = !followHudEnabled_;
        RefreshFollowHudFromLastMouse();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (vk == VK_MENU && firstPress && cursorInfoEnabled_ && followHudEnabled_ && HasSelection() &&
        dragMode_ == DragMode::None && ShouldShowCursorInfoOverlay()) {
        colorHexMode_ = !colorHexMode_;
        if (lastInfoRect_.has_value() || lastMagnifierRect_.has_value() ||
            lastVerticalGuideRect_.has_value() || lastHorizontalGuideRect_.has_value()) {
            UpdateHudWindows(lastInfoRect_, lastMagnifierRect_, lastVerticalGuideRect_, lastHorizontalGuideRect_);
        }
        return;
    }

    if (longCaptureMode_ && vk == VK_ESCAPE) {
        Finish(OverlayAction::Cancel);
        return;
    }

    if (whiteboardMode_ && vk == VK_ESCAPE) {
        Finish(OverlayAction::Cancel);
        return;
    }

    if (screenRecordingMode_ && vk == VK_ESCAPE) {
        HWND target = GetForegroundWindow();
        if (!target || target == hwnd_ || (toolbar_.Hwnd() && (target == toolbar_.Hwnd() || IsChild(toolbar_.Hwnd(), target)))) {
            target = preRecordingForegroundHwnd_;
        }
        if (target && target != hwnd_ && !(toolbar_.Hwnd() && (target == toolbar_.Hwnd() || IsChild(toolbar_.Hwnd(), target)))) {
            PostMessageW(target, WM_KEYDOWN, VK_ESCAPE, lParam);
            PostMessageW(target, WM_KEYUP, VK_ESCAPE, (lParam & 0x7FFF0000) | 0xC0000001);
        }
        return;
    }

    if (screenRecordingMode_ && ctrlDown) {
        return;
    }

    if (ctrlDown && (vk == 'C' || vk == 'c') && HasSelection()) {
        EndTextEdit(true);
        Image composed = whiteboardMode_ ? ComposeBackgroundSelection() : ComposeCurrent();
        if (composed.IsValid()) {
            clipboard_.CopyImage(hwnd_, composed);
            Finish(OverlayAction::Cancel);
        }
        return;
    }

    if (ctrlDown && (vk == 'S' || vk == 's') && HasSelection()) {
        EndTextEdit(true);
        if (SaveSelectionWithoutHistory()) {
            Finish(OverlayAction::Cancel);
        }
        return;
    }

    if (ctrlDown && firstPress && (vk == 'Z' || vk == 'z')) {
        Undo();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (ctrlDown && firstPress && (vk == 'Y' || vk == 'y')) {
        Redo();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    if (vk == VK_ESCAPE) {
        if (screenRecordingMode_) {
            return;
        }
        const bool inEditingSession = stage_ == Stage::Annotating && HasSelection() &&
            (tool_ != ToolType::None || selectedShape_ >= 0 || hasCurrentShape_ || textEditing_ ||
                dragMode_ == DragMode::DrawShape || dragMode_ == DragMode::MoveShape || dragMode_ == DragMode::ResizeShape);
        if (inEditingSession) {
            ExitToCursorMode();
            return;
        }

        Finish(OverlayAction::Cancel);
        return;
    }




    if (vk == VK_SPACE) {
        if (screenRecordingMode_) {
            return;
        }
        if (HasSelection()) {
            Finish(OverlayAction::QuickSave);
        }
        return;
    }

    if ((vk == 'C' || vk == 'c') && TryHandleColorCopyHotkey()) {
        Finish(OverlayAction::Cancel);
        return;
    }

    if (!HasSelection()) {
        return;
    }

    const RECT selectionBeforeMove = selection_;
    const int step = shiftDown ? 10 : 1;
    bool moved = false;
    switch (vk) {
    case VK_LEFT: selection_.left -= step; selection_.right -= step; moved = true; break;
    case VK_RIGHT: selection_.left += step; selection_.right += step; moved = true; break;
    case VK_UP: selection_.top -= step; selection_.bottom -= step; moved = true; break;
    case VK_DOWN: selection_.top += step; selection_.bottom += step; moved = true; break;
    default: break;
    }

    if (moved) {
        ClampSelectionToBounds();
        const int appliedDx = selection_.left - selectionBeforeMove.left;
        const int appliedDy = selection_.top - selectionBeforeMove.top;
        if (appliedDx != 0 || appliedDy != 0) {
            for (auto& s : shapes_) {
                MoveShape(s, appliedDx, appliedDy);
            }
            if (hasCurrentShape_) {
                MoveShape(currentShape_, appliedDx, appliedDy);
            }
            if (!whiteboardMode_) {
                MarkSceneDirty();
            }
        } else if (!whiteboardMode_) {
            MarkSceneDirty();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void OverlayWindow::OnToolbarCommand(UINT id, UINT notifyCode) {
    auto isButtonClickNotify = [](UINT code) {
        return code == BN_CLICKED || code == BN_DOUBLECLICKED;
    };
    auto restoreOverlayFocus = [&]() {
        if (screenRecordingMode_) {
            if (preRecordingForegroundHwnd_ && IsWindow(preRecordingForegroundHwnd_)) {
                SetForegroundWindow(preRecordingForegroundHwnd_);
            }
            return;
        }
        if (recordingPreviewMode_) {
            return;
        }
        if (hwnd_ && IsWindow(hwnd_) && GetFocus() != hwnd_) {
            SetFocus(hwnd_);
        }
    };

    if (recordingPreviewMode_) {
        switch (id) {
        case ID_TOOL_PREVIEW_SPEED:
            if (notifyCode == CBN_SELCHANGE && previewPlayer_) {
                previewPlayer_->SetRate(previewBar_.PlaybackRate());
            }
            return;
        case ID_TOOL_PREVIEW_EXPORT_QUALITY:
            return;
        case ID_TOOL_PREVIEW_PROGRESS:
            if (previewPlayer_) {
                const LONGLONG targetPosition = previewBar_.SliderSeekPosition100ns();
                auto seekPreviewAt = [&](LONGLONG pos) {
                    if (previewSeekWarmupNeeded_) {
                        previewPlayer_->PrimeFirstFrame(pos);
                        previewSeekWarmupNeeded_ = false;
                    } else {
                        previewPlayer_->SeekPreviewFrame(pos);
                    }
                };
                if (previewBar_.IsSliderTracking()) {
                    if (!previewSeekTracking_) {
                        previewSeekTracking_ = true;
                        previewSeekResumeAfterRelease_ = previewPlayer_->IsPlaying();
                        if (previewSeekResumeAfterRelease_) {
                            previewPlayer_->Pause();
                        }
                    }
                    seekPreviewAt(targetPosition);
                    const LONGLONG duration = previewPlayer_->Duration100ns();
                    previewBar_.SetPreviewMetrics(duration, targetPosition);
                    if (duration > 0 && targetPosition < duration - 200000) {
                        previewSeekWarmupNeeded_ = false;
                    }
                    previewBar_.SetPlaying(false);
                    return;
                }

                seekPreviewAt(targetPosition);
                const LONGLONG duration = previewPlayer_->Duration100ns();
                previewBar_.SetPreviewMetrics(duration, targetPosition);
                if (duration > 0 && targetPosition < duration - 200000) {
                    previewSeekWarmupNeeded_ = false;
                }

                if (previewSeekTracking_) {
                    const bool shouldResume = previewSeekResumeAfterRelease_;
                    previewSeekTracking_ = false;
                    previewSeekResumeAfterRelease_ = false;
                    if (shouldResume) {
                        previewPlayer_->Play();
                    }
                }
                previewBar_.SetPlaying(previewPlayer_->IsPlaying());
            }
            return;
        case ID_TOOL_PREVIEW_PLAY_PAUSE:
            if (!isButtonClickNotify(notifyCode) || !previewPlayer_) {
                return;
            }
            {
                const bool wasPlaying = previewPlayer_->IsPlaying();
                if (!wasPlaying) {
                    const LONGLONG duration = previewPlayer_->Duration100ns();
                    const LONGLONG position = previewPlayer_->Position100ns();
                    if (duration > 0 && position >= duration - 200000) {
                        previewPlayer_->SeekToTime(0);
                    }
                }
                previewPlayer_->TogglePlayPause();
                previewBar_.SetPlaying(!wasPlaying);
                previewSeekWarmupNeeded_ = false;
                if (wasPlaying) {
                    const LONGLONG duration = previewPlayer_->Duration100ns();
                    const LONGLONG position = previewPlayer_->Position100ns();
                    previewBar_.SetPreviewMetrics(duration, position);
                }
            }
            return;
        case ID_TOOL_PREVIEW_RERECORD:
            if (!isButtonClickNotify(notifyCode)) {
                return;
            }
            if (previewExporting_) {
                MessageBoxW(hwnd_, L"\u6B63\u5728\u5BFC\u51FA\u89C6\u9891\uff0c\u8BF7\u7A0D\u5019\u518D\u8BD5\u3002", L"SnapPin", MB_ICONINFORMATION);
                return;
            }
            ExitRecordingPreviewMode(true);
            EnterScreenRecordingMode();
            return;
        case ID_TOOL_PREVIEW_EXPORT:
            if (!isButtonClickNotify(notifyCode)) {
                return;
            }
            if (previewExporting_) {
                MessageBoxW(hwnd_, L"\u6B63\u5728\u5BFC\u51FA\u89C6\u9891\uff0c\u8BF7\u7A0D\u5019\u3002", L"SnapPin", MB_ICONINFORMATION);
                return;
            }
            if (recordingTempPath_.empty()) {
                MessageBoxW(hwnd_, L"No recording file available.", L"SnapPin", MB_ICONWARNING);
                return;
            }
            {
                std::filesystem::path defaultPath = recordingTempPath_;
                defaultPath = defaultPath.parent_path() / (defaultPath.stem().wstring() + L"_export.mp4");
                std::wstring initialDir = defaultPath.parent_path().wstring();
                std::wstring defaultName = defaultPath.filename().wstring();
                wchar_t pathBuf[MAX_PATH]{};
                wcsncpy_s(pathBuf, _countof(pathBuf), defaultName.c_str(), _TRUNCATE);

                OPENFILENAMEW ofn{};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd_;
                ofn.lpstrFilter = L"MP4 Video (*.mp4)\0*.mp4\0\0";
                ofn.lpstrFile = pathBuf;
                ofn.nMaxFile = MAX_PATH;
                ofn.lpstrInitialDir = initialDir.c_str();
                ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                ofn.lpstrDefExt = L"mp4";
                if (!GetSaveFileNameW(&ofn)) {
                    return;
                }

                const std::filesystem::path inputPath = recordingTempPath_;
                const std::filesystem::path outputPath = std::filesystem::path(pathBuf);
                const VideoExportQuality quality = previewBar_.ExportQuality();
                const HWND notifyHwnd = hwnd_;
                previewExporting_ = true;
                std::thread([notifyHwnd, inputPath, outputPath, quality]() {
                    auto payload = std::make_unique<PreviewExportDonePayload>();
                    payload->outputPath = outputPath.wstring();
                    try {
                        payload->success = ScreenRecorder::ExportRecording(inputPath, outputPath, quality, payload->errorMessage);
                    } catch (const std::exception& ex) {
                        payload->success = false;
                        payload->errorMessage = L"Export failed: " + Utf8ToWide(ex.what());
                        Logger::Instance().Error(payload->errorMessage);
                    } catch (...) {
                        payload->success = false;
                        payload->errorMessage = L"Export failed: unknown exception.";
                        Logger::Instance().Error(payload->errorMessage);
                    }
                    if (!WindowMessagePayload::Post(notifyHwnd, WMU_PREVIEW_EXPORT_DONE, 0, std::move(payload))) {
                        Logger::Instance().Error(L"Preview export result dispatch failed.");
                    }
                }).detach();
            }
            return;
        case ID_TOOL_PREVIEW_CLOSE:
            if (!isButtonClickNotify(notifyCode)) {
                return;
            }
            if (previewExporting_) {
                MessageBoxW(hwnd_, L"\u6B63\u5728\u5BFC\u51FA\u89C6\u9891\uff0c\u8BF7\u7A0D\u5019\u518D\u5173\u95ED\u3002", L"SnapPin", MB_ICONINFORMATION);
                return;
            }
            ExitRecordingPreviewMode(true);
            Finish(OverlayAction::Cancel);
            return;
        default:
            return;
        }
    }

    auto isComboControl = [](UINT cid) {
        return cid == ID_TOOL_STROKE_WIDTH || cid == ID_TOOL_TEXT_SIZE || cid == ID_TOOL_TEXT_STYLE ||
            cid == ID_TOOL_RECORD_DELAY || cid == ID_TOOL_RECORD_FPS;
    };
    auto isStyleControl = [](UINT cid) {
        return cid == ID_TOOL_STROKE_COLOR || cid == ID_TOOL_FILL_ENABLE ||
            cid == ID_TOOL_FILL_COLOR || cid == ID_TOOL_TEXT_COLOR;
    };
    auto isActionButton = [](UINT cid) {
        switch (cid) {
        case ID_TOOL_UNDO:
        case ID_TOOL_REDO:
        case ID_TOOL_SAVE:
        case ID_TOOL_COPY:
        case ID_TOOL_COPY_FILE:
        case ID_TOOL_PIN:
        case ID_TOOL_LONG_CAPTURE:
        case ID_TOOL_OCR:
        case ID_TOOL_WHITEBOARD:
        case ID_TOOL_SCREEN_RECORD:
        case ID_TOOL_TRIM_ABOVE:
        case ID_TOOL_TRIM_BELOW:
        case ID_TOOL_RECORD_TOGGLE:
        case ID_TOOL_RECORD_PAUSE:
        case ID_TOOL_RECORD_SYSTEM_AUDIO:
        case ID_TOOL_RECORD_MIC_AUDIO:
        case ID_TOOL_CANCEL:
            return true;
        default:
            return false;
        }
    };
    const bool modeButton = toolbar_.IsModeButtonId(id);
    const bool comboControl = isComboControl(id);
    const bool styleControl = isStyleControl(id);
    const bool actionButton = isActionButton(id);

    if (!(modeButton || comboControl || styleControl || actionButton)) {
        return;
    }
    if (longCaptureMode_) {
        const bool allowedInLongCapture =
            id == ID_TOOL_SAVE ||
            id == ID_TOOL_COPY ||
            id == ID_TOOL_COPY_FILE ||
            id == ID_TOOL_PIN ||
            id == ID_TOOL_OCR ||
            id == ID_TOOL_TRIM_ABOVE ||
            id == ID_TOOL_TRIM_BELOW ||
            id == ID_TOOL_CANCEL;
        if (!allowedInLongCapture) {
            return;
        }
    }
    if (screenRecordingMode_) {
        const bool allowedInRecording =
            id == ID_TOOL_RECORD_DELAY ||
            id == ID_TOOL_RECORD_FPS ||
            id == ID_TOOL_RECORD_TOGGLE ||
            id == ID_TOOL_RECORD_PAUSE ||
            id == ID_TOOL_RECORD_SYSTEM_AUDIO ||
            id == ID_TOOL_RECORD_MIC_AUDIO ||
            id == ID_TOOL_CANCEL;
        if (!allowedInRecording) {
            return;
        }
    }

    if (comboControl) {
        if (notifyCode == CBN_DROPDOWN) {
            return;
        }
        if (notifyCode == CBN_CLOSEUP) {
            restoreOverlayFocus();
            return;
        }
        if (notifyCode != CBN_SELCHANGE) {
            return;
        }
    } else if (!isButtonClickNotify(notifyCode)) {
        return;
    }

    if (modeButton) {
        const ToolType requested = toolbar_.ToolFromButtonId(id);
        if (tool_ != ToolType::None && requested != ToolType::None && requested != tool_) {
            return;
        }

        if (textEditing_ && requested != ToolType::Text) {
            EndTextEdit(true);
        }

        tool_ = requested;
        if (tool_ == ToolType::None) {
            selectedShape_ = -1;
            activeShapeHit_ = HitKind::None;
            hasCurrentShape_ = false;
            currentShape_ = {};
            dragMode_ = DragMode::None;
            ReleaseCapture();
        }
        toolbar_.SetActiveTool(tool_);
        ApplyToolbarStyle();
        if (HasSelection()) {
            RefreshToolbarPlacement();
        }
        MarkSceneDirty();
        InvalidateRect(hwnd_, nullptr, FALSE);
        restoreOverlayFocus();
        return;
    }

    if (id == ID_TOOL_STROKE_WIDTH || id == ID_TOOL_FILL_ENABLE || id == ID_TOOL_TEXT_SIZE || id == ID_TOOL_TEXT_STYLE) {
        ApplyToolbarStyle();
        InvalidateRect(hwnd_, nullptr, FALSE);
        restoreOverlayFocus();
        return;
    }
    if (id == ID_TOOL_RECORD_DELAY || id == ID_TOOL_RECORD_FPS) {
        restoreOverlayFocus();
        return;
    }
    if (id == ID_TOOL_STROKE_COLOR) {
        if (toolbar_.ChooseStrokeColor(hwnd_)) {
            ApplyToolbarStyle();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        restoreOverlayFocus();
        return;
    }
    if (id == ID_TOOL_FILL_COLOR) {
        if (toolbar_.ChooseFillColor(hwnd_)) {
            ApplyToolbarStyle();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        restoreOverlayFocus();
        return;
    }
    if (id == ID_TOOL_TEXT_COLOR) {
        if (toolbar_.ChooseTextColor(hwnd_)) {
            ApplyToolbarStyle();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        restoreOverlayFocus();
        return;
    }

    if (textEditing_ && id != ID_TOOL_TEXT_SIZE && id != ID_TOOL_TEXT_STYLE && id != ID_TOOL_TEXT_COLOR) {
        EndTextEdit(true);
    }

    switch (id) {
    case ID_TOOL_UNDO: Undo(); break;
    case ID_TOOL_REDO: Redo(); break;
    case ID_TOOL_SAVE: Finish(OverlayAction::Save); break;
    case ID_TOOL_COPY: Finish(OverlayAction::Copy); break;
    case ID_TOOL_COPY_FILE: Finish(OverlayAction::CopyFile); break;
    case ID_TOOL_PIN: Finish(OverlayAction::Pin); break;
    case ID_TOOL_OCR: Finish(OverlayAction::Ocr); break;
    case ID_TOOL_TRIM_ABOVE: {
        if (longCaptureMode_ && longCaptureImage_.IsValid()) {
            const int cutY = std::clamp(longCaptureViewportOffsetY_, 0, std::max(0, longCaptureImage_.height - 1));
            const int keepH = std::max(1, longCaptureImage_.height - cutY);
            Image trimmed = Image::Create(longCaptureImage_.width, keepH);
            if (trimmed.IsValid()) {
                for (int y = 0; y < keepH; ++y) {
                    const uint8_t* src = longCaptureImage_.bgra.data() +
                        static_cast<size_t>(cutY + y) * static_cast<size_t>(longCaptureImage_.width) * 4;
                    uint8_t* dst = trimmed.bgra.data() +
                        static_cast<size_t>(y) * static_cast<size_t>(trimmed.width) * 4;
                    memcpy(dst, src, static_cast<size_t>(trimmed.width) * 4);
                }
                longCaptureImage_ = std::move(trimmed);
                longCaptureViewportOffsetY_ = 0;
                longCaptureThumbDirty_ = true;
                const float dpiScale = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
                UpdateLongCaptureThumbnailCache(dpiScale, true);
            }
        }
        break;
    }
    case ID_TOOL_TRIM_BELOW: {
        if (longCaptureMode_ && longCaptureImage_.IsValid()) {
            const int viewH = longCaptureLastFrame_.IsValid() ? longCaptureLastFrame_.height : RectHeight(SelectionRectNormalized());
            const int cutBottom = std::clamp(longCaptureViewportOffsetY_ + std::max(1, viewH), 1, longCaptureImage_.height);
            Image trimmed = Image::Create(longCaptureImage_.width, cutBottom);
            if (trimmed.IsValid()) {
                for (int y = 0; y < cutBottom; ++y) {
                    const uint8_t* src = longCaptureImage_.bgra.data() +
                        static_cast<size_t>(y) * static_cast<size_t>(longCaptureImage_.width) * 4;
                    uint8_t* dst = trimmed.bgra.data() +
                        static_cast<size_t>(y) * static_cast<size_t>(trimmed.width) * 4;
                    memcpy(dst, src, static_cast<size_t>(trimmed.width) * 4);
                }
                longCaptureImage_ = std::move(trimmed);
                longCaptureViewportOffsetY_ = std::clamp(longCaptureViewportOffsetY_, 0, std::max(0, longCaptureImage_.height - 1));
                longCaptureThumbDirty_ = true;
                const float dpiScale = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
                UpdateLongCaptureThumbnailCache(dpiScale, true);
            }
        }
        break;
    }
    case ID_TOOL_LONG_CAPTURE:
        if (longCaptureMode_) {
            Finish(OverlayAction::Cancel);
        } else {
            EnterLongCaptureMode();
        }
        break;
    case ID_TOOL_WHITEBOARD:
        EnterWhiteboardMode();
        break;
    case ID_TOOL_SCREEN_RECORD:
        EnterScreenRecordingMode();
        break;
    case ID_TOOL_RECORD_TOGGLE:
        if (recordingStartPending_) {
            StopWindowTimer(recordingStartTimer_);
            recordingStartPending_ = false;
            toolbar_.SetRecordingState(false, false);
            InvalidateRect(hwnd_, nullptr, FALSE);
            restoreOverlayFocus();
            return;
        }
        if (recordingActive_) {
            StopScreenRecording(true);
            return;
        }
        {
            const int delayMs = toolbar_.RecordingDelayMs();
            if (delayMs > 0) {
                StopWindowTimer(recordingStartTimer_);
                recordingStartPending_ = true;
                recordingStartTimer_ = SetTimer(hwnd_, IDT_RECORDING_START, static_cast<UINT>(delayMs), nullptr);
            } else {
                StartScreenRecording();
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            restoreOverlayFocus();
        }
        return;
    case ID_TOOL_RECORD_PAUSE:
        if (recordingActive_ && screenRecorder_) {
            recordingPaused_ = !recordingPaused_;
            screenRecorder_->Pause(recordingPaused_);
            toolbar_.SetRecordingState(recordingActive_, recordingPaused_);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        restoreOverlayFocus();
        return;
    case ID_TOOL_RECORD_SYSTEM_AUDIO:
    case ID_TOOL_RECORD_MIC_AUDIO:
        restoreOverlayFocus();
        return;
    case ID_TOOL_CANCEL:
        Finish(OverlayAction::Cancel);
        break;
    default:
        break;
    }
    if (id != ID_TOOL_SAVE && id != ID_TOOL_COPY && id != ID_TOOL_COPY_FILE && id != ID_TOOL_PIN && id != ID_TOOL_OCR && id != ID_TOOL_CANCEL) {
        MarkSceneDirty();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (id != ID_TOOL_SAVE && id != ID_TOOL_COPY && id != ID_TOOL_COPY_FILE && id != ID_TOOL_PIN && id != ID_TOOL_OCR && id != ID_TOOL_CANCEL) {
        restoreOverlayFocus();
    }
}

void OverlayWindow::EnterLongCaptureMode() {
    if (!hwnd_ || longCaptureMode_ || stage_ != Stage::Annotating || !HasSelection()) {
        return;
    }

    if (textEditing_) {
        EndTextEdit(true);
    }
    if (hasCurrentShape_) {
        CommitCurrentShape();
    }
    dragMode_ = DragMode::None;
    ReleaseCapture();

    longCaptureTargetHwnd_ = nullptr;
    longCaptureImage_ = {};
    longCaptureLastFrame_ = {};
    longCaptureViewportOffsetY_ = 0;
    longCaptureScrollDir_ = 0;
    longCaptureMatchAccepted_ = true;
    longCaptureThumbRect_.reset();
    longCaptureThumbCacheReady_ = false;
    longCaptureThumbDirty_ = true;
    longCaptureThumbLastRenderTick_ = 0;
    longCaptureThumbCache_ = {};

    longCaptureMode_ = true;
    whiteboardMode_ = false;
    screenRecordingMode_ = false;
    recordingPreviewMode_ = false;
    recordingStartPending_ = false;
    recordingActive_ = false;
    recordingPaused_ = false;
    tool_ = ToolType::None;
    toolbar_.SetLongCaptureMode(true);
    toolbar_.SetWhiteboardMode(false);
    toolbar_.SetScreenRecordingMode(false);
    toolbar_.SetRecordingState(false, false);
    toolbar_.SetActiveTool(ToolType::None);
    toolbar_.Hide();
    cursorInfoEnabled_ = false;
    followHudEnabled_ = false;
    ClearFollowHud();

    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    const LONG_PTR longExFlags = WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_LAYERED;
    if ((ex & longExFlags) != longExFlags) {
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex | longExFlags);
        SetLayeredWindowAttributes(hwnd_, kLongCaptureColorKey, 0, LWA_COLORKEY);
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    POINT sp{};
    if (GetCursorPos(&sp)) {
        HWND hit = WindowFromPoint(sp);
        if (hit && hit != hwnd_ &&
            !(toolbar_.Hwnd() && (hit == toolbar_.Hwnd() || IsChild(toolbar_.Hwnd(), hit)))) {
            longCaptureTargetHwnd_ = GetAncestor(hit, GA_ROOT);
            if (!longCaptureTargetHwnd_) {
                longCaptureTargetHwnd_ = hit;
            }
        }
    }
    if (!longCaptureTargetHwnd_) {
        HWND fg = GetForegroundWindow();
        if (fg && fg != hwnd_ &&
            !(toolbar_.Hwnd() && (fg == toolbar_.Hwnd() || IsChild(toolbar_.Hwnd(), fg)))) {
            longCaptureTargetHwnd_ = fg;
        }
    }
    if (longCaptureTargetHwnd_ && IsWindow(longCaptureTargetHwnd_)) {
        SetForegroundWindow(longCaptureTargetHwnd_);
        SetFocus(longCaptureTargetHwnd_);
    }

    StopWindowTimer(longCaptureTimer_);
    longCaptureTimer_ = SetTimer(hwnd_, IDT_LONG_CAPTURE, kLongCaptureTickMs, nullptr);
    MarkSceneDirty();
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    RefreshToolbarPlacement();
}

void OverlayWindow::EnterWhiteboardMode() {
    if (!hwnd_ || longCaptureMode_ || whiteboardMode_ || screenRecordingMode_ || stage_ != Stage::Annotating || !HasSelection()) {
        return;
    }

    if (textEditing_) {
        EndTextEdit(true);
    }
    if (hasCurrentShape_) {
        CommitCurrentShape();
    }
    dragMode_ = DragMode::None;
    ReleaseCapture();

    // Whiteboard starts from a clean drawing scene.
    shapes_.clear();
    undoStack_.clear();
    redoStack_.clear();
    nextNumber_ = 1;

    whiteboardMode_ = true;
    screenRecordingMode_ = false;
    recordingPreviewMode_ = false;
    recordingStartPending_ = false;
    recordingActive_ = false;
    recordingPaused_ = false;
    tool_ = ToolType::None;
    selectedShape_ = -1;
    activeShapeHit_ = HitKind::None;
    hasCurrentShape_ = false;
    currentShape_ = {};

    cursorInfoEnabled_ = false;
    followHudEnabled_ = false;
    ClearFollowHud();

    toolbar_.SetLongCaptureMode(false);
    toolbar_.SetWhiteboardMode(true);
    toolbar_.SetScreenRecordingMode(false);
    toolbar_.SetRecordingState(false, false);
    toolbar_.SetActiveTool(tool_);
    ApplyToolbarStyle();
    RefreshToolbarPlacement();

    MarkSceneDirty();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void OverlayWindow::EnterScreenRecordingMode() {
    if (!hwnd_ || longCaptureMode_ || whiteboardMode_ || screenRecordingMode_ || recordingPreviewMode_ || stage_ != Stage::Annotating || !HasSelection()) {
        return;
    }

    if (textEditing_) {
        EndTextEdit(true);
    }
    if (hasCurrentShape_) {
        CommitCurrentShape();
    }
    dragMode_ = DragMode::None;
    ReleaseCapture();

    screenRecordingMode_ = true;
    recordingPreviewMode_ = false;
    longCaptureMode_ = false;
    whiteboardMode_ = false;
    recordingStartPending_ = false;
    recordingActive_ = false;
    recordingPaused_ = false;
    tool_ = ToolType::None;
    selectedShape_ = -1;
    activeShapeHit_ = HitKind::None;
    hasCurrentShape_ = false;
    currentShape_ = {};

    cursorInfoEnabled_ = false;
    followHudEnabled_ = false;
    ClearFollowHud();

    toolbar_.SetLongCaptureMode(false);
    toolbar_.SetWhiteboardMode(false);
    toolbar_.SetScreenRecordingMode(true);
    toolbar_.SetRecordingState(false, false);
    toolbar_.SetActiveTool(ToolType::None);

    HWND candidate = nullptr;
    POINT cursorPt{};
    if (GetCursorPos(&cursorPt)) {
        HWND hit = WindowFromPoint(cursorPt);
        if (hit && hit != hwnd_ && !(toolbar_.Hwnd() && (hit == toolbar_.Hwnd() || IsChild(toolbar_.Hwnd(), hit)))) {
            candidate = GetAncestor(hit, GA_ROOT);
            if (!candidate) {
                candidate = hit;
            }
        }
    }
    if (!candidate) {
        POINT center{(selection_.left + selection_.right) / 2, (selection_.top + selection_.bottom) / 2};
        POINT screenCenter = LocalToScreenPoint(center);
        HWND hit = WindowFromPoint(screenCenter);
        if (hit && hit != hwnd_ && !(toolbar_.Hwnd() && (hit == toolbar_.Hwnd() || IsChild(toolbar_.Hwnd(), hit)))) {
            candidate = GetAncestor(hit, GA_ROOT);
            if (!candidate) {
                candidate = hit;
            }
        }
    }
    if (!candidate) {
        HWND fg = GetForegroundWindow();
        if (fg && fg != hwnd_ && !(toolbar_.Hwnd() && (fg == toolbar_.Hwnd() || IsChild(toolbar_.Hwnd(), fg)))) {
            candidate = fg;
        }
    }
    preRecordingForegroundHwnd_ = candidate;

    ApplyRecordingOverlayWindowStyle();
    RefreshToolbarPlacement();
    MarkSceneDirty();
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_NOERASE);

    if (preRecordingForegroundHwnd_ && IsWindow(preRecordingForegroundHwnd_)) {
        SetForegroundWindow(preRecordingForegroundHwnd_);
    }
}

bool OverlayWindow::StartScreenRecording() {
    if (!hwnd_ || !screenRecordingMode_ || !HasSelection()) {
        return false;
    }
    if (recordingActive_) {
        return true;
    }
    if (!screenRecorder_) {
        screenRecorder_ = std::make_unique<ScreenRecorder>();
    }

    RECT screenRect = SelectionRectNormalized();
    OffsetRect(&screenRect, capture_.virtualRect.left, capture_.virtualRect.top);
    constexpr int kRecordingCaptureInset = 3;
    if (RectWidth(screenRect) > kRecordingCaptureInset * 2 && RectHeight(screenRect) > kRecordingCaptureInset * 2) {
        InflateRect(&screenRect, -kRecordingCaptureInset, -kRecordingCaptureInset);
    }

    std::error_code ec;
    if (!recordingTempPath_.empty()) {
        std::filesystem::remove(recordingTempPath_, ec);
    }
    recordingTempPath_ = BuildRecordingTempPath();

    ScreenRecordingOptions options{};
    options.screenRect = screenRect;
    options.fps = toolbar_.RecordingFps();
    options.recordSystemAudio = toolbar_.RecordSystemAudioEnabled();
    options.recordMicrophoneAudio = toolbar_.RecordMicrophoneAudioEnabled();
    options.outputPath = recordingTempPath_;
    options.videoBitrate = ScreenRecorder::RecommendedVideoBitrate(RectWidth(screenRect), RectHeight(screenRect), options.fps, VideoExportQuality::Original);

    if (!screenRecorder_->Start(options)) {
        recordingTempPath_.clear();
        MessageBoxW(hwnd_, L"Start recording failed.", L"SnapPin", MB_ICONERROR);
        toolbar_.SetRecordingState(false, false);
        return false;
    }

    recordingStartPending_ = false;
    recordingActive_ = true;
    recordingPaused_ = false;
    lastRecordingResult_.reset();
    toolbar_.SetRecordingState(true, false);
    ApplyRecordingOverlayWindowStyle();
    InvalidateRect(hwnd_, nullptr, FALSE);

    if (preRecordingForegroundHwnd_ && IsWindow(preRecordingForegroundHwnd_)) {
        SetForegroundWindow(preRecordingForegroundHwnd_);
    }
    return true;
}

void OverlayWindow::StopScreenRecording(bool enterPreview) {
    StopWindowTimer(recordingStartTimer_);
    if (recordingStartPending_) {
        recordingStartPending_ = false;
        toolbar_.SetRecordingState(false, false);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (!recordingActive_ || !screenRecorder_) {
        return;
    }

    ScreenRecordingResult result;
    const bool success = screenRecorder_->Stop(result);
    recordingActive_ = false;
    recordingPaused_ = false;
    toolbar_.SetRecordingState(false, false);
    InvalidateRect(hwnd_, nullptr, FALSE);

    if (!success) {
        std::error_code ec;
        if (!recordingTempPath_.empty()) {
            std::filesystem::remove(recordingTempPath_, ec);
        }
        recordingTempPath_.clear();
        lastRecordingResult_.reset();
        MessageBoxW(hwnd_, L"Stop recording failed.", L"SnapPin", MB_ICONERROR);
        return;
    }

    lastRecordingResult_ = result;
    recordingTempPath_ = result.outputPath;
    if (enterPreview) {
        EnterRecordingPreviewMode(result);
    }
}

void OverlayWindow::EnterRecordingPreviewMode(const ScreenRecordingResult& result) {
    if (!hwnd_ || !result.success) {
        return;
    }

    screenRecordingMode_ = false;
    recordingPreviewMode_ = true;
    recordingStartPending_ = false;
    recordingActive_ = false;
    recordingPaused_ = false;
    preRecordingForegroundHwnd_ = nullptr;

    toolbar_.SetScreenRecordingMode(false);
    toolbar_.SetRecordingState(false, false);
    toolbar_.Hide();

    cursorInfoEnabled_ = false;
    followHudEnabled_ = false;
    ClearFollowHud();
    previewSeekTracking_ = false;
    previewSeekResumeAfterRelease_ = false;
    previewSeekWarmupNeeded_ = false;
    previewExporting_ = false;

    ApplyPreviewOverlayWindowStyle();
    EnsurePreviewHostWindow();
    RefreshPreviewPlacement();

    previewBar_.SetPlaying(false);
    previewBar_.SetPreviewMetrics(result.duration100ns, 0);

    if (!previewPlayer_) {
        previewPlayer_ = std::make_unique<VideoPreviewPlayer>(hwnd_);
    }
    if (previewPlayer_) {
        previewPlayer_->Close();
        std::wstring errorMessage;
        if (!previewPlayer_->Open(previewVideoHostHwnd_, result.outputPath, errorMessage)) {
            const std::wstring message = errorMessage.empty() ? L"Open preview failed." : errorMessage;
            MessageBoxW(hwnd_, message.c_str(), L"SnapPin", MB_ICONERROR);
        }
    }

    StopWindowTimer(previewProgressTimer_);
    previewProgressTimer_ = SetTimer(hwnd_, IDT_PREVIEW_PROGRESS, 33, nullptr);

    MarkSceneDirty();
    InvalidateRect(hwnd_, nullptr, FALSE);
    RefreshToolbarPlacement();
}

void OverlayWindow::ExitRecordingPreviewMode(bool discardTempFile) {
    StopWindowTimer(previewProgressTimer_);
    if (previewPlayer_) {
        previewPlayer_->Close();
    }
    previewBar_.Hide();
    DestroyPreviewHostWindow();
    recordingPreviewMode_ = false;
    previewSeekTracking_ = false;
    previewSeekResumeAfterRelease_ = false;
    previewSeekWarmupNeeded_ = false;
    previewExporting_ = false;

    if (discardTempFile) {
        std::error_code ec;
        if (!recordingTempPath_.empty()) {
            std::filesystem::remove(recordingTempPath_, ec);
        }
        recordingTempPath_.clear();
    }
    lastRecordingResult_.reset();
}

void OverlayWindow::RestoreOverlayWindowStyle() {
    if (!hwnd_) {
        return;
    }
    const LONG_PTR normalExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    if (GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) != normalExStyle) {
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, normalExStyle);
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
}

void OverlayWindow::ApplyRecordingOverlayWindowStyle() {
    if (!hwnd_) {
        return;
    }
    const LONG_PTR recordingExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    if (GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) != recordingExStyle) {
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, recordingExStyle);
    }
    SetLayeredWindowAttributes(hwnd_, kLongCaptureColorKey, 0, LWA_COLORKEY);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void OverlayWindow::ApplyPreviewOverlayWindowStyle() {
    if (!hwnd_) {
        return;
    }
    const LONG_PTR previewExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE;
    if (GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) != previewExStyle) {
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, previewExStyle);
    }
    SetLayeredWindowAttributes(hwnd_, kLongCaptureColorKey, 0, LWA_COLORKEY);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void OverlayWindow::EnsurePreviewHostWindow() {
    if (previewVideoHostHwnd_ || !hwnd_) {
        return;
    }
    previewVideoHostHwnd_ = CreateWindowExW(
        WS_EX_NOPARENTNOTIFY,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | SS_BLACKRECT,
        0, 0, 0, 0,
        hwnd_,
        nullptr,
        hInstance_,
        nullptr);
    if (previewVideoHostHwnd_) {
        SetWindowSubclass(previewVideoHostHwnd_, PreviewHostSubclassProc, 1, 0);
    }
}

void OverlayWindow::DestroyPreviewHostWindow() {
    if (previewVideoHostHwnd_) {
        RemoveWindowSubclass(previewVideoHostHwnd_, PreviewHostSubclassProc, 1);
        DestroyWindow(previewVideoHostHwnd_);
        previewVideoHostHwnd_ = nullptr;
    }
}

void OverlayWindow::RefreshPreviewPlacement() {
    if (!recordingPreviewMode_ || !HasSelection()) {
        previewBar_.Hide();
        if (previewVideoHostHwnd_) {
            ShowWindow(previewVideoHostHwnd_, SW_HIDE);
        }
        return;
    }

    EnsurePreviewHostWindow();
    RECT sr = SelectionRectNormalized();
    RECT hostRect = sr;
    InflateRect(&hostRect, -2, -2);
    if (RectWidth(hostRect) < 2 || RectHeight(hostRect) < 2) {
        hostRect = sr;
    }
    if (previewVideoHostHwnd_) {
        UINT flags = SWP_NOACTIVATE | SWP_NOZORDER;
        if (!IsWindowVisible(previewVideoHostHwnd_)) {
            flags |= SWP_SHOWWINDOW;
        }
        SetWindowPos(previewVideoHostHwnd_, nullptr,
            hostRect.left, hostRect.top,
            std::max(1, RectWidth(hostRect)), std::max(1, RectHeight(hostRect)),
            flags);
    }
    previewBar_.ShowNear(sr, RECT{0, 0, capture_.image.width, capture_.image.height});
}

void OverlayWindow::UpdateRecordingPreviewProgress() {
    if (!recordingPreviewMode_) {
        return;
    }
    if (previewBar_.IsSliderTracking()) {
        return;
    }
    const bool isPlaying = previewPlayer_ && previewPlayer_->IsPlaying();
    previewBar_.SetPlaying(isPlaying);
    if (!isPlaying) {
        return;
    }
    const LONGLONG duration = previewPlayer_ ? previewPlayer_->Duration100ns() : (lastRecordingResult_.has_value() ? lastRecordingResult_->duration100ns : 0);
    const LONGLONG position = previewPlayer_ ? previewPlayer_->Position100ns() : 0;
    previewBar_.SetPreviewMetrics(duration, position);
}

std::filesystem::path OverlayWindow::BuildRecordingTempPath() const {
    std::filesystem::path base;
    try {
        base = std::filesystem::temp_directory_path();
    } catch (...) {
        base = std::filesystem::current_path();
    }
    base /= L"SnapPin";
    base /= L"Recordings";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);

    const std::wstring fileName = L"SnapPin_Record_" + FormatNowForFile() + L"_" + std::to_wstring(GetTickCount64() % 1000000ULL) + L".mp4";
    return base / fileName;
}

void OverlayWindow::UpdateLongCaptureThumbnailCache(float dpiScale, bool force) {
    if (!longCaptureMode_ || !HasSelection() || !longCaptureImage_.IsValid()) {
        return;
    }
    const DWORD now = GetTickCount();
    if (!force && !longCaptureThumbDirty_) {
        return;
    }
    if (!force && longCaptureThumbLastRenderTick_ != 0 &&
        (now - longCaptureThumbLastRenderTick_) < kLongCaptureThumbRefreshMs) {
        return;
    }

    const RECT sr = SelectionRectNormalized();
    const int panelW = std::max(140, static_cast<int>(std::round(168.0f * dpiScale)));
    const int panelH = std::max(120, RectHeight(sr));
    if (panelW <= 0 || panelH <= 0) {
        return;
    }

    LongCaptureThumbCache cache;
    cache.panelW = panelW;
    cache.panelH = panelH;
    cache.viewportOffsetY = longCaptureViewportOffsetY_;
    cache.viewportW = longCaptureLastFrame_.IsValid() ? longCaptureLastFrame_.width : RectWidth(sr);
    cache.viewportH = longCaptureLastFrame_.IsValid() ? longCaptureLastFrame_.height : RectHeight(sr);

    const double scale = std::min(
        static_cast<double>(panelW) / static_cast<double>(longCaptureImage_.width),
        static_cast<double>(panelH) / static_cast<double>(longCaptureImage_.height));
    cache.scale = std::max(1e-6, scale);
    cache.drawW = std::max(1, static_cast<int>(std::lround(longCaptureImage_.width * cache.scale)));
    cache.drawH = std::max(1, static_cast<int>(std::lround(longCaptureImage_.height * cache.scale)));

    Image scaled;
    if (force) {
        if (!ScaleImageBicubic(longCaptureImage_, cache.drawW, cache.drawH, scaled)) {
            scaled = longCaptureImage_.ResizeNearest(cache.drawW, cache.drawH);
        }
    } else {
        scaled = longCaptureImage_.ResizeNearest(cache.drawW, cache.drawH);
    }
    if (!scaled.IsValid()) {
        return;
    }

    cache.image = std::move(scaled);
    longCaptureThumbCache_ = std::move(cache);
    longCaptureThumbCacheReady_ = true;
    longCaptureThumbDirty_ = false;
    longCaptureThumbLastRenderTick_ = now;
}

void OverlayWindow::OnLongCaptureTimer() {
    if (!longCaptureMode_ || !hwnd_) {
        return;
    }

    if (toolbar_.Hwnd() && IsWindowVisible(toolbar_.Hwnd())) {
        POINT sp{};
        if (GetCursorPos(&sp)) {
            POINT lp = ScreenToLocalPoint(sp);
            if (IsPointInToolbarZone(lp, 1)) {
                return;
            }
        }
    }

    const RECT oldSelection = SelectionRectNormalized();
    const auto oldThumb = longCaptureThumbRect_;
    const int oldViewportOffset = longCaptureViewportOffsetY_;
    const int oldImageW = longCaptureImage_.width;
    const int oldImageH = longCaptureImage_.height;
    const bool oldMatchAccepted = longCaptureMatchAccepted_;

    Image frame;
    if (!CaptureTargetWindowFrame(frame) || !frame.IsValid()) {
        return;
    }
    if (!longCaptureLastFrame_.IsValid() ||
        longCaptureLastFrame_.width != frame.width ||
        longCaptureLastFrame_.height != frame.height) {
        longCaptureImage_ = frame;
        longCaptureLastFrame_ = frame;
        longCaptureViewportOffsetY_ = 0;
        longCaptureMatchAccepted_ = true;
    } else {
        const VerticalSsdMatch match = FindBestVerticalShiftSsdParallel(longCaptureLastFrame_, frame, longCaptureScrollDir_);
        int predictedOffset = longCaptureViewportOffsetY_ + match.shift;
        const int maxStep = std::max(24, frame.height - std::max(36, frame.height / 5));
        predictedOffset = std::clamp(predictedOffset,
            longCaptureViewportOffsetY_ - maxStep,
            longCaptureViewportOffsetY_ + maxStep);

        if (match.matched) {
            if (std::abs(match.shift) > 1) {
                AppendLongCaptureFrame(frame, predictedOffset);
            }
            longCaptureMatchAccepted_ = true;
            longCaptureScrollDir_ = (match.shift > 1) ? +1 : ((match.shift < -1) ? -1 : 0);
            // Only advance baseline frame when this frame passed matching.
            // If matching failed, keep previous baseline so next frame still compares against last valid image.
            longCaptureLastFrame_ = frame;
        } else {
            longCaptureMatchAccepted_ = false;
            longCaptureScrollDir_ = 0;
        }
    }

    if (longCaptureImage_.IsValid()) {
        const bool geometryChanged =
            (longCaptureViewportOffsetY_ != oldViewportOffset) ||
            (longCaptureImage_.width != oldImageW) ||
            (longCaptureImage_.height != oldImageH) ||
            (longCaptureMatchAccepted_ != oldMatchAccepted);
        const bool noCache = !longCaptureThumbCacheReady_;
        if (geometryChanged || noCache) {
            longCaptureThumbDirty_ = true;
        }
        const float dpiScale = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
        UpdateLongCaptureThumbnailCache(dpiScale, noCache);
    }

    MarkSceneDirty();
    RECT dirty = SelectionRectNormalized();
    auto mergeRect = [&](const RECT& rc) {
        RECT n = NormalizeRect(rc);
        if (!IsRectValid(n)) {
            return;
        }
        if (!IsRectValid(dirty)) {
            dirty = n;
            return;
        }
        dirty.left = std::min(dirty.left, n.left);
        dirty.top = std::min(dirty.top, n.top);
        dirty.right = std::max(dirty.right, n.right);
        dirty.bottom = std::max(dirty.bottom, n.bottom);
    };
    mergeRect(oldSelection);
    if (oldThumb.has_value()) {
        mergeRect(*oldThumb);
    }
    InflateRect(&dirty, 24, 24);
    dirty.left = std::max(0L, dirty.left - 220);
    dirty.right = std::min(static_cast<LONG>(capture_.image.width), dirty.right + 260);
    dirty.top = std::max(0L, dirty.top);
    dirty.bottom = std::min(static_cast<LONG>(capture_.image.height), dirty.bottom);
    if (IsRectValid(dirty)) {
        InvalidateRect(hwnd_, &dirty, FALSE);
    } else {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

bool OverlayWindow::CaptureTargetWindowFrame(Image& outFrame) {
    outFrame = {};

    if (!HasSelection()) {
        return false;
    }

    RECT finalScreen = SelectionRectNormalized();
    OffsetRect(&finalScreen, capture_.virtualRect.left, capture_.virtualRect.top);
    RECT clipped{};
    if (!IntersectRect(&clipped, &finalScreen, &capture_.virtualRect)) {
        return false;
    }
    if (RectWidth(clipped) <= 2 || RectHeight(clipped) <= 2) {
        return false;
    }

    return ScreenCaptureUtil::CaptureScreenRect(clipped, outFrame);
}

bool ScaleImageBicubic(const Image& src, int dstW, int dstH, Image& out) {
    if (!src.IsValid() || dstW <= 0 || dstH <= 0) {
        return false;
    }
    auto srcBmp = std::make_unique<Gdiplus::Bitmap>(
        src.width, src.height, src.width * 4, PixelFormat32bppARGB,
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(src.bgra.data())));
    if (!srcBmp || srcBmp->GetLastStatus() != Gdiplus::Ok) {
        return false;
    }

    auto dstBmp = std::make_unique<Gdiplus::Bitmap>(dstW, dstH, PixelFormat32bppARGB);
    if (!dstBmp || dstBmp->GetLastStatus() != Gdiplus::Ok) {
        return false;
    }

    {
        Gdiplus::Graphics g(dstBmp.get());
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        Gdiplus::ImageAttributes attrs;
        attrs.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
        g.DrawImage(srcBmp.get(),
            Gdiplus::Rect(0, 0, dstW, dstH),
            0, 0, src.width, src.height,
            Gdiplus::UnitPixel, &attrs);
    }

    out = Image::Create(dstW, dstH);
    if (!out.IsValid()) {
        return false;
    }
    Gdiplus::Rect r(0, 0, dstW, dstH);
    Gdiplus::BitmapData data{};
    if (dstBmp->LockBits(&r, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return false;
    }
    for (int y = 0; y < dstH; ++y) {
        const uint8_t* srcRow = static_cast<const uint8_t*>(data.Scan0) + static_cast<size_t>(data.Stride) * static_cast<size_t>(y);
        uint8_t* dstRow = out.bgra.data() + static_cast<size_t>(dstW) * static_cast<size_t>(y) * 4;
        memcpy(dstRow, srcRow, static_cast<size_t>(dstW) * 4);
    }
    dstBmp->UnlockBits(&data);
    return true;
}

void OverlayWindow::AppendLongCaptureFrame(const Image& frame, int targetOffset) {
    if (!frame.IsValid()) {
        return;
    }

    if (!longCaptureImage_.IsValid() ||
        longCaptureImage_.width != frame.width) {
        longCaptureImage_ = frame;
        longCaptureViewportOffsetY_ = 0;
        return;
    }

    int desiredOffset = targetOffset;
    const int neededTop = std::max(0, -desiredOffset);
    const int neededBottom = std::max(0, desiredOffset + frame.height - longCaptureImage_.height);
    if (neededTop > 0 || neededBottom > 0) {
        const int maxHeight = 28000;
        const int newHeight = longCaptureImage_.height + neededTop + neededBottom;
        if (newHeight <= maxHeight) {
            Image expanded = Image::Create(longCaptureImage_.width, newHeight);
            if (expanded.IsValid()) {
                for (int y = 0; y < longCaptureImage_.height; ++y) {
                    const uint8_t* src = longCaptureImage_.bgra.data() + static_cast<size_t>(y) * static_cast<size_t>(longCaptureImage_.width) * 4;
                    uint8_t* dst = expanded.bgra.data() +
                        static_cast<size_t>(y + neededTop) * static_cast<size_t>(expanded.width) * 4;
                    memcpy(dst, src, static_cast<size_t>(expanded.width) * 4);
                }
                longCaptureImage_ = std::move(expanded);
                desiredOffset += neededTop;
            }
        } else {
            desiredOffset = std::clamp(desiredOffset, 0, std::max(0, longCaptureImage_.height - frame.height));
        }
    }

    desiredOffset = std::clamp(desiredOffset, 0, std::max(0, longCaptureImage_.height - frame.height));
    for (int y = 0; y < frame.height; ++y) {
        const uint8_t* src = frame.bgra.data() + static_cast<size_t>(y) * static_cast<size_t>(frame.width) * 4;
        uint8_t* dst = longCaptureImage_.bgra.data() +
            static_cast<size_t>(desiredOffset + y) * static_cast<size_t>(longCaptureImage_.width) * 4;
        memcpy(dst, src, static_cast<size_t>(frame.width) * 4);
    }
    longCaptureViewportOffsetY_ = desiredOffset;
}

void OverlayWindow::DrawLongCaptureThumbnail(Gdiplus::Graphics& g, float dpiScale) {
    longCaptureThumbRect_.reset();
    if (!longCaptureMode_ || !HasSelection()) {
        return;
    }

    const RECT sr = SelectionRectNormalized();
    const int gap = std::max(8, static_cast<int>(std::round(10.0f * dpiScale)));
    const int thumbW = std::max(140, static_cast<int>(std::round(168.0f * dpiScale)));
    const int thumbH = std::max(120, RectHeight(sr));

    int left = sr.right + gap;
    if (left + thumbW > capture_.image.width - 4) {
        left = sr.left - gap - thumbW;
    }
    left = std::clamp(left, 0, std::max(0, capture_.image.width - thumbW));
    int top = std::clamp(static_cast<int>(sr.top), 0, std::max(0, capture_.image.height - thumbH));

    RECT panel{left, top, left + thumbW, top + thumbH};
    longCaptureThumbRect_ = panel;
    RECT content = panel;
    if (!IsRectValid(content)) {
        return;
    }

    if (!longCaptureThumbCacheReady_ || !longCaptureThumbCache_.image.IsValid()) {
        UpdateLongCaptureThumbnailCache(dpiScale, true);
    } else {
        const bool sizeChanged = (longCaptureThumbCache_.panelW != RectWidth(content)) ||
            (longCaptureThumbCache_.panelH != RectHeight(content));
        if (sizeChanged) {
            longCaptureThumbDirty_ = true;
            UpdateLongCaptureThumbnailCache(dpiScale, true);
        } else {
            UpdateLongCaptureThumbnailCache(dpiScale, false);
        }
    }
    if (!longCaptureThumbCacheReady_ || !longCaptureThumbCache_.image.IsValid()) {
        return;
    }
    const LongCaptureThumbCache& cache = longCaptureThumbCache_;

    const int drawW = cache.drawW;
    const int drawH = cache.drawH;
    if (drawW <= 0 || drawH <= 0 || drawW > RectWidth(content) || drawH > RectHeight(content)) {
        return;
    }
    const bool panelOnRight = panel.left >= sr.right;
    const bool panelOnLeft = panel.right <= sr.left;
    int dx = content.left + (RectWidth(content) - drawW) / 2;
    if (panelOnRight) {
        dx = content.left;
    } else if (panelOnLeft) {
        dx = content.right - drawW;
    }
    const int dy = content.top + (RectHeight(content) - drawH) / 2;
    RECT drawRect{dx, dy, dx + drawW, dy + drawH};

    Gdiplus::Bitmap thumbBmp(drawW, drawH, drawW * 4, PixelFormat32bppARGB,
        const_cast<BYTE*>(cache.image.bgra.data()));
    g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    g.DrawImage(&thumbBmp, drawRect.left, drawRect.top, drawW, drawH);

    Gdiplus::Pen blueBorder(Gdiplus::Color(255, 44, 140, 255), std::max(1.0f, 1.0f * dpiScale));
    g.DrawRectangle(&blueBorder, ToGdiRect(drawRect));

    const double scale = std::max(1e-6, cache.scale);
    const int viewY = drawRect.top + static_cast<int>(std::lround(cache.viewportOffsetY * scale));
    const int viewH = std::max(2, static_cast<int>(std::lround(cache.viewportH * scale)));
    const int viewW = std::max(2, static_cast<int>(std::lround(cache.viewportW * scale)));
    RECT viewRect{
        drawRect.left,
        viewY,
        drawRect.left + std::min(viewW, RectWidth(drawRect)),
        viewY + viewH
    };
    viewRect.left = std::clamp(viewRect.left, drawRect.left, drawRect.right);
    viewRect.right = std::clamp(viewRect.right, drawRect.left, drawRect.right);
    viewRect.top = std::clamp(viewRect.top, drawRect.top, drawRect.bottom);
    viewRect.bottom = std::clamp(viewRect.bottom, drawRect.top, drawRect.bottom);
    if (IsRectValid(viewRect)) {
        const Gdiplus::Color frameColor = longCaptureMatchAccepted_
            ? Gdiplus::Color(255, 54, 220, 96)
            : Gdiplus::Color(255, 246, 206, 62);
        Gdiplus::Pen viewPen(frameColor, std::max(2.0f, 2.8f * dpiScale));
        g.DrawRectangle(&viewPen, ToGdiRect(viewRect));
    }
}

void OverlayWindow::UpdateHoverWindow(POINT screenPt) {
    auto hit = HitWindow(screenPt);
    if (!hit.has_value()) {
        hoverWindowRect_.reset();
        return;
    }
    hoverWindowRect_ = NormalizeRect(*hit);
}

std::optional<RECT> OverlayWindow::HitWindow(POINT screenPt) const {
    RECT localBounds{0, 0, capture_.image.width, capture_.image.height};
    POINT lp = ScreenToLocalPoint(screenPt);
    for (const auto& w : windows_) {
        RECT top = w.rect;
        OffsetRect(&top, -capture_.virtualRect.left, -capture_.virtualRect.top);
        RECT topInter{};
        if (!(IntersectRect(&topInter, &top, &localBounds) && PtInRect(&topInter, lp))) {
            continue;
        }
        return topInter;
    }
    return std::nullopt;
}

HCURSOR OverlayWindow::CursorForHit(HitKind hit) const {
    switch (hit) {
    case HitKind::Left:
    case HitKind::Right:
        return LoadCursorW(nullptr, IDC_SIZEWE);
    case HitKind::Top:
    case HitKind::Bottom:
        return LoadCursorW(nullptr, IDC_SIZENS);
    case HitKind::LeftTop:
    case HitKind::RightBottom:
        return LoadCursorW(nullptr, IDC_SIZENWSE);
    case HitKind::RightTop:
    case HitKind::LeftBottom:
        return LoadCursorW(nullptr, IDC_SIZENESW);
    case HitKind::Inside:
        return LoadCursorW(nullptr, IDC_SIZEALL);
    default:
        return LoadCursorW(nullptr, IDC_CROSS);
    }
}

void OverlayWindow::UpdateCursorVisual(POINT p) {
    if (!hwnd_) {
        return;
    }

    if (longCaptureMode_) {
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        return;
    }

    const bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool noMouseButtons = ((GetKeyState(VK_LBUTTON) & 0x8000) == 0) &&
        ((GetKeyState(VK_RBUTTON) & 0x8000) == 0) &&
        ((GetKeyState(VK_MBUTTON) & 0x8000) == 0);
    if (shiftHeld && dragMode_ == DragMode::None && noMouseButtons) {
        SetCursor(InvisibleCursor());
        return;
    }

    if (dragMode_ == DragMode::None && stage_ == Stage::Annotating && HasSelection() && IsPointInToolbarZone(p, 3)) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return;
    }

    HCURSOR cursor = LoadCursorW(nullptr, IDC_CROSS);
    const float dpiScale = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
    const int borderGrab = std::max(6, static_cast<int>(std::round(kSelectionBorderGrab * dpiScale)));

    if (dragMode_ == DragMode::MoveSelection) {
        cursor = LoadCursorW(nullptr, IDC_SIZEALL);
    } else if (dragMode_ == DragMode::ResizeSelection) {
        cursor = CursorForHit(activeHit_);
    } else if (dragMode_ == DragMode::MoveShape) {
        cursor = LoadCursorW(nullptr, IDC_SIZEALL);
    } else if (dragMode_ == DragMode::ResizeShape) {
        cursor = CursorForHit(activeShapeHit_);
    } else if (stage_ == Stage::Annotating && HasSelection()) {
        RECT sr = SelectionRectNormalized();
        HitKind hit = HitTestRectHandles(sr, p, kHandleSize + 3);
        if (!whiteboardMode_ && hit != HitKind::None && hit != HitKind::Inside) {
            cursor = CursorForHit(hit);
        } else if (hit == HitKind::Inside) {
            const bool canMove = recordingPreviewMode_ ||
                ((GetKeyState(VK_CONTROL) & 0x8000) != 0) || IsNearRectBorder(sr, p, borderGrab);
            cursor = canMove ? LoadCursorW(nullptr, IDC_SIZEALL) : LoadCursorW(nullptr, IDC_CROSS);
        } else {
            cursor = LoadCursorW(nullptr, IDC_NO);
        }
    } else if (stage_ == Stage::Selecting && HasSelection()) {
        HitKind hit = HitTestRectHandles(SelectionRectNormalized(), p, kHandleSize + 3);
        if (hit != HitKind::None) {
            cursor = CursorForHit(hit);
        }
    }

    SetCursor(cursor);
}

bool OverlayWindow::IsCursorFollowUiActiveAt(POINT p) const {
    if (longCaptureMode_ || whiteboardMode_ || recordingPreviewMode_) {
        return false;
    }
    if (!cursorInfoEnabled_ || !HasSelection()) {
        return false;
    }
    if (p.x < 0 || p.y < 0 || p.x >= capture_.image.width || p.y >= capture_.image.height) {
        return false;
    }

    const RECT sr = SelectionRectNormalized();
    if (!PtInRect(&sr, p)) {
        return false;
    }

    if (stage_ == Stage::Annotating && IsPointInToolbarZone(p, 3)) {
        return false;
    }
    return true;
}

bool OverlayWindow::ShouldShowCursorInfoOverlay() const {
    return !screenRecordingMode_ && !recordingPreviewMode_ && followHudEnabled_ && tool_ == ToolType::None;
}

bool OverlayWindow::ShouldShowMagnifierOverlay() const {
    return !screenRecordingMode_ && !recordingPreviewMode_ && followHudEnabled_;
}

bool OverlayWindow::ComputeCrosshairGuideRects(POINT p, RECT& vertical, RECT& horizontal) const {
    if (!IsCursorFollowUiActiveAt(p)) {
        return false;
    }

    RECT sr = SelectionRectNormalized();
    if (!IsRectValid(sr)) {
        return false;
    }

    const float dpiScale = static_cast<float>(GetDpiForWindow(hwnd_) ? GetDpiForWindow(hwnd_) : 96) / 96.0f;
    const int thickness = std::max(2, static_cast<int>(std::ceil(dpiScale * 1.25f)));
    const int half = thickness / 2;
    const int mouseX = static_cast<int>(p.x);
    const int mouseY = static_cast<int>(p.y);

    vertical = RECT{
        std::max(static_cast<int>(sr.left), mouseX - half),
        sr.top,
        std::min(static_cast<int>(sr.right), mouseX - half + thickness),
        sr.bottom
    };
    horizontal = RECT{
        sr.left,
        std::max(static_cast<int>(sr.top), mouseY - half),
        sr.right,
        std::min(static_cast<int>(sr.bottom), mouseY - half + thickness)
    };
    return RectHeight(vertical) > 0 && RectWidth(horizontal) > 0;
}

std::optional<RECT> OverlayWindow::ComputeCursorInfoRect(POINT p) const {
    if (!IsCursorFollowUiActiveAt(p)) {
        return std::nullopt;
    }
    if (dragMode_ != DragMode::None) {
        return std::nullopt;
    }
    if (hasCurrentShape_ && currentShape_.type == ToolType::Pen) {
        return std::nullopt;
    }

    const float dpiScale = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
    const int boxW = std::max(150, static_cast<int>(std::round(188.0f * dpiScale)));
    const int boxH = std::max(90, static_cast<int>(std::round(112.0f * dpiScale)));
    const int offset = std::max(10, static_cast<int>(std::round(16.0f * dpiScale)));

    auto makeRect = [&](int left, int top) -> RECT {
        RECT out{};
        out.left = std::clamp(left, 0, std::max(0, capture_.image.width - boxW));
        out.top = std::clamp(top, 0, std::max(0, capture_.image.height - boxH));
        out.right = out.left + boxW;
        out.bottom = out.top + boxH;
        return out;
    };

    std::array<RECT, 8> candidates{
        makeRect(p.x + offset, p.y + offset),
        makeRect(p.x + offset, p.y - offset - boxH),
        makeRect(p.x - offset - boxW, p.y + offset),
        makeRect(p.x - offset - boxW, p.y - offset - boxH),
        makeRect(p.x - boxW / 2, p.y + offset),
        makeRect(p.x - boxW / 2, p.y - offset - boxH),
        makeRect(p.x + offset, p.y - boxH / 2),
        makeRect(p.x - offset - boxW, p.y - boxH / 2)
    };

    for (const RECT& c : candidates) {
        if (!PtInRect(&c, p)) {
            return c;
        }
    }
    return candidates[0];
}

std::optional<RECT> OverlayWindow::ComputeMagnifierRect(POINT p) const {
    auto infoOpt = ComputeCursorInfoRect(p);
    if (!infoOpt.has_value()) {
        return std::nullopt;
    }

    const RECT info = *infoOpt;
    const float dpiScale = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
    const int gap = 0;
    const int magH = std::max(98, static_cast<int>(std::round(122.0f * dpiScale)));
    const int magW = std::max(150, RectWidth(info));
    auto makeRect = [&](int left, int top) -> RECT {
        RECT out{};
        out.left = std::clamp(left, 0, std::max(0, capture_.image.width - magW));
        out.top = std::clamp(top, 0, std::max(0, capture_.image.height - magH));
        out.right = out.left + magW;
        out.bottom = out.top + magH;
        return out;
    };

    std::array<RECT, 4> candidates{
        makeRect(info.left, info.top - gap - magH),
        makeRect(info.left, info.bottom + gap),
        makeRect(info.left - gap - magW, info.top),
        makeRect(info.right + gap, info.top)
    };

    auto overlapArea = [](const RECT& a, const RECT& b) -> int64_t {
        RECT inter{};
        if (!IntersectRect(&inter, &a, &b)) {
            return 0;
        }
        return static_cast<int64_t>(std::max(0, RectWidth(inter))) *
            static_cast<int64_t>(std::max(0, RectHeight(inter)));
    };

    for (const RECT& c : candidates) {
        if (overlapArea(c, info) == 0) {
            return c;
        }
    }

    int bestIdx = 0;
    int64_t bestOverlap = overlapArea(candidates[0], info);
    for (int i = 1; i < static_cast<int>(candidates.size()); ++i) {
        const int64_t ov = overlapArea(candidates[i], info);
        if (ov < bestOverlap) {
            bestOverlap = ov;
            bestIdx = i;
        }
    }
    return candidates[bestIdx];
}

bool OverlayWindow::IsPointInToolbarZone(POINT p, int inflatePx) const {
    if (!toolbar_.Hwnd() || !IsWindowVisible(toolbar_.Hwnd())) {
        return false;
    }

    POINT screenPt = LocalToScreenPoint(p);
    RECT tr{};
    if (!GetWindowRect(toolbar_.Hwnd(), &tr)) {
        return false;
    }
    if (inflatePx > 0) {
        InflateRect(&tr, inflatePx, inflatePx);
    }
    return PtInRect(&tr, screenPt) != FALSE;
}

bool OverlayWindow::BeginTextEdit(POINT p) {
    if (!hwnd_ || !HasSelection()) {
        return false;
    }

    EndTextEdit(true);

    RECT sr = SelectionRectNormalized();
    if (!PtInRect(&sr, p)) {
        return false;
    }

    const float dpiScale = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
    const float textPixelSize = LogicalTextSizeToPixels(textSize_, p);
    int height = std::max(28, static_cast<int>(std::round((textSize_ + 12.0f) * dpiScale)));
    HDC measureDc = GetDC(hwnd_);
    if (measureDc) {
        UiGdi::ScopedGdiObject<HFONT> initialFont(CreateOverlayTextFontHandle(textPixelSize, textStyle_));
        const TextLayoutMetrics initialMetrics = MeasureTextLayout(measureDc, initialFont.Get(), std::wstring_view{});
        ReleaseDC(hwnd_, measureDc);
        if (initialMetrics.contentHeight > 0) {
            height = std::max(kTextEditMinHeight, initialMetrics.contentHeight + kTextEditPaddingY * 2);
        }
    }
    const int width = std::max(140, static_cast<int>(std::round(220.0f * dpiScale)));
    const POINT anchor{ p.x, p.y - height / 2 };
    RECT rc = FitTextRectToSelection(sr, anchor, width, height);

    textDraftShape_ = {};
    textDraftShape_.type = ToolType::Text;
    textDraftShape_.color = textColor_;
    textDraftShape_.stroke = strokeWidth_;
    textDraftShape_.textSize = textPixelSize;
    textDraftShape_.textStyle = textStyle_;
    textDraftShape_.rect = rc;
    textEditing_ = true;
    textEditRect_ = rc;

    textEdit_ = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        WS_CHILD | WS_TABSTOP | ES_MULTILINE | ES_WANTRETURN | ES_AUTOHSCROLL | ES_AUTOVSCROLL,
        rc.left, rc.top, RectWidth(rc), RectHeight(rc),
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_OVERLAY_TEXT_EDIT)),
        hInstance_,
        nullptr
    );
    if (!textEdit_) {
        textEditing_ = false;
        return false;
    }

    SetWindowSubclass(textEdit_, TextEditSubclassProc, 1, reinterpret_cast<DWORD_PTR>(hwnd_));
    UpdateTextEditFont();
    ShowWindow(textEdit_, SW_SHOW);
    SetFocus(textEdit_);
    SendMessageW(textEdit_, EM_SETSEL, 0, -1);

    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void OverlayWindow::DestroyTextEditControl() {
    if (textEditFont_) {
        DeleteObject(textEditFont_);
        textEditFont_ = nullptr;
    }
    DestroyTextEditBackground();
    if (textEdit_) {
        RemoveWindowSubclass(textEdit_, TextEditSubclassProc, 1);
        DestroyWindow(textEdit_);
        textEdit_ = nullptr;
    }
}

void OverlayWindow::DestroyTextEditBackground() {
    UiGdi::ResetGdiObject(textEditBgBrush_);
    UiGdi::ResetGdiObject(textEditBgBitmap_);
}

void OverlayWindow::UpdateTextEditFont() {
    if (!textEdit_) {
        return;
    }
    if (textEditFont_) {
        DeleteObject(textEditFont_);
        textEditFont_ = nullptr;
    }
    textEditFont_ = CreateOverlayTextFontHandle(textDraftShape_.textSize, textDraftShape_.textStyle);
    SendMessageW(textEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(textEditFont_), TRUE);
    ResizeTextEditToFit();
}

void OverlayWindow::UpdateTextEditBackground() {
    if (!textEdit_ || !hwnd_) {
        DestroyTextEditBackground();
        return;
    }

    const int width = std::max(1, RectWidth(textEditRect_));
    const int height = std::max(1, RectHeight(textEditRect_));
    if (width <= 0 || height <= 0) {
        DestroyTextEditBackground();
        return;
    }

    EnsureStaticSceneBitmap();
    if (!staticSceneBitmap_) {
        DestroyTextEditBackground();
        return;
    }

    Gdiplus::Bitmap backgroundBitmap(width, height, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics bgGraphics(&backgroundBitmap);
        bgGraphics.SetCompositingQuality(Gdiplus::CompositingQualityHighSpeed);
        bgGraphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        bgGraphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        bgGraphics.DrawImage(staticSceneBitmap_.get(),
            Gdiplus::Rect(0, 0, width, height),
            textEditRect_.left, textEditRect_.top, width, height, Gdiplus::UnitPixel);
    }

    HBITMAP bitmap = nullptr;
    if (backgroundBitmap.GetHBITMAP(Gdiplus::Color(255, 0, 0, 0), &bitmap) != Gdiplus::Ok || !bitmap) {
        DestroyTextEditBackground();
        return;
    }

    UiGdi::ScopedGdiObject<HBITMAP> newBitmap(bitmap);
    UiGdi::ScopedGdiObject<HBRUSH> newBrush(CreatePatternBrush(newBitmap.Get()));
    if (!newBrush) {
        DestroyTextEditBackground();
        return;
    }

    DestroyTextEditBackground();
    textEditBgBitmap_ = newBitmap.Release();
    textEditBgBrush_ = newBrush.Release();
}

void OverlayWindow::ResizeTextEditToFit() {
    if (!textEdit_ || !hwnd_ || !HasSelection()) {
        return;
    }

    const int textLen = GetWindowTextLengthW(textEdit_);
    std::wstring text;
    if (textLen > 0) {
        std::vector<wchar_t> buffer(static_cast<size_t>(textLen) + 1, L'\0');
        GetWindowTextW(textEdit_, buffer.data(), textLen + 1);
        text.assign(buffer.data());
    }

    HDC hdc = GetDC(textEdit_);
    if (!hdc) {
        return;
    }
    const TextLayoutMetrics metrics = MeasureTextLayout(hdc, textEditFont_, text);
    ReleaseDC(textEdit_, hdc);

    const RECT sr = SelectionRectNormalized();
    const POINT anchor{ textEditRect_.left, textEditRect_.top };
    const RECT fitted = FitTextRectToSelection(sr, anchor,
        metrics.contentWidth + kTextEditPaddingX * 2,
        metrics.contentHeight + kTextEditPaddingY * 2);
    const bool rectChanged =
        textEditRect_.left != fitted.left || textEditRect_.top != fitted.top ||
        textEditRect_.right != fitted.right || textEditRect_.bottom != fitted.bottom;
    textEditRect_ = fitted;
    textDraftShape_.rect = fitted;
    if (!rectChanged && textEditBgBrush_) {
        return;
    }

    UpdateTextEditBackground();
    if (rectChanged) {
        MoveWindow(textEdit_, fitted.left, fitted.top, RectWidth(fitted), RectHeight(fitted), TRUE);
        ApplyTextEditFormattingRect(textEdit_);
    } else {
        ApplyTextEditFormattingRect(textEdit_);
        InvalidateRect(textEdit_, nullptr, TRUE);
    }
}

void OverlayWindow::EndTextEdit(bool commit) {
    if (!textEditing_) {
        return;
    }

    RECT rc = textEditRect_;
    std::wstring text;
    if (textEdit_) {
        RECT wr{};
        GetWindowRect(textEdit_, &wr);
        MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&wr), 2);
        rc = wr;

        const int len = GetWindowTextLengthW(textEdit_);
        if (len > 0) {
            std::vector<wchar_t> buf(static_cast<size_t>(len) + 1, L'\0');
            GetWindowTextW(textEdit_, buf.data(), len + 1);
            text.assign(buf.data());
        }
    }

    DestroyTextEditControl();

    if (commit) {
        const size_t first = text.find_first_not_of(L" \t\r\n");
        if (first != std::wstring::npos) {
            const size_t last = text.find_last_not_of(L" \t\r\n");
            text = text.substr(first, last - first + 1);

            textDraftShape_.text = text;
            textDraftShape_.rect = NormalizeRect(rc);
            textDraftShape_.color = textColor_;
            textDraftShape_.textStyle = textStyle_;
            PushUndo();
            shapes_.push_back(textDraftShape_);
            selectedShape_ = static_cast<int>(shapes_.size()) - 1;
            MarkSceneDirty();
        }
    }

    textEditing_ = false;
    textDraftShape_ = {};
    textEditRect_ = RECT{};
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool OverlayWindow::SaveSelectionWithoutHistory() {
    Image composed = whiteboardMode_ ? ComposeBackgroundSelection() : ComposeCurrent();
    if (!composed.IsValid()) {
        return false;
    }

    const auto defaultPath = DefaultSelectionSavePath();
    const std::wstring defaultName = defaultPath.filename().wstring();
    const std::wstring initialDir = defaultPath.parent_path().wstring();
    wchar_t path[MAX_PATH]{};
    wcsncpy_s(path, _countof(path), defaultName.c_str(), _TRUNCATE);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"PNG (*.png)\0*.png\0JPEG (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = initialDir.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"png";

    if (!GetSaveFileNameW(&ofn)) {
        return false;
    }

    std::filesystem::path output = path;
    const auto ext = output.extension().wstring();
    const bool jpeg = (_wcsicmp(ext.c_str(), L".jpg") == 0 || _wcsicmp(ext.c_str(), L".jpeg") == 0);
    if (!exporter_.SaveImage(composed, output, jpeg)) {
        MessageBoxW(hwnd_, L"保存图像失败", L"SnapPin", MB_ICONERROR);
        return false;
    }
    return true;
}

bool OverlayWindow::HasSelection() const {
    RECT n = NormalizeRect(selection_);
    return RectWidth(n) >= kMinSelection && RectHeight(n) >= kMinSelection;
}

RECT OverlayWindow::SelectionRectNormalized() const {
    return NormalizeRect(selection_);
}

void OverlayWindow::ClampSelectionToBounds() {
    RECT bounds{0, 0, capture_.image.width, capture_.image.height};
    selection_ = NormalizeRect(selection_);
    selection_.left = std::clamp(selection_.left, bounds.left, static_cast<LONG>(bounds.right - 1));
    selection_.top = std::clamp(selection_.top, bounds.top, static_cast<LONG>(bounds.bottom - 1));
    selection_.right = std::clamp(selection_.right, static_cast<LONG>(selection_.left + 1), static_cast<LONG>(bounds.right));
    selection_.bottom = std::clamp(selection_.bottom, static_cast<LONG>(selection_.top + 1), static_cast<LONG>(bounds.bottom));
}

OverlayWindow::HitKind OverlayWindow::HitTestRectHandles(const RECT& rc, POINT p, int handleSize) const {
    RECT n = NormalizeRect(rc);
    if (!PtInRectInclusive(InflateRectCopy(n, handleSize), p)) {
        return HitKind::None;
    }

    auto isNear = [&](int x, int y) {
        RECT h{x - handleSize, y - handleSize, x + handleSize, y + handleSize};
        return PtInRect(&h, p);
    };

    const int cx = (n.left + n.right) / 2;
    const int cy = (n.top + n.bottom) / 2;
    if (isNear(n.left, n.top)) return HitKind::LeftTop;
    if (isNear(n.right, n.top)) return HitKind::RightTop;
    if (isNear(n.left, n.bottom)) return HitKind::LeftBottom;
    if (isNear(n.right, n.bottom)) return HitKind::RightBottom;
    if (isNear(cx, n.top)) return HitKind::Top;
    if (isNear(cx, n.bottom)) return HitKind::Bottom;
    if (isNear(n.left, cy)) return HitKind::Left;
    if (isNear(n.right, cy)) return HitKind::Right;
    if (PtInRect(&n, p)) return HitKind::Inside;
    return HitKind::None;
}

int OverlayWindow::HitTestShape(POINT p, HitKind* outHit) const {
    if (!HasSelection()) {
        return -1;
    }
    for (int i = static_cast<int>(shapes_.size()) - 1; i >= 0; --i) {
        RECT b = ShapeBounds(shapes_[i]);
        HitKind hit = HitTestRectHandles(b, p, 6);
        if (hit != HitKind::None) {
            if (outHit) {
                *outHit = hit;
            }
            return i;
        }
    }
    if (outHit) {
        *outHit = HitKind::None;
    }
    return -1;
}

void OverlayWindow::MoveShape(AnnotationShape& shape, int dx, int dy) {
    if (shape.type == ToolType::Line || shape.type == ToolType::Arrow || shape.type == ToolType::Pen) {
        for (auto& p : shape.points) {
            p.x += dx;
            p.y += dy;
        }
    }
    shape.rect.left += dx;
    shape.rect.right += dx;
    shape.rect.top += dy;
    shape.rect.bottom += dy;
}

void OverlayWindow::ResizeShape(AnnotationShape& shape, HitKind hit, POINT, POINT current, bool keepRatio) {
    RECT sr = SelectionRectNormalized();
    POINT rp{
        std::clamp(static_cast<int>(current.x), static_cast<int>(sr.left), static_cast<int>(sr.right)),
        std::clamp(static_cast<int>(current.y), static_cast<int>(sr.top), static_cast<int>(sr.bottom))
    };
    RECT b = ShapeBounds(shape);

    switch (hit) {
    case HitKind::Left: b.left = rp.x; break;
    case HitKind::Top: b.top = rp.y; break;
    case HitKind::Right: b.right = rp.x; break;
    case HitKind::Bottom: b.bottom = rp.y; break;
    case HitKind::LeftTop: b.left = rp.x; b.top = rp.y; break;
    case HitKind::RightTop: b.right = rp.x; b.top = rp.y; break;
    case HitKind::LeftBottom: b.left = rp.x; b.bottom = rp.y; break;
    case HitKind::RightBottom: b.right = rp.x; b.bottom = rp.y; break;
    default: break;
    }

    b = NormalizeRect(b);
    if (keepRatio) {
        const int side = std::max(RectWidth(b), RectHeight(b));
        b.right = b.left + side;
        b.bottom = b.top + side;
    }

    if (shape.type == ToolType::Line || shape.type == ToolType::Arrow) {
        if (shape.points.size() >= 2) {
            shape.points[0] = MakePoint(b.left, b.top);
            shape.points[1] = MakePoint(b.right, b.bottom);
        }
    } else if (shape.type == ToolType::Pen) {
        const RECT old = selectedShapeBounds_;
        const float sx = std::max(0.01f, static_cast<float>(RectWidth(b)) / std::max(1, RectWidth(old)));
        const float sy = std::max(0.01f, static_cast<float>(RectHeight(b)) / std::max(1, RectHeight(old)));
        for (auto& p : shape.points) {
            p.x = b.left + static_cast<int>((p.x - old.left) * sx);
            p.y = b.top + static_cast<int>((p.y - old.top) * sy);
        }
    }

    shape.rect = b;
}

void OverlayWindow::CommitCurrentShape() {
    if (!hasCurrentShape_) {
        return;
    }

    if (currentShape_.type == ToolType::Pen) {
        if (currentShape_.points.size() >= 2) {
            shapes_.push_back(currentShape_);
        }
    } else {
        RECT r = NormalizeRect(currentShape_.rect);
        if (RectWidth(r) >= 2 && RectHeight(r) >= 2) {
            currentShape_.rect = r;
            shapes_.push_back(currentShape_);
        }
    }

    hasCurrentShape_ = false;
    currentShape_ = {};
    selectedShape_ = static_cast<int>(shapes_.size()) - 1;
    MarkSceneDirty();
}

void OverlayWindow::PushUndo() {
    undoStack_.push_back(shapes_);
    if (undoStack_.size() > kMaxUndo) {
        undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear();
}

void OverlayWindow::Undo() {
    if (undoStack_.empty()) {
        return;
    }
    redoStack_.push_back(shapes_);
    shapes_ = undoStack_.back();
    undoStack_.pop_back();
    selectedShape_ = -1;
    MarkSceneDirty();
}

void OverlayWindow::Redo() {
    if (redoStack_.empty()) {
        return;
    }
    undoStack_.push_back(shapes_);
    shapes_ = redoStack_.back();
    redoStack_.pop_back();
    selectedShape_ = -1;
    MarkSceneDirty();
}

RECT OverlayWindow::ScreenToLocalRect(const RECT& rc) const {
    RECT out = rc;
    OffsetRect(&out, -capture_.virtualRect.left, -capture_.virtualRect.top);
    return out;
}

POINT OverlayWindow::ScreenToLocalPoint(POINT p) const {
    p.x -= capture_.virtualRect.left;
    p.y -= capture_.virtualRect.top;
    return p;
}

POINT OverlayWindow::LocalToScreenPoint(POINT p) const {
    p.x += capture_.virtualRect.left;
    p.y += capture_.virtualRect.top;
    return p;
}

void OverlayWindow::SnapCursorToCrosshair() {
    if (!hwnd_ || !HasSelection()) {
        return;
    }

    RECT sr = SelectionRectNormalized();
    POINT p = lastMouse_;
    p.x = std::clamp(p.x, sr.left, std::max(sr.left, sr.right - 1));
    p.y = std::clamp(p.y, sr.top, std::max(sr.top, sr.bottom - 1));
    lastMouse_ = p;
    POINT screenPt = LocalToScreenPoint(p);
    SetCursorPos(screenPt.x, screenPt.y);
}

Image OverlayWindow::ComposeCurrent() const {
    if (!HasSelection()) {
        return {};
    }
    const RECT sr = SelectionRectNormalized();
    std::vector<AnnotationShape> translated;
    translated.reserve(shapes_.size());

    for (const auto& s : shapes_) {
        RECT bounds = ShapeBounds(s);
        RECT inter{};
        if (!IntersectRect(&inter, &bounds, &sr)) {
            continue;
        }

        AnnotationShape t = s;
        OffsetRect(&t.rect, -sr.left, -sr.top);
        for (auto& p : t.points) {
            p.x -= sr.left;
            p.y -= sr.top;
        }
        translated.push_back(std::move(t));
    }
    return exporter_.Compose(capture_.image, sr, translated);
}

Image OverlayWindow::ComposeBackgroundSelection() const {
    if (!HasSelection()) {
        return {};
    }
    const RECT sr = SelectionRectNormalized();
    std::vector<AnnotationShape> translated;
    translated.reserve(shapes_.size());

    for (const auto& s : shapes_) {
        RECT bounds = ShapeBounds(s);
        RECT inter{};
        if (!IntersectRect(&inter, &bounds, &sr)) {
            continue;
        }

        AnnotationShape t = s;
        OffsetRect(&t.rect, -sr.left, -sr.top);
        for (auto& p : t.points) {
            p.x -= sr.left;
            p.y -= sr.top;
        }
        translated.push_back(std::move(t));
    }

    Image whiteBase = capture_.image;
    if (!whiteBase.IsValid()) {
        return {};
    }
    for (int y = sr.top; y < sr.bottom; ++y) {
        uint8_t* row = whiteBase.bgra.data() +
            (static_cast<size_t>(y) * static_cast<size_t>(whiteBase.width) + static_cast<size_t>(sr.left)) * 4;
        for (int x = sr.left; x < sr.right; ++x) {
            row[0] = 255;
            row[1] = 255;
            row[2] = 255;
            row[3] = 255;
            row += 4;
        }
    }
    return exporter_.Compose(whiteBase, sr, translated);
}

void OverlayWindow::Finish(OverlayAction action) {
    const bool finishingLongCapture = longCaptureMode_;
    const bool finishingWhiteboard = whiteboardMode_;
    const bool finishingScreenRecording = screenRecordingMode_;
    if (precisionModeActive_ || (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        SnapCursorToCrosshair();
        precisionModeActive_ = false;
    }
    if (finishingLongCapture) {
        StopWindowTimer(longCaptureTimer_);
        longCaptureMode_ = false;
        longCaptureTargetHwnd_ = nullptr;
        longCaptureScrollDir_ = 0;
        longCaptureMatchAccepted_ = true;
        longCaptureThumbRect_.reset();
        toolbar_.SetLongCaptureMode(false);
        longCaptureThumbCacheReady_ = false;
        longCaptureThumbDirty_ = false;
        longCaptureThumbLastRenderTick_ = 0;
        longCaptureThumbCache_ = {};
    }
    if (finishingWhiteboard) {
        whiteboardMode_ = false;
        toolbar_.SetWhiteboardMode(false);
    }
    if (finishingScreenRecording) {
        screenRecordingMode_ = false;
        recordingActive_ = false;
        recordingPaused_ = false;
        toolbar_.SetScreenRecordingMode(false);
        toolbar_.SetRecordingState(false, false);
    }

    if (action == OverlayAction::Cancel) {
        EndTextEdit(false);
    } else {
        EndTextEdit(true);
    }

    OverlayResult result;
    result.action = action;
    if (action != OverlayAction::Cancel) {
        if (finishingLongCapture && longCaptureImage_.IsValid()) {
            result.image = longCaptureImage_;
            if (HasSelection()) {
                RECT sr = SelectionRectNormalized();
                OffsetRect(&sr, capture_.virtualRect.left, capture_.virtualRect.top);
                sr.right = sr.left + longCaptureImage_.width;
                sr.bottom = sr.top + longCaptureImage_.height;
                result.selectionScreenRect = sr;
            }
        } else {
            result.image = finishingWhiteboard ? ComposeBackgroundSelection() : ComposeCurrent();
            if (HasSelection()) {
                RECT sr = SelectionRectNormalized();
                OffsetRect(&sr, capture_.virtualRect.left, capture_.virtualRect.top);
                result.selectionScreenRect = sr;
            }
        }
    }

    auto cb = callback_;
    Close();
    if (cb) {
        cb(result);
    }
}


