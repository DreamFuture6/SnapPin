#include "ui/HistoryWindow.h"

#include "ui/GdiObject.h"
#include "ui/GdiResourceCache.h"

namespace
{
    constexpr UINT ID_HISTORY_LIST  = 60001;
    constexpr UINT ID_HISTORY_COPY  = 60002;
    constexpr UINT ID_HISTORY_PIN   = 60003;
    constexpr UINT ID_HISTORY_CLEAR = 60005;
    constexpr UINT ID_HISTORY_TITLE = 60006;

    std::wstring SizeText(int w, int h)
    {
        std::wstringstream ss;
        ss << w << L" x " << h;
        return ss.str();
    }

    std::wstring FileNameFromPath(const std::wstring &path)
    {
        std::filesystem::path p(path);
        return p.filename().wstring();
    }

    std::wstring FileStemFromPath(const std::wstring &path)
    {
        std::filesystem::path p(path);
        return p.stem().wstring();
    }

    std::wstring FileSizeText(const std::wstring &path)
    {
        std::error_code ec;
        const std::filesystem::path p(path);
        const auto size      = std::filesystem::file_size(p, ec);
        uintmax_t actualSize = size;
        if (ec || size == static_cast<uintmax_t>(-1)) {
            std::ifstream in(p, std::ios::binary | std::ios::ate);
            if (!in.is_open()) {
                return L"--";
            }
            actualSize = static_cast<uintmax_t>(in.tellg());
        }

        constexpr double kKB = 1024.0;
        constexpr double kMB = 1024.0 * 1024.0;
        std::wstringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(2);
        if (actualSize >= static_cast<uintmax_t>(kMB)) {
            ss << (static_cast<double>(actualSize) / kMB) << L" MB";
        } else {
            ss << std::max(0.01, static_cast<double>(actualSize) / kKB) << L" KB";
        }
        return ss.str();
    }

    void SetCtrlFont(HWND hwnd, HFONT font)
    {
        if (hwnd && font) {
            SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    }

    int FitTextChars(HDC hdc, const std::wstring &text, int maxWidth)
    {
        if (!hdc || text.empty() || maxWidth <= 0) {
            return 0;
        }
        int fit = 0;
        SIZE size{};
        if (!GetTextExtentExPointW(hdc, text.c_str(), static_cast<int>(text.size()), maxWidth, &fit, nullptr, &size)) {
            return static_cast<int>(text.size());
        }
        return std::clamp(fit, 0, static_cast<int>(text.size()));
    }

    std::wstring EllipsizeToWidth(HDC hdc, const std::wstring &text, int maxWidth)
    {
        if (text.empty()) {
            return {};
        }
        const int fullFit = FitTextChars(hdc, text, maxWidth);
        if (fullFit >= static_cast<int>(text.size())) {
            return text;
        }

        const std::wstring ellipsis = L"...";
        SIZE ellipsisSize{};
        GetTextExtentPoint32W(hdc, ellipsis.c_str(), static_cast<int>(ellipsis.size()), &ellipsisSize);
        if (ellipsisSize.cx >= maxWidth) {
            return ellipsis;
        }

        const int prefixFit = FitTextChars(hdc, text, maxWidth - ellipsisSize.cx);
        if (prefixFit <= 0) {
            return ellipsis;
        }
        return text.substr(0, static_cast<size_t>(prefixFit)) + ellipsis;
    }

    std::wstring FormatPathTwoLines(HWND pathLabel, const std::wstring &fullPathText)
    {
        if (!pathLabel || fullPathText.empty()) {
            return fullPathText;
        }

        RECT rc{};
        GetClientRect(pathLabel, &rc);
        const int maxWidth = std::max(1, RectWidth(rc) - 2);
        if (maxWidth <= 1) {
            return fullPathText;
        }

        HDC hdc = GetDC(pathLabel);
        if (!hdc) {
            return fullPathText;
        }

        HFONT font = reinterpret_cast<HFONT>(SendMessageW(pathLabel, WM_GETFONT, 0, 0));
        UiGdi::ScopedSelectObject fontSelection(hdc, font);

        const int fullFit = FitTextChars(hdc, fullPathText, maxWidth);
        if (fullFit >= static_cast<int>(fullPathText.size())) {
            ReleaseDC(pathLabel, hdc);
            return fullPathText;
        }

        int firstLineFit    = std::max(1, fullFit);
        std::wstring line1  = fullPathText.substr(0, static_cast<size_t>(firstLineFit));
        std::wstring remain = fullPathText.substr(static_cast<size_t>(firstLineFit));
        const int secondFit = FitTextChars(hdc, remain, maxWidth);
        if (secondFit >= static_cast<int>(remain.size())) {
            ReleaseDC(pathLabel, hdc);
            return line1 + L"\r\n" + remain;
        }

        const int secondLineFit = std::max(1, secondFit);
        std::wstring line2      = remain.substr(0, static_cast<size_t>(secondLineFit));
        std::wstring remain2    = remain.substr(static_cast<size_t>(secondLineFit));
        std::wstring line3      = EllipsizeToWidth(hdc, remain2, maxWidth);
        ReleaseDC(pathLabel, hdc);

        return line1 + L"\r\n" + line2 + L"\r\n" + line3;
    }
}

bool HistoryWindow::Show(HINSTANCE hInstance, const std::vector<HistoryItem> &items, ActionCallback callback)
{
    items_    = items;
    callback_ = std::move(callback);

    if (hwnd_) {
        deferPreviewUntilFlyoutShown_ = (IsWindowVisible(hwnd_) == FALSE);
        if (deferPreviewUntilFlyoutShown_) {
            previewBitmap_.reset();
        }
        RefreshList();
        ShowFlyout();
        return true;
    }

    hInstance_ = hInstance;

    WNDCLASSW wc{};
    wc.lpfnWndProc   = HistoryWindow::WndProc;
    wc.hInstance     = hInstance_;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"SnapPinHistoryWindowClass";
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        wc.lpszClassName,
        L"SnapPin 历史记录",
        WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1020,
        680,
        nullptr,
        nullptr,
        hInstance_,
        this);

    if (!hwnd_) {
        return false;
    }

    list_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWW,
        L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 100, 100,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_HISTORY_LIST)),
        hInstance_,
        nullptr);

    ListView_SetExtendedListViewStyleEx(
        list_,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    col.pszText = const_cast<wchar_t *>(L"时间");
    col.cx      = 170;
    ListView_InsertColumn(list_, 0, &col);

    col.pszText  = const_cast<wchar_t *>(L"图片名");
    col.cx       = 190;
    col.iSubItem = 1;
    ListView_InsertColumn(list_, 1, &col);

    col.pszText  = const_cast<wchar_t *>(L"尺寸");
    col.cx       = 110;
    col.iSubItem = 2;
    ListView_InsertColumn(list_, 2, &col);

    editTitle_ = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 100, 30,
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_HISTORY_TITLE)),
        hInstance_,
        nullptr);
    lblMeta_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 22, hwnd_, nullptr, hInstance_, nullptr);
    lblPath_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 100, 40, hwnd_, nullptr, hInstance_, nullptr);

    btnCopy_  = CreateWindowW(L"BUTTON", L"复制", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              0, 0, 100, 32, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_HISTORY_COPY)), hInstance_, nullptr);
    btnPin_   = CreateWindowW(L"BUTTON", L"贴图", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              0, 0, 100, 32, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_HISTORY_PIN)), hInstance_, nullptr);
    btnClear_ = CreateWindowW(L"BUTTON", L"清空历史", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              0, 0, 100, 32, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_HISTORY_CLEAR)), hInstance_, nullptr);

    UpdateDpiFonts();

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    LayoutChildren(RectWidth(rc), RectHeight(rc));
    deferPreviewUntilFlyoutShown_ = true;
    RefreshList();

    ShowFlyout();
    return true;
}

void HistoryWindow::Close()
{
    if (flyoutTimer_ != 0 && hwnd_) {
        KillTimer(hwnd_, flyoutTimer_);
        flyoutTimer_ = 0;
    }
    flyoutHiding_ = false;

    if (titleFont_) {
        UiGdi::ResetGdiObject(titleFont_);
    }
    if (normalFont_) {
        UiGdi::ResetGdiObject(normalFont_);
    }
    if (btnFont_) {
        UiGdi::ResetGdiObject(normalFont_);
    }

    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void HistoryWindow::Hide()
{
    HideFlyout();
}

void HistoryWindow::ReleasePreview()
{
    previewBitmap_.reset();
    if (hwnd_ && IsRectValid(previewRect_)) {
        InvalidateRect(hwnd_, &previewRect_, FALSE);
    }
}

LRESULT CALLBACK HistoryWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    HistoryWindow *self = reinterpret_cast<HistoryWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
        self     = reinterpret_cast<HistoryWindow *>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT HistoryWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        LayoutChildren(LOWORD(lParam), HIWORD(lParam));
        ApplyFlyoutClip();
        return 0;
    case WM_DPICHANGED: {
        currentDpi_ = HIWORD(wParam);
        auto *rc    = reinterpret_cast<RECT *>(lParam);
        if (rc) {
            SetWindowPos(hwnd_, nullptr, rc->left, rc->top, RectWidth(*rc), RectHeight(*rc),
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        UpdateDpiFonts();
        RECT client{};
        GetClientRect(hwnd_, &client);
        LayoutChildren(RectWidth(client), RectHeight(client));
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd_, &ps);
        DrawPreview(hdc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HWND ctrl = reinterpret_cast<HWND>(lParam);
        if (ctrl == lblMeta_ || ctrl == lblPath_) {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, RGB(46, 52, 64));
            SetBkColor(hdc, RGB(246, 248, 251));
            static HBRUSH sInfoBg = CreateSolidBrush(RGB(246, 248, 251));
            return reinterpret_cast<LRESULT>(sInfoBg);
        }
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
    case WM_TIMER:
        if (flyoutTimer_ != 0 && wParam == flyoutTimer_) {
            StepFlyoutAnimation();
            return 0;
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            HideFlyout();
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            HideFlyout();
            return 0;
        }
        return 0;
    case WM_NOTIFY: {
        auto *hdr = reinterpret_cast<NMHDR *>(lParam);
        if (!hdr) {
            return 0;
        }

        if (hdr->idFrom == ID_HISTORY_LIST) {
            if (hdr->code == LVN_ITEMCHANGED) {
                auto *n = reinterpret_cast<NMLISTVIEW *>(lParam);
                if ((n->uChanged & LVIF_STATE) != 0) {
                    UpdateSelection();
                }
            } else if (hdr->code == NM_DBLCLK) {
                int idx = SelectedIndex();
                if (idx >= 0 && idx < static_cast<int>(items_.size()) && callback_) {
                    callback_(Action::OpenFile, items_[idx], L"");
                }
            }
        }
        return 0;
    }
    case WM_COMMAND: {
        const UINT id = LOWORD(wParam);
        if (id == ID_HISTORY_CLEAR && callback_) {
            callback_(Action::ClearAll, std::nullopt, L"");
            return 0;
        }

        if (id == ID_HISTORY_TITLE && HIWORD(wParam) == EN_KILLFOCUS && callback_) {
            const int idx = SelectedIndex();
            if (idx >= 0 && idx < static_cast<int>(items_.size())) {
                wchar_t buf[MAX_PATH]{};
                GetWindowTextW(editTitle_, buf, static_cast<int>(std::size(buf)));
                callback_(Action::Rename, items_[idx], buf);
            }
            return 0;
        }

        const int idx = SelectedIndex();
        if (idx < 0 || idx >= static_cast<int>(items_.size()) || !callback_) {
            return 0;
        }
        const HistoryItem &item = items_[idx];

        if (id == ID_HISTORY_COPY) {
            callback_(Action::Copy, item, L"");
        } else if (id == ID_HISTORY_PIN) {
            callback_(Action::Pin, item, L"");
        }
        return 0;
    }
    case WM_CLOSE:
        HideFlyout();
        return 0;
    case WM_DESTROY:
        if (flyoutTimer_ != 0) {
            KillTimer(hwnd_, flyoutTimer_);
            flyoutTimer_ = 0;
        }
        flyoutHiding_ = false;
        hwnd_         = nullptr;
        list_         = nullptr;
        editTitle_    = nullptr;
        lblMeta_      = nullptr;
        lblPath_      = nullptr;
        btnCopy_      = nullptr;
        btnPin_       = nullptr;
        btnClear_     = nullptr;
        previewBitmap_.reset();
        return 0;
    default:
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

void HistoryWindow::RefreshList()
{
    if (!list_) {
        return;
    }

    ListView_DeleteAllItems(list_);
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const auto &it = items_[i];

        LVITEMW row{};
        row.mask           = LVIF_TEXT | LVIF_PARAM;
        row.iItem          = i;
        row.iSubItem       = 0;
        row.pszText        = const_cast<wchar_t *>(it.createdAt.c_str());
        row.lParam         = i;
        const int rowIndex = ListView_InsertItem(list_, &row);

        const auto nameText = FileNameFromPath(it.filePath);
        ListView_SetItemText(list_, rowIndex, 1, const_cast<wchar_t *>(nameText.c_str()));

        const auto sizeText = SizeText(it.width, it.height);
        ListView_SetItemText(list_, rowIndex, 2, const_cast<wchar_t *>(sizeText.c_str()));
    }

    if (!items_.empty()) {
        const int target = std::clamp(selectedIndex_, 0, static_cast<int>(items_.size()) - 1);
        ListView_SetItemState(list_, target, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list_, target, FALSE);
    }

    UpdateSelection();
}

void HistoryWindow::LayoutChildren(int width, int height)
{
    if (!hwnd_) {
        return;
    }

    const int w = std::max(600, width);
    const int h = std::max(420, height);

    const float scale = static_cast<float>(currentDpi_) / 96.0f;
    const int margin  = std::max(8, static_cast<int>(std::round(12.0f * scale)));
    const int gap     = std::max(8, static_cast<int>(std::round(12.0f * scale)));
    const int buttonH = std::max(28, static_cast<int>(std::round(32.0f * scale)));

    int leftWidth = static_cast<int>((w - margin * 2 - gap) * 0.62f);
    leftWidth     = std::clamp(leftWidth, 360, w - 320);

    const int rightX = margin + leftWidth + gap;
    const int rightW = w - rightX - margin;

    // Left list bottom aligns with the bottom edge of right action buttons.
    const int listHeight = std::max(120, h - margin * 2);

    SetWindowPos(list_, nullptr, margin, margin, leftWidth, listHeight, SWP_NOZORDER | SWP_NOACTIVATE);

    const int titleH           = std::max(30, static_cast<int>(std::round(34.0f * scale)));
    const int infoGap          = std::max(6, static_cast<int>(std::round(8.0f * scale)));
    int metaH                  = std::max(78, static_cast<int>(std::round(86.0f * scale)));
    int pathH                  = std::max(66, static_cast<int>(std::round(76.0f * scale)));
    const int minMetaH         = std::max(64, static_cast<int>(std::round(68.0f * scale)));
    const int minPathH         = std::max(56, static_cast<int>(std::round(64.0f * scale)));
    const int infoToButtonsGap = std::max(8, static_cast<int>(std::round(10.0f * scale)));

    SetWindowPos(editTitle_, nullptr, rightX, margin, rightW, titleH, SWP_NOZORDER | SWP_NOACTIVATE);

    const int previewTop  = margin + titleH + infoGap;
    const int minPreviewH = std::max(150, static_cast<int>(168 * scale));
    const int btnY        = h - margin - buttonH;
    const int infoBottom  = btnY - infoToButtonsGap;

    int previewBottom = infoBottom - infoGap - metaH - infoGap - pathH;
    int previewH      = previewBottom - previewTop;
    if (previewH < minPreviewH) {
        int deficit          = minPreviewH - previewH;
        const int metaShrink = std::min(deficit, std::max(0, metaH - minMetaH));
        metaH -= metaShrink;
        deficit -= metaShrink;
        const int pathShrink = std::min(deficit, std::max(0, pathH - minPathH));
        pathH -= pathShrink;
        deficit -= pathShrink;
        previewBottom = infoBottom - infoGap - metaH - infoGap - pathH;
        previewH      = std::max(minPreviewH, previewBottom - previewTop);
    }
    previewH     = std::max(minPreviewH, previewH);
    previewRect_ = RECT{rightX, previewTop, rightX + rightW, previewTop + previewH};

    SetWindowPos(lblMeta_, nullptr, rightX, previewRect_.bottom + infoGap, rightW, metaH, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(lblPath_, nullptr, rightX, previewRect_.bottom + infoGap + metaH, rightW, pathH, SWP_NOZORDER | SWP_NOACTIVATE);

    const int btnCount  = 3;
    const int btnGap    = std::max(6, static_cast<int>(std::round(8.0f * scale)));
    int btnW            = (rightW - btnGap * (btnCount - 1)) / btnCount;
    btnW                = std::max(72, btnW);
    const int btnStartX = rightX;
    SetWindowPos(btnClear_, nullptr, btnStartX + (btnW + btnGap) * 0, btnY, btnW, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(btnCopy_, nullptr, btnStartX + (btnW + btnGap) * 1, btnY, btnW, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(btnPin_, nullptr, btnStartX + (btnW + btnGap) * 2, btnY, btnW, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);

    InvalidateRect(hwnd_, nullptr, FALSE);
}

void HistoryWindow::UpdateSelection()
{
    selectedIndex_ = SelectedIndex();

    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(items_.size())) {
        SetWindowTextW(editTitle_, L"");
        SetWindowTextW(lblMeta_, L"未选择记录");
        SetWindowTextW(lblPath_, L"");
        previewBitmap_.reset();
        EnableWindow(editTitle_, FALSE);
        EnableWindow(btnCopy_, FALSE);
        EnableWindow(btnPin_, FALSE);
        EnableWindow(btnClear_, TRUE);
        InvalidateRect(hwnd_, &previewRect_, FALSE);
        return;
    }

    const auto &item = items_[selectedIndex_];
    SetWindowTextW(editTitle_, FileStemFromPath(item.filePath).c_str());
    EnableWindow(editTitle_, TRUE);

    std::wstringstream meta;
    meta << L"时间：" << item.createdAt << L"\r\n";
    meta << L"尺寸：" << item.width << L" x " << item.height << L"\r\n";
    meta << L"文件大小：" << FileSizeText(item.filePath);
    SetWindowTextW(lblMeta_, meta.str().c_str());
    std::wstring pathLines = FormatPathTwoLines(lblPath_, L"路径：" + item.filePath);
    SetWindowTextW(lblPath_, pathLines.c_str());

    if (deferPreviewUntilFlyoutShown_) {
        previewBitmap_.reset();
    } else {
        LoadPreview(item.filePath);
    }

    EnableWindow(btnCopy_, TRUE);
    EnableWindow(btnPin_, TRUE);
    EnableWindow(btnClear_, TRUE);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void HistoryWindow::LoadPreview(const std::wstring &filePath)
{
    previewBitmap_.reset();
    std::filesystem::path p(filePath);
    if (!std::filesystem::exists(p)) {
        return;
    }

    auto bmp = std::make_unique<Gdiplus::Bitmap>(p.c_str());
    if (bmp->GetLastStatus() != Gdiplus::Ok) {
        return;
    }

    previewBitmap_ = std::move(bmp);
}

void HistoryWindow::DrawPreview(HDC hdc)
{
    RECT rc{};
    GetClientRect(hwnd_, &rc);

    UiGdi::ScopedGdiObject<HBRUSH> bg(CreateSolidBrush(RGB(246, 248, 251)));
    FillRect(hdc, &rc, bg.Get());

    UiGdi::ScopedGdiObject<HBRUSH> pvBg(CreateSolidBrush(RGB(28, 31, 38)));
    FillRect(hdc, &previewRect_, pvBg.Get());

    if (previewBitmap_) {
        Gdiplus::Graphics g(hdc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        const int srcW = static_cast<int>(previewBitmap_->GetWidth());
        const int srcH = static_cast<int>(previewBitmap_->GetHeight());
        if (srcW > 0 && srcH > 0) {
            const int dstW     = RectWidth(previewRect_);
            const int dstH     = RectHeight(previewRect_);
            const double scale = std::min(static_cast<double>(dstW) / srcW, static_cast<double>(dstH) / srcH);
            const int drawW    = std::max(1, static_cast<int>(srcW * scale));
            const int drawH    = std::max(1, static_cast<int>(srcH * scale));
            const int dx       = previewRect_.left + (dstW - drawW) / 2;
            const int dy       = previewRect_.top + (dstH - drawH) / 2;

            g.DrawImage(previewBitmap_.get(), dx, dy, drawW, drawH);
        }
    } else {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(170, 178, 190));
        DrawTextW(hdc, L"选择一条历史记录以预览", -1, &previewRect_, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

int HistoryWindow::SelectedIndex() const
{
    if (!list_) {
        return -1;
    }
    return ListView_GetNextItem(list_, -1, LVNI_SELECTED);
}

void HistoryWindow::ApplyFlyoutClip()
{
    if (!hwnd_) {
        return;
    }

    RECT wr{};
    if (!GetWindowRect(hwnd_, &wr)) {
        return;
    }
    const int w           = std::max(0, RectWidth(wr));
    const int h           = std::max(0, RectHeight(wr));
    const LONG clipBottom = (flyoutWorkArea_.bottom > flyoutWorkArea_.top) ? flyoutWorkArea_.bottom : wr.bottom;
    const int visibleH    = std::clamp(static_cast<int>(clipBottom - wr.top), 0, h);

    HRGN rgn = CreateRectRgn(0, 0, w, visibleH);
    if (rgn) {
        SetWindowRgn(hwnd_, rgn, TRUE);
    }
}

void HistoryWindow::PositionAsFlyout()
{
    if (!hwnd_) {
        return;
    }

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    RECT wa{};
    HMONITOR mon = nullptr;
    POINT cursorPt{};
    if (GetCursorPos(&cursorPt)) {
        mon = MonitorFromPoint(cursorPt, MONITOR_DEFAULTTONEAREST);
    }
    if (!mon) {
        mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    }
    if (mon && GetMonitorInfoW(mon, &mi)) {
        wa = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    }
    flyoutWorkArea_ = wa;

    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    const int w            = RectWidth(wr);
    const int h            = RectHeight(wr);
    const int marginRight  = std::max(10, static_cast<int>(std::round(14.0f * currentDpi_ / 96.0f)));
    const int marginBottom = std::max(10, static_cast<int>(std::round(14.0f * currentDpi_ / 96.0f)));
    const int x            = wa.right - w - marginRight;
    const int y            = wa.bottom - h - marginBottom;
    SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void HistoryWindow::ShowFlyout()
{
    if (!hwnd_) {
        return;
    }

    if (flyoutTimer_ != 0) {
        KillTimer(hwnd_, flyoutTimer_);
        flyoutTimer_ = 0;
    }
    flyoutHiding_ = false;

    PositionAsFlyout();
    if (IsWindowVisible(hwnd_)) {
        deferPreviewUntilFlyoutShown_ = false;
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ApplyFlyoutClip();
        SetForegroundWindow(hwnd_);
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        return;
    }

    GetWindowRect(hwnd_, &flyoutTargetRect_);
    flyoutStartRect_        = flyoutTargetRect_;
    const int flyoutFloor   = flyoutWorkArea_.bottom;
    const int h             = RectHeight(flyoutStartRect_);
    flyoutStartRect_.top    = flyoutFloor;
    flyoutStartRect_.bottom = flyoutFloor + h;

    flyoutFrame_ = 0;
    SetWindowPos(hwnd_, HWND_TOPMOST, flyoutStartRect_.left, flyoutStartRect_.top,
                 RectWidth(flyoutTargetRect_), RectHeight(flyoutTargetRect_),
                 SWP_SHOWWINDOW);
    ApplyFlyoutClip();
    SetForegroundWindow(hwnd_);
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

    flyoutTimer_ = SetTimer(hwnd_, 1, 12, nullptr);
    if (flyoutTimer_ == 0) {
        SetWindowPos(hwnd_, HWND_TOPMOST, flyoutTargetRect_.left, flyoutTargetRect_.top, 0, 0, SWP_NOSIZE);
        ApplyFlyoutClip();
        if (deferPreviewUntilFlyoutShown_) {
            deferPreviewUntilFlyoutShown_ = false;
            UpdateSelection();
        }
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }
}

void HistoryWindow::HideFlyout()
{
    if (!hwnd_ || !IsWindowVisible(hwnd_)) {
        return;
    }

    if (flyoutTimer_ != 0) {
        if (flyoutHiding_) {
            return;
        }
        KillTimer(hwnd_, flyoutTimer_);
        flyoutTimer_ = 0;
    }

    GetWindowRect(hwnd_, &flyoutStartRect_);
    flyoutTargetRect_        = flyoutStartRect_;
    const int flyoutFloor    = flyoutWorkArea_.bottom;
    const int h              = RectHeight(flyoutTargetRect_);
    flyoutTargetRect_.top    = flyoutFloor;
    flyoutTargetRect_.bottom = flyoutFloor + h;
    flyoutFrame_             = 0;
    flyoutHiding_            = true;
    flyoutTimer_             = SetTimer(hwnd_, 1, 12, nullptr);
    if (flyoutTimer_ == 0) {
        flyoutHiding_ = false;
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void HistoryWindow::StepFlyoutAnimation()
{
    if (!hwnd_) {
        return;
    }

    constexpr int kFrames = 10;
    ++flyoutFrame_;
    const double t     = std::clamp(static_cast<double>(flyoutFrame_) / static_cast<double>(kFrames), 0.0, 1.0);
    const double c1    = 1.70158;
    const double c3    = c1 + 1.0;
    const double eased = flyoutHiding_
                             ? (c3 * t * t * t - c1 * t * t)                                      // ease-in-back
                             : (1.0 + c3 * std::pow(t - 1.0, 3.0) + c1 * std::pow(t - 1.0, 2.0)); // ease-out-back

    const int x = static_cast<int>(std::lround(
        flyoutStartRect_.left + (flyoutTargetRect_.left - flyoutStartRect_.left) * eased));
    const int y = static_cast<int>(std::lround(
        flyoutStartRect_.top + (flyoutTargetRect_.top - flyoutStartRect_.top) * eased));
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    ApplyFlyoutClip();

    if (flyoutFrame_ >= kFrames) {
        if (flyoutTimer_ != 0) {
            KillTimer(hwnd_, flyoutTimer_);
            flyoutTimer_ = 0;
        }
        if (flyoutHiding_) {
            flyoutHiding_ = false;
            ShowWindow(hwnd_, SW_HIDE);
        } else {
            SetWindowPos(hwnd_, HWND_TOPMOST, flyoutTargetRect_.left, flyoutTargetRect_.top, 0, 0,
                         SWP_NOSIZE | SWP_NOACTIVATE);
            ApplyFlyoutClip();
            if (deferPreviewUntilFlyoutShown_) {
                deferPreviewUntilFlyoutShown_ = false;
                UpdateSelection();
            }
            RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        }
    }
}

void HistoryWindow::UpdateDpiFonts()
{
    if (!hwnd_) {
        return;
    }
    currentDpi_ = GetDpiForWindow(hwnd_);

    // 使用 GDI 资源缓存管理字体，避免重复创建/销毁
    auto &cache         = GdiResourceCache::Instance();
    const int titleSize = MulDiv(16, static_cast<int>(currentDpi_), 96);
    const int textSize  = MulDiv(12, static_cast<int>(currentDpi_), 96);
    const int btnSize   = MulDiv(10, static_cast<int>(currentDpi_), 96);

    titleFont_  = cache.GetFont(L"Segoe UI", titleSize, FW_SEMIBOLD);
    normalFont_ = cache.GetFont(L"Segoe UI", textSize, FW_NORMAL);
    btnFont_    = cache.GetFont(L"Segoe UI", btnSize, FW_NORMAL);

    SetCtrlFont(list_, normalFont_);
    SetCtrlFont(editTitle_, titleFont_);
    SetCtrlFont(lblMeta_, normalFont_);
    SetCtrlFont(lblPath_, normalFont_);
    SetCtrlFont(btnClear_, btnFont_);
    SetCtrlFont(btnCopy_, btnFont_);
    SetCtrlFont(btnPin_, btnFont_);
}
