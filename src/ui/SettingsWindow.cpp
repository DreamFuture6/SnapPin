#include "ui/SettingsWindow.h"

#include "ui/GdiObject.h"
#include "ui/UiUtil.h"
#include "ui/ThemeColors.h"
#include "resource.h"
#include "common/KnownFolderUtil.h"

namespace
{
    constexpr wchar_t kSettingsClassName[] = L"SnapPinSettingsWindowClass";

    enum : int {
        IDC_CATEGORY_LIST          = 7001,
        IDC_BTN_APPLY              = 7002,
        IDC_BTN_CLOSE              = 7003,
        IDC_CHK_AUTOSTART          = 7004,
        IDC_EDT_HISTORY_LIMIT      = 7005,
        IDC_BTN_PIN_SAVE_PATH      = 7006,
        IDC_EDT_FILENAME_PATTERN   = 7007,
        IDC_CHK_SAVE_JPEG          = 7008,
        IDC_BTN_IMPORT             = 7009,
        IDC_BTN_EXPORT             = 7010,
        IDC_LINK_AUTHOR            = 7011,
        IDC_BTN_RESET_DEFAULTS     = 7012,
        IDC_CHK_GUIDE_LINES        = 7013,
        IDC_EDT_PADDLE_OCR_API_URL = 7014,
        IDC_EDT_PADDLE_OCR_TOKEN   = 7015,
        IDC_LINK_PADDLE_OCR        = 7016,
        IDC_HK_MOD_BASE            = 7100,
    };
    constexpr UINT_PTR kPanelSubclassId          = 0x5101;
    constexpr UINT_PTR kOwnerDrawHoverSubclassId = 0x5102;
    constexpr UINT_PTR kCategoryListSubclassId   = 0x5103;
    constexpr wchar_t kHoverPropName[]           = L"SnapPinHoverState";
    constexpr wchar_t kAuthorUrl[]               = L"https://github.com/DreamFuture6";
    constexpr wchar_t kPaddleOcrPortalUrl[]      = L"https://aistudio.baidu.com/paddleocr";

    // 使用统一的主题颜色定义
    using namespace ThemeColors::Basic;
    using namespace ThemeColors::State;
    const COLORREF kBgColor           = Background;
    const COLORREF kCardColor         = Surface;
    const COLORREF kPanelColor        = Surface;
    const COLORREF kTextColor         = Text;
    const COLORREF kMutedText         = TextMuted;
    const COLORREF kAccentColor       = AccentActive;
    const COLORREF kBorderColor       = Border;
    const COLORREF kInputColor        = RGB(49, 54, 62); // 保留特定输入框颜色（未在主题中定义）
    const COLORREF kEditBorderDefault = EditBorderDefault;
    const COLORREF kEditBorderHover   = EditBorderHover;
    const COLORREF kEditBorderActive  = EditBorderActive;

    using UiUtil::ApplyRoundedRegion;
    using UiUtil::CreateChildControl;
    using UiUtil::CreateLabel;
    using UiUtil::CreateOwnerDrawButton;
    using UiUtil::CreatePanel;
    using UiUtil::DpiScale;
    using UiUtil::DrawRoundedFillStroke;
    using UiUtil::FillRectColor;
    using UiUtil::GetWindowDpiSafe;
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_DEFAULT
#define DWMWCP_DEFAULT    0
#define DWMWCP_DONOTROUND 1
#define DWMWCP_ROUND      2
#define DWMWCP_ROUNDSMALL 3
#endif

    std::wstring VkLabel(UINT vk)
    {
        if (vk >= VK_F1 && vk <= VK_F24) {
            return L"F" + std::to_wstring(vk - VK_F1 + 1);
        }
        if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
            return std::wstring(1, static_cast<wchar_t>(vk));
        }
        switch (vk) {
        case VK_ESCAPE:
            return L"Esc";
        case VK_TAB:
            return L"Tab";
        case VK_RETURN:
            return L"Enter";
        case VK_SPACE:
            return L"Space";
        case VK_BACK:
            return L"Backspace";
        case VK_DELETE:
            return L"Delete";
        case VK_INSERT:
            return L"Insert";
        case VK_HOME:
            return L"Home";
        case VK_END:
            return L"End";
        case VK_PRIOR:
            return L"PageUp";
        case VK_NEXT:
            return L"PageDown";
        case VK_LEFT:
            return L"Left";
        case VK_RIGHT:
            return L"Right";
        case VK_UP:
            return L"Up";
        case VK_DOWN:
            return L"Down";
        case VK_CAPITAL:
            return L"CapsLock";
        case VK_SCROLL:
            return L"ScrollLock";
        case VK_NUMLOCK:
            return L"NumLock";
        case VK_SNAPSHOT:
            return L"PrintScreen";
        case VK_PAUSE:
            return L"Pause";
        case VK_APPS:
            return L"Menu";
        case VK_LWIN:
            return L"LeftWin";
        case VK_RWIN:
            return L"RightWin";
        case VK_OEM_1:
            return L";";
        case VK_OEM_PLUS:
            return L"=";
        case VK_OEM_COMMA:
            return L",";
        case VK_OEM_MINUS:
            return L"-";
        case VK_OEM_PERIOD:
            return L".";
        case VK_OEM_2:
            return L"/";
        case VK_OEM_3:
            return L"`";
        case VK_OEM_4:
            return L"[";
        case VK_OEM_5:
            return L"\\";
        case VK_OEM_6:
            return L"]";
        case VK_OEM_7:
            return L"'";
        case VK_OEM_102:
            return L"\\";
        case VK_DECIMAL:
            return L"Numpad .";
        case VK_ADD:
            return L"Numpad +";
        case VK_SUBTRACT:
            return L"Numpad -";
        case VK_MULTIPLY:
            return L"Numpad *";
        case VK_DIVIDE:
            return L"Numpad /";
        default:
            break;
        }
        if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
            return L"Numpad " + std::to_wstring(vk - VK_NUMPAD0);
        }
        UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        if (sc != 0) {
            switch (vk) {
            case VK_LEFT:
            case VK_RIGHT:
            case VK_UP:
            case VK_DOWN:
            case VK_INSERT:
            case VK_DELETE:
            case VK_HOME:
            case VK_END:
            case VK_PRIOR:
            case VK_NEXT:
            case VK_DIVIDE:
            case VK_NUMLOCK:
            case VK_RCONTROL:
            case VK_RMENU:
                sc |= 0xE000;
                break;
            default:
                break;
            }
            const LONG keyInfo = static_cast<LONG>(sc << 16);
            wchar_t keyName[64]{};
            if (GetKeyNameTextW(keyInfo, keyName, static_cast<int>(std::size(keyName))) > 0) {
                return keyName;
            }
        }
        wchar_t buf[32]{};
        swprintf_s(buf, L"0x%X", vk);
        return buf;
    }

    std::wstring DefaultPinSavePath()
    {
        std::filesystem::path out = KnownFolderUtil::GetPathOr(FOLDERID_LocalAppData, std::filesystem::temp_directory_path()) / L"SnapPin" / L"History";
        if (out.empty()) {
            out = std::filesystem::temp_directory_path() / L"SnapPin" / L"History";
        }
        std::error_code ec;
        std::filesystem::create_directories(out, ec);
        return out.wstring();
    }

    int CALLBACK BrowseFolderInitProc(HWND hwnd, UINT msg, LPARAM, LPARAM data)
    {
        if (msg == BFFM_INITIALIZED && data != 0) {
            const auto *initPath = reinterpret_cast<const wchar_t *>(data);
            if (initPath && initPath[0] != L'\0') {
                SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, reinterpret_cast<LPARAM>(initPath));
            }
        }
        return 0;
    }

    std::optional<std::wstring> BrowseFolderPath(HWND owner, const std::wstring &initialPath)
    {
        wchar_t displayName[MAX_PATH]{};
        BROWSEINFOW bi{};
        bi.hwndOwner      = owner;
        bi.pszDisplayName = displayName;
        bi.lpszTitle      = L"选择贴图保存路径";
        bi.ulFlags        = BIF_RETURNONLYFSDIRS | BIF_USENEWUI | BIF_VALIDATE;
        bi.lpfn           = BrowseFolderInitProc;
        bi.lParam         = reinterpret_cast<LPARAM>(initialPath.c_str());

        PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
        if (!pidl) {
            return std::nullopt;
        }

        wchar_t selected[MAX_PATH]{};
        const bool ok = SHGetPathFromIDListW(pidl, selected) != FALSE;
        CoTaskMemFree(pidl);
        if (!ok || selected[0] == L'\0') {
            return std::nullopt;
        }
        return std::wstring(selected);
    }

    bool IsPanel(HWND panel, HWND general, HWND hotkeys, HWND save, HWND importExport, HWND ocr, HWND about)
    {
        return panel == general || panel == hotkeys || panel == save || panel == importExport || panel == ocr || panel == about;
    }

    bool IsHotkeyFieldId(int id)
    {
        constexpr int kLocalHotkeyCount = 5;
        return id >= IDC_HK_MOD_BASE && id < (IDC_HK_MOD_BASE + kLocalHotkeyCount);
    }

    void InvalidateControlBorder(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd)) {
            return;
        }
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const int w = rc.right - rc.left;
        const int h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) {
            return;
        }
        const int bw = std::max(1, DpiScale(3, GetWindowDpiSafe(hwnd)));
        if (w <= bw * 2 || h <= bw * 2) {
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        RECT rTop{rc.left, rc.top, rc.right, rc.top + bw};
        RECT rBottom{rc.left, rc.bottom - bw, rc.right, rc.bottom};
        RECT rLeft{rc.left, rc.top + bw, rc.left + bw, rc.bottom - bw};
        RECT rRight{rc.right - bw, rc.top + bw, rc.right, rc.bottom - bw};
        InvalidateRect(hwnd, &rTop, FALSE);
        InvalidateRect(hwnd, &rBottom, FALSE);
        InvalidateRect(hwnd, &rLeft, FALSE);
        InvalidateRect(hwnd, &rRight, FALSE);
    }

    bool IsHoverControl(HWND hwnd)
    {
        return GetPropW(hwnd, kHoverPropName) != nullptr;
    }

    void SetHoverControl(HWND hwnd, bool hover)
    {
        const int id                     = GetDlgCtrlID(hwnd);
        const auto invalidateHoverTarget = [hwnd, id]() {
            if (IsHotkeyFieldId(id)) {
                InvalidateControlBorder(hwnd);
            } else {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        };
        if (hover) {
            if (!IsHoverControl(hwnd)) {
                SetPropW(hwnd, kHoverPropName, reinterpret_cast<HANDLE>(1));
                invalidateHoverTarget();
            }
        } else {
            if (IsHoverControl(hwnd)) {
                RemovePropW(hwnd, kHoverPropName);
                invalidateHoverTarget();
            }
        }
    }

    void ApplyDarkNonClient(HWND hwnd)
    {
        const BOOL dark        = TRUE;
        const COLORREF cap     = RGB(22, 24, 30);
        const COLORREF text    = RGB(235, 240, 246);
        const COLORREF border  = RGB(48, 52, 62);
        const DWORD cornerPref = DWMWCP_ROUNDSMALL;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &cap, sizeof(cap));
        DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));
    }

    LRESULT CALLBACK PanelForwardSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                              UINT_PTR, DWORD_PTR)
    {
        switch (msg) {
        case WM_LBUTTONDOWN: {
            HWND root = GetAncestor(hwnd, GA_ROOT);
            if (root && IsWindow(root)) {
                SetFocus(root);
                return 0;
            }
            break;
        }
        case WM_COMMAND:
        case WM_NOTIFY:
        case WM_DRAWITEM:
        case WM_MEASUREITEM:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HWND root = GetAncestor(hwnd, GA_ROOT);
            if (root && IsWindow(root)) {
                return SendMessageW(root, msg, wParam, lParam);
            }
            break;
        }
        default:
            break;
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    LRESULT CALLBACK OwnerDrawHoverSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                                UINT_PTR, DWORD_PTR)
    {
        switch (msg) {
        case WM_MOUSEMOVE: {
            SetHoverControl(hwnd, true);
            TRACKMOUSEEVENT tme{};
            tme.cbSize    = sizeof(tme);
            tme.dwFlags   = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            SetHoverControl(hwnd, false);
            return 0;
        case WM_NCDESTROY:
            SetHoverControl(hwnd, false);
            RemoveWindowSubclass(hwnd, OwnerDrawHoverSubclassProc, kOwnerDrawHoverSubclassId);
            return DefSubclassProc(hwnd, msg, wParam, lParam);
        default:
            break;
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    LRESULT CALLBACK CategoryListSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                              UINT_PTR, DWORD_PTR)
    {
        switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, CategoryListSubclassProc, kCategoryListSubclassId);
            return DefSubclassProc(hwnd, msg, wParam, lParam);
        default:
            break;
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    HICON LoadAppIcon(HINSTANCE hInstance, bool smallIcon)
    {
        const int size = smallIcon ? GetSystemMetrics(SM_CXSMICON) : GetSystemMetrics(SM_CXICON);
        return static_cast<HICON>(LoadImageW(
            hInstance,
            MAKEINTRESOURCEW(IDI_APP_ICON),
            IMAGE_ICON,
            size,
            size,
            LR_DEFAULTCOLOR));
    }

}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SettingsWindow *self = reinterpret_cast<SettingsWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        const auto *cs = reinterpret_cast<CREATESTRUCTW *>(lParam);
        self           = reinterpret_cast<SettingsWindow *>(cs->lpCreateParams);
        if (self) {
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        return TRUE;
    }
    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool SettingsWindow::Show(HINSTANCE hInstance, HWND owner, const AppSettings &settings, ApplyCallback onApply)
{
    hInstance_ = hInstance;
    current_   = settings;
    onApply_   = std::move(onApply);

    EnsureWindow(hInstance, owner);
    if (!hwnd_ || !IsWindow(hwnd_)) {
        return false;
    }

    ApplyToControls();
    SetCategory(category_);
    ApplyDarkNonClient(hwnd_);
    SetWindowTextW(hwnd_, L"SnapPin");

    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (mon && GetMonitorInfoW(mon, &mi)) {
        const int width  = wr.right - wr.left;
        const int height = wr.bottom - wr.top;
        const int x      = static_cast<int>(mi.rcWork.left) +
                      std::max<int>(0, (static_cast<int>(mi.rcWork.right - mi.rcWork.left) - width) / 2);
        const int y = static_cast<int>(mi.rcWork.top) +
                      std::max<int>(0, (static_cast<int>(mi.rcWork.bottom - mi.rcWork.top) - height) / 2);
        SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    return true;
}

bool SettingsWindow::CaptureHotkeyFromRegistered(UINT modifiers, UINT vk)
{
    if (hotkeyCaptureIndex_ < 0 || hotkeyCaptureIndex_ >= kHotkeyCount) {
        return false;
    }
    HotkeyConfig hk                                        = hotkeyDraft_[static_cast<size_t>(hotkeyCaptureIndex_)];
    hk.modifiers                                           = modifiers & (MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN);
    hk.vk                                                  = vk;
    hotkeyDraft_[static_cast<size_t>(hotkeyCaptureIndex_)] = hk;
    UpdateHotkeyConflictState();
    if (cmbHotkeyMods_[static_cast<size_t>(hotkeyCaptureIndex_)]) {
        InvalidateRect(cmbHotkeyMods_[static_cast<size_t>(hotkeyCaptureIndex_)], nullptr, FALSE);
    }
    return true;
}

void SettingsWindow::Destroy()
{
    if (hwnd_ && IsWindow(hwnd_)) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    UiGdi::ResetGdiObject(font_);
    UiGdi::ResetGdiObject(fontSmall_);
    UiGdi::ResetGdiObject(fontTitle_);
    UiGdi::ResetGdiObject(windowBrush_);
    UiGdi::ResetGdiObject(panelBrush_);
    UiGdi::ResetGdiObject(editBrush_);
}

void SettingsWindow::EnsureWindow(HINSTANCE hInstance, HWND owner)
{
    if (hwnd_ && IsWindow(hwnd_)) {
        return;
    }

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_LINK_CLASS;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SettingsWindow::WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kSettingsClassName;
    wc.hIcon         = LoadAppIcon(hInstance, false);
    wc.hIconSm       = LoadAppIcon(hInstance, true);
    RegisterClassExW(&wc);

    const UINT dpi   = GetWindowDpiSafe(owner);
    const int width  = DpiScale(920, dpi);
    const int height = DpiScale(640, dpi);
    hwnd_            = CreateWindowExW(
        WS_EX_APPWINDOW,
        kSettingsClassName,
        L"SnapPin",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        width,
        height,
        owner,
        nullptr,
        hInstance,
        this);

    if (!hwnd_) {
        return;
    }

    UiGdi::ResetGdiObject(windowBrush_);
    UiGdi::ResetGdiObject(panelBrush_);
    UiGdi::ResetGdiObject(editBrush_);
    windowBrush_ = CreateSolidBrush(kBgColor);
    panelBrush_  = CreateSolidBrush(kPanelColor);
    editBrush_   = CreateSolidBrush(kInputColor);

    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(wc.hIcon));
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(wc.hIconSm));
    ApplyDarkNonClient(hwnd_);

    CreateControls();
    EnsureFont();
    Layout();
    SetCategory(Category::General);
}
void SettingsWindow::CreateControls()
{
    lstCategories_ = CreateChildControl(
        hwnd_,
        hInstance_,
        WC_LISTBOXW,
        nullptr,
        WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL |
            LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
        IDC_CATEGORY_LIST);
    SendMessageW(lstCategories_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"常规"));
    SendMessageW(lstCategories_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"快捷键"));
    SendMessageW(lstCategories_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"保存"));
    SendMessageW(lstCategories_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"导入/导出"));
    SendMessageW(lstCategories_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"OCR"));
    SendMessageW(lstCategories_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"关于"));
    SendMessageW(lstCategories_, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    SetWindowSubclass(lstCategories_, CategoryListSubclassProc, kCategoryListSubclassId, 0);

    lblTitle_ = CreateLabel(hwnd_, hInstance_, L"常规");

    panelCard_         = CreatePanel(hwnd_, hInstance_, true);
    panelGeneral_      = CreatePanel(panelCard_, hInstance_, true);
    panelHotkeys_      = CreatePanel(panelCard_, hInstance_, false);
    panelSave_         = CreatePanel(panelCard_, hInstance_, false);
    panelImportExport_ = CreatePanel(panelCard_, hInstance_, false);
    panelOcr_          = CreatePanel(panelCard_, hInstance_, false);
    panelAbout_        = CreatePanel(panelCard_, hInstance_, false);

    chkAutoStart_.Create(panelGeneral_, hInstance_, IDC_CHK_AUTOSTART, L"开机启动", 120);
    chkGuideLines_.Create(panelGeneral_, hInstance_, IDC_CHK_GUIDE_LINES, L"显示辅助线", 104);
    lblHistoryLimit_ = CreateLabel(panelGeneral_, hInstance_, L"历史记录上限");
    historyLimitEdit_.Create(panelGeneral_, hInstance_, IDC_EDT_HISTORY_LIMIT, L"100", ES_NUMBER | ES_CENTER);
    lblHistoryHint_     = CreateLabel(panelGeneral_, hInstance_, L"ⓘ 范围10~100");
    lblPaddleOcrApiUrl_ = CreateLabel(panelOcr_, hInstance_, L"PaddleOCR 服务 URL");
    paddleOcrApiUrlEdit_.Create(panelOcr_, hInstance_, IDC_EDT_PADDLE_OCR_API_URL, L"", ES_AUTOHSCROLL);
    lblPaddleOcrToken_ = CreateLabel(panelOcr_, hInstance_, L"Access Token");
    paddleOcrTokenEdit_.Create(panelOcr_, hInstance_, IDC_EDT_PADDLE_OCR_TOKEN, L"", ES_AUTOHSCROLL);
    lblPaddleOcrHint_       = CreateLabel(panelOcr_, hInstance_, L"ⓘ AI Studio 服务请填写 URL 和 Token；自部署服务可只填 URL。");
    lblPaddleOcrLinkPrefix_ = CreateLabel(panelOcr_, hInstance_, L"ⓘ 官方接口：");
    btnPaddleOcrLink_       = CreateOwnerDrawButton(panelOcr_, hInstance_, kPaddleOcrPortalUrl, IDC_LINK_PADDLE_OCR);

    const std::array<const wchar_t *, kHotkeyCount> hotkeyNames = {
        L"区域截图",
        L"全屏截图",
        L"贴图最近截图",
        L"贴图控制",
        L"打开历史记录"};
    for (int i = 0; i < kHotkeyCount; ++i) {
        lblHotkey_[i]     = CreateLabel(panelHotkeys_, hInstance_, hotkeyNames[static_cast<size_t>(i)]);
        cmbHotkeyMods_[i] = CreateOwnerDrawButton(panelHotkeys_, hInstance_, L"", IDC_HK_MOD_BASE + i);
    }

    lblPinSavePath_ = CreateLabel(panelSave_, hInstance_, L"贴图保存路径");
    btnPinSavePath_ = CreateOwnerDrawButton(panelSave_, hInstance_, L"", IDC_BTN_PIN_SAVE_PATH);

    lblFileNamePattern_ = CreateLabel(panelSave_, hInstance_, L"文件命名规则");
    fileNamePatternEdit_.Create(panelSave_, hInstance_, IDC_EDT_FILENAME_PATTERN, L"", ES_AUTOHSCROLL | ES_CENTER);
    chkSaveAsJpeg_.Create(panelSave_, hInstance_, IDC_CHK_SAVE_JPEG, L"默认保存为 JPG", 60);

    btnImport_        = CreateOwnerDrawButton(panelImportExport_, hInstance_, L"导入设置", IDC_BTN_IMPORT);
    btnExport_        = CreateOwnerDrawButton(panelImportExport_, hInstance_, L"导出设置", IDC_BTN_EXPORT);
    btnResetDefaults_ = CreateOwnerDrawButton(panelImportExport_, hInstance_, L"恢复默认设置", IDC_BTN_RESET_DEFAULTS);
    lblImportTip_     = CreateLabel(panelImportExport_, hInstance_, L"导入会覆盖当前界面参数，点击“应用”后生效。");

    lblVersion_      = CreateLabel(panelAbout_, hInstance_, L"");
    lblAuthorPrefix_ = CreateLabel(panelAbout_, hInstance_, L"作者：");
    lblAuthor_       = CreateOwnerDrawButton(panelAbout_, hInstance_, L"DreamFuture6", IDC_LINK_AUTHOR);
    lblBuild_        = CreateLabel(panelAbout_, hInstance_, L"");

    btnApply_ = CreateOwnerDrawButton(hwnd_, hInstance_, L"应用", IDC_BTN_APPLY);
    btnClose_ = CreateOwnerDrawButton(hwnd_, hInstance_, L"取消", IDC_BTN_CLOSE);

    const HWND forwardPanels[] = {panelCard_, panelGeneral_, panelHotkeys_, panelSave_, panelImportExport_, panelOcr_, panelAbout_};
    for (HWND panel : forwardPanels) {
        if (panel) {
            SetWindowSubclass(panel, PanelForwardSubclassProc, kPanelSubclassId, 0);
        }
    }

    const HWND hoverControls[] = {
        btnPinSavePath_,
        btnImport_, btnExport_, btnResetDefaults_,
        btnApply_, btnClose_,
        btnPaddleOcrLink_};
    for (HWND control : hoverControls) {
        if (control) {
            SetWindowSubclass(control, OwnerDrawHoverSubclassProc, kOwnerDrawHoverSubclassId, 0);
        }
    }
    for (HWND hotkeyField : cmbHotkeyMods_) {
        if (hotkeyField) {
            SetWindowSubclass(hotkeyField, OwnerDrawHoverSubclassProc, kOwnerDrawHoverSubclassId, 0);
        }
    }

    hotkeyCaptureIndex_ = -1;
}

void SettingsWindow::EnsureFont()
{
    if (!hwnd_ || !IsWindow(hwnd_)) {
        return;
    }

    UiGdi::ResetGdiObject(font_);
    UiGdi::ResetGdiObject(fontSmall_);
    UiGdi::ResetGdiObject(fontTitle_);

    const UINT dpi = GetWindowDpiSafe(hwnd_);
    LOGFONTW lf{};
    lf.lfHeight         = -MulDiv(12, static_cast<int>(dpi), 72);
    lf.lfWeight         = FW_NORMAL;
    lf.lfCharSet        = DEFAULT_CHARSET;
    lf.lfOutPrecision   = OUT_TT_PRECIS;
    lf.lfClipPrecision  = CLIP_DEFAULT_PRECIS;
    lf.lfQuality        = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    font_ = CreateFontIndirectW(&lf);

    LOGFONTW lfSmall = lf;
    lfSmall.lfHeight = -MulDiv(10, static_cast<int>(dpi), 72);
    fontSmall_       = CreateFontIndirectW(&lfSmall);

    LOGFONTW lfTitle = lf;
    lfTitle.lfHeight = -MulDiv(14, static_cast<int>(dpi), 72);
    lfTitle.lfWeight = FW_SEMIBOLD;
    fontTitle_       = CreateFontIndirectW(&lfTitle);

    auto applyFont = [this](HWND root, auto &&applyFontRef) -> void {
        if (!root) {
            return;
        }
        SendMessageW(root, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        for (HWND child = GetWindow(root, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            applyFontRef(child, applyFontRef);
        }
    };
    applyFont(hwnd_, applyFont);

    const HWND smallLabels[] = {lblHistoryHint_, lblPaddleOcrHint_, lblPaddleOcrLinkPrefix_, btnPaddleOcrLink_};
    for (HWND label : smallLabels) {
        if (label && fontSmall_) {
            SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(fontSmall_), TRUE);
        }
    }
    if (lblTitle_ && fontTitle_) {
        SendMessageW(lblTitle_, WM_SETFONT, reinterpret_cast<WPARAM>(fontTitle_), TRUE);
    }

    if (lstCategories_) {
        const int itemH = DpiScale(52, dpi);
        SendMessageW(lstCategories_, LB_SETITEMHEIGHT, 0, itemH);
    }
}

void SettingsWindow::Layout()
{
    if (!hwnd_ || !IsWindow(hwnd_)) {
        return;
    }

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const UINT dpi         = GetWindowDpiSafe(hwnd_);
    const int margin       = DpiScale(16, dpi);
    const int leftWidth    = DpiScale(190, dpi);
    const int titleHeight  = DpiScale(42, dpi);
    const int buttonHeight = DpiScale(36, dpi);
    const int buttonWidth  = DpiScale(112, dpi);
    const int panelTop     = margin + titleHeight + DpiScale(10, dpi);
    const int panelBottom  = rc.bottom - margin - buttonHeight - DpiScale(10, dpi);
    const int panelHeight  = std::max<int>(DpiScale(120, dpi), panelBottom - panelTop);

    SetWindowPos(lstCategories_, nullptr, margin, margin, leftWidth, panelBottom - margin, SWP_NOZORDER);

    const int rightX = margin + leftWidth + margin;
    const int rightW = std::max<int>(DpiScale(420, dpi), rc.right - rightX - margin);

    SetWindowPos(lblTitle_, nullptr, rightX, margin, rightW, titleHeight, SWP_NOZORDER);
    SetWindowPos(panelCard_, nullptr, rightX, panelTop, rightW, panelHeight, SWP_NOZORDER);
    ApplyRoundedRegion(lstCategories_, DpiScale(6, dpi));
    ApplyRoundedRegion(panelCard_, DpiScale(6, dpi));

    const int cardPadding = DpiScale(16, dpi);
    const int innerW      = rightW - cardPadding * 2;
    const int innerH      = panelHeight - cardPadding * 2;
    const HWND panels[]   = {panelGeneral_, panelHotkeys_, panelSave_, panelImportExport_, panelOcr_, panelAbout_};
    for (HWND panel : panels) {
        SetWindowPos(panel, nullptr, cardPadding, cardPadding, innerW, innerH, SWP_NOZORDER);
    }

    const int btnY = rc.bottom - margin - buttonHeight;
    SetWindowPos(btnClose_, nullptr, rc.right - margin - buttonWidth, btnY, buttonWidth, buttonHeight, SWP_NOZORDER);
    SetWindowPos(btnApply_, nullptr, rc.right - margin - buttonWidth * 2 - DpiScale(10, dpi), btnY, buttonWidth, buttonHeight, SWP_NOZORDER);

    const int px             = DpiScale(20, dpi);
    const int rowH           = DpiScale(40, dpi);
    const int ctrlH          = DpiScale(34, dpi);
    const int gap            = DpiScale(16, dpi);
    const int rowInnerTop    = (rowH - ctrlH) / 2;
    const int editVisualBias = 0;

    RECT pr{};
    GetClientRect(panelGeneral_, &pr);
    int y = DpiScale(18, dpi);
    chkAutoStart_.SetBounds(px, y, pr.right - px * 2, rowH);
    y += rowH + gap;
    chkGuideLines_.SetBounds(px, y, pr.right - px * 2, rowH);
    y += rowH + gap;
    const int historyLabelW = DpiScale(180, dpi);
    SetWindowPos(lblHistoryLimit_, nullptr, px, y, historyLabelW, rowH, SWP_NOZORDER);
    const int editW = DpiScale(110, dpi);
    const int editX = px + historyLabelW + DpiScale(4, dpi);
    historyLimitEdit_.SetBounds(editX, y + rowInnerTop + editVisualBias, editW, ctrlH);
    SetWindowPos(lblHistoryHint_, nullptr, editX + editW + DpiScale(18, dpi), y, DpiScale(126, dpi), rowH, SWP_NOZORDER);

    GetClientRect(panelHotkeys_, &pr);
    y                = DpiScale(18, dpi);
    const int labelW = DpiScale(180, dpi);
    const int fieldW = std::min<int>(DpiScale(260, dpi), pr.right - labelW - px * 2 - DpiScale(12, dpi));
    for (int i = 0; i < kHotkeyCount; ++i) {
        SetWindowPos(lblHotkey_[i], nullptr, px, y, labelW, rowH, SWP_NOZORDER);
        SetWindowPos(cmbHotkeyMods_[i], nullptr, px + labelW + DpiScale(8, dpi), y + rowInnerTop, fieldW, ctrlH, SWP_NOZORDER);
        y += rowH + DpiScale(10, dpi);
    }

    GetClientRect(panelSave_, &pr);
    y                    = DpiScale(18, dpi);
    const int saveLabelW = DpiScale(160, dpi);
    const int saveEditW  = std::max<int>(DpiScale(200, dpi), pr.right - px * 2 - saveLabelW - DpiScale(12, dpi));
    const int saveEditX  = px + saveLabelW + DpiScale(12, dpi);

    SetWindowPos(lblPinSavePath_, nullptr, px, y, saveLabelW, rowH, SWP_NOZORDER);
    SetWindowPos(btnPinSavePath_, nullptr, saveEditX, y + rowInnerTop + editVisualBias, saveEditW, ctrlH, SWP_NOZORDER);

    y += rowH + gap;
    SetWindowPos(lblFileNamePattern_, nullptr, px, y, saveLabelW, rowH, SWP_NOZORDER);
    fileNamePatternEdit_.SetBounds(saveEditX, y + rowInnerTop + editVisualBias, saveEditW, ctrlH);
    y += rowH + gap;
    chkSaveAsJpeg_.SetBounds(px, y, pr.right - px * 2, rowH);

    GetClientRect(panelImportExport_, &pr);
    y                 = DpiScale(18, dpi);
    const int actBtnW = DpiScale(150, dpi);
    SetWindowPos(btnImport_, nullptr, px, y, actBtnW, buttonHeight, SWP_NOZORDER);
    SetWindowPos(btnExport_, nullptr, px + actBtnW + DpiScale(12, dpi), y, actBtnW, buttonHeight, SWP_NOZORDER);
    y += buttonHeight + DpiScale(10, dpi);
    SetWindowPos(btnResetDefaults_, nullptr, px, y, actBtnW, buttonHeight, SWP_NOZORDER);
    y += buttonHeight + DpiScale(16, dpi);
    SetWindowPos(lblImportTip_, nullptr, px, y, pr.right - px * 2, DpiScale(96, dpi), SWP_NOZORDER);

    GetClientRect(panelOcr_, &pr);
    y                   = DpiScale(18, dpi);
    const int ocrLabelW = DpiScale(180, dpi);
    const int ocrEditX  = px + ocrLabelW + DpiScale(12, dpi);
    const int ocrEditW  = std::max<int>(DpiScale(240, dpi), pr.right - px * 2 - ocrLabelW - DpiScale(12, dpi));
    SetWindowPos(lblPaddleOcrApiUrl_, nullptr, px, y, ocrLabelW, rowH, SWP_NOZORDER);
    paddleOcrApiUrlEdit_.SetBounds(ocrEditX, y + rowInnerTop + editVisualBias, ocrEditW, ctrlH);
    y += rowH + gap;
    SetWindowPos(lblPaddleOcrToken_, nullptr, px, y, ocrLabelW, rowH, SWP_NOZORDER);
    paddleOcrTokenEdit_.SetBounds(ocrEditX, y + rowInnerTop + editVisualBias, ocrEditW, ctrlH);
    y += rowH + DpiScale(6, dpi);
    SetWindowPos(lblPaddleOcrHint_, nullptr, px, y, pr.right - px * 2, DpiScale(40, dpi), SWP_NOZORDER);
    y += DpiScale(36, dpi);
    const int ocrLinkPrefixW = DpiScale(80, dpi);
    SetWindowPos(lblPaddleOcrLinkPrefix_, nullptr, px, y, ocrLinkPrefixW, DpiScale(40, dpi), SWP_NOZORDER);
    SetWindowPos(btnPaddleOcrLink_, nullptr, px + ocrLinkPrefixW, y, pr.right - px * 2 - ocrLinkPrefixW, DpiScale(40, dpi), SWP_NOZORDER);

    GetClientRect(panelAbout_, &pr);
    y = DpiScale(18, dpi);
    SetWindowPos(lblVersion_, nullptr, px, y, pr.right - px * 2, rowH, SWP_NOZORDER);
    y += rowH + DpiScale(8, dpi);
    const int authorPrefixW = DpiScale(47, dpi);
    SetWindowPos(lblAuthorPrefix_, nullptr, px, y, authorPrefixW, rowH, SWP_NOZORDER);
    SetWindowPos(lblAuthor_, nullptr, px + authorPrefixW, y, pr.right - px * 2 - authorPrefixW, rowH, SWP_NOZORDER);
    y += rowH + DpiScale(8, dpi);
    SetWindowPos(lblBuild_, nullptr, px, y, pr.right - px * 2, rowH, SWP_NOZORDER);
}

void SettingsWindow::SetCategory(Category category)
{
    if (category != category_) {
        SetHotkeyCaptureIndex(-1);
    }
    category_     = category;
    const int idx = static_cast<int>(category_);
    if (lstCategories_ && IsWindow(lstCategories_)) {
        const int cur = static_cast<int>(SendMessageW(lstCategories_, LB_GETCURSEL, 0, 0));
        if (cur != idx) {
            SendMessageW(lstCategories_, LB_SETCURSEL, idx, 0);
        }
    }

    const wchar_t *title = L"常规";
    switch (category_) {
    case Category::General:
        title = L"常规";
        break;
    case Category::Hotkeys:
        title = L"快捷键";
        break;
    case Category::Save:
        title = L"保存";
        break;
    case Category::ImportExport:
        title = L"导入 / 导出";
        break;
    case Category::Ocr:
        title = L"OCR";
        break;
    case Category::About:
        title = L"关于";
        break;
    default:
        break;
    }
    SetWindowTextW(lblTitle_, title);
    UpdateVisiblePanel();
}

void SettingsWindow::UpdateVisiblePanel()
{
    ShowWindow(panelGeneral_, category_ == Category::General ? SW_SHOW : SW_HIDE);
    ShowWindow(panelHotkeys_, category_ == Category::Hotkeys ? SW_SHOW : SW_HIDE);
    ShowWindow(panelSave_, category_ == Category::Save ? SW_SHOW : SW_HIDE);
    ShowWindow(panelImportExport_, category_ == Category::ImportExport ? SW_SHOW : SW_HIDE);
    ShowWindow(panelOcr_, category_ == Category::Ocr ? SW_SHOW : SW_HIDE);
    ShowWindow(panelAbout_, category_ == Category::About ? SW_SHOW : SW_HIDE);
}

bool SettingsWindow::IsModifierVk(UINT vk) const
{
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
           vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
           vk == VK_LWIN || vk == VK_RWIN;
}

std::wstring SettingsWindow::HotkeyToText(const HotkeyConfig &hk) const
{
    std::wstring text;
    const auto appendToken = [&text](const wchar_t *token) {
        if (!text.empty()) {
            text += L" + ";
        }
        text += token;
    };

    if ((hk.modifiers & MOD_CONTROL) != 0) {
        appendToken(L"Ctrl");
    }
    if ((hk.modifiers & MOD_ALT) != 0) {
        appendToken(L"Alt");
    }
    if ((hk.modifiers & MOD_SHIFT) != 0) {
        appendToken(L"Shift");
    }
    if ((hk.modifiers & MOD_WIN) != 0) {
        appendToken(L"Win");
    }
    if (hk.vk != 0) {
        appendToken(VkLabel(hk.vk).c_str());
    }
    if (text.empty()) {
        text = L"未设置";
    }
    return text;
}

void SettingsWindow::UpdateHotkeyConflictState()
{
    std::array<bool, kHotkeyCount> next{};
    for (int i = 0; i < kHotkeyCount; ++i) {
        const auto &a = hotkeyDraft_[static_cast<size_t>(i)];
        if (a.vk == 0) {
            continue;
        }
        for (int j = i + 1; j < kHotkeyCount; ++j) {
            const auto &b = hotkeyDraft_[static_cast<size_t>(j)];
            if (b.vk == 0) {
                continue;
            }
            if (a.vk == b.vk && a.modifiers == b.modifiers) {
                next[static_cast<size_t>(i)] = true;
                next[static_cast<size_t>(j)] = true;
            }
        }
    }
    for (int i = 0; i < kHotkeyCount; ++i) {
        if (next[static_cast<size_t>(i)] != hotkeyConflicts_[static_cast<size_t>(i)]) {
            hotkeyConflicts_[static_cast<size_t>(i)] = next[static_cast<size_t>(i)];
            if (cmbHotkeyMods_[static_cast<size_t>(i)]) {
                InvalidateControlBorder(cmbHotkeyMods_[static_cast<size_t>(i)]);
            }
        }
    }
}

bool SettingsWindow::HasHotkeyConflicts() const
{
    return std::any_of(hotkeyConflicts_.begin(), hotkeyConflicts_.end(), [](bool v) { return v; });
}

void SettingsWindow::SetHotkeyCaptureIndex(int index)
{
    const int prev      = hotkeyCaptureIndex_;
    hotkeyCaptureIndex_ = index;
    if (prev >= 0 && prev < kHotkeyCount && cmbHotkeyMods_[static_cast<size_t>(prev)]) {
        InvalidateRect(cmbHotkeyMods_[static_cast<size_t>(prev)], nullptr, FALSE);
    }
    if (hotkeyCaptureIndex_ >= 0 && hotkeyCaptureIndex_ < kHotkeyCount &&
        cmbHotkeyMods_[static_cast<size_t>(hotkeyCaptureIndex_)]) {
        SetFocus(hwnd_);
        InvalidateRect(cmbHotkeyMods_[static_cast<size_t>(hotkeyCaptureIndex_)], nullptr, FALSE);
    }
}

void SettingsWindow::ApplyToControls()
{
    chkAutoStart_.SetChecked(current_.autoStart);
    chkGuideLines_.SetChecked(current_.showGuideLines);
    chkSaveAsJpeg_.SetChecked(current_.saveAsJpeg);
    hotkeyDraft_[0] = current_.areaCapture;
    hotkeyDraft_[1] = current_.fullCapture;
    hotkeyDraft_[2] = current_.pinLast;
    hotkeyDraft_[3] = current_.closePins;
    hotkeyDraft_[4] = current_.showHistory;
    UpdateHotkeyConflictState();
    SetHotkeyCaptureIndex(-1);
    for (HWND hkField : cmbHotkeyMods_) {
        if (hkField) {
            InvalidateRect(hkField, nullptr, FALSE);
        }
    }

    const std::wstring limit = std::to_wstring(std::clamp(current_.historyLimit, 10, 100));
    uiHistoryLimit_          = std::clamp(current_.historyLimit, 10, 100);
    historyLimitEdit_.SetText(limit);
    SetWindowTextW(lblHistoryHint_, L"ⓘ 范围10~100");

    uiPinSavePath_ = current_.pinSavePath.empty() ? DefaultPinSavePath() : current_.pinSavePath;
    SetWindowTextW(btnPinSavePath_, uiPinSavePath_.c_str());
    fileNamePatternEdit_.SetText(current_.fileNamePattern);
    paddleOcrApiUrlEdit_.SetText(current_.paddleOcrApiUrl);
    paddleOcrTokenEdit_.SetText(current_.paddleOcrAccessToken);

    SetWindowTextW(lblVersion_, L"版本：v0.2.0");
    SetWindowTextW(lblAuthorPrefix_, L"作者：");
    SetWindowTextW(lblAuthor_, L"DreamFuture6");
    const std::wstring buildText = L"编译日期：" + Utf8ToWide(std::string(__DATE__) + " " + __TIME__);
    SetWindowTextW(lblBuild_, buildText.c_str());
}

bool SettingsWindow::ReadFromControls(AppSettings &out, std::wstring &error) const
{
    const auto trim = [](std::wstring value) {
        const size_t begin = value.find_first_not_of(L" \t\r\n");
        if (begin == std::wstring::npos) {
            return std::wstring();
        }
        const size_t end = value.find_last_not_of(L" \t\r\n");
        return value.substr(begin, end - begin + 1);
    };

    out                = current_;
    out.autoStart      = chkAutoStart_.Checked();
    out.showGuideLines = chkGuideLines_.Checked();

    const std::wstring limit = historyLimitEdit_.Text();
    try {
        out.historyLimit = std::stoi(limit);
    } catch (...) {
        error = L"历史记录上限必须是数字。";
        return false;
    }
    out.historyLimit = std::clamp(out.historyLimit, 10, 100);

    out.areaCapture = hotkeyDraft_[0];
    out.fullCapture = hotkeyDraft_[1];
    out.pinLast     = hotkeyDraft_[2];
    out.closePins   = hotkeyDraft_[3];
    out.showHistory = hotkeyDraft_[4];

    out.pinSavePath = uiPinSavePath_;
    if (out.pinSavePath.empty()) {
        error = L"贴图保存路径不能为空。";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(out.pinSavePath, ec);

    out.fileNamePattern = fileNamePatternEdit_.Text();
    if (out.fileNamePattern.empty()) {
        error = L"文件命名规则不能为空。";
        return false;
    }
    out.saveAsJpeg           = chkSaveAsJpeg_.Checked();
    out.paddleOcrApiUrl      = trim(paddleOcrApiUrlEdit_.Text());
    out.paddleOcrAccessToken = trim(paddleOcrTokenEdit_.Text());
    if (out.paddleOcrApiUrl.empty() && !out.paddleOcrAccessToken.empty()) {
        error = L"填写 Access Token 时，必须同时填写 PaddleOCR 服务 URL。";
        return false;
    }
    if (!out.paddleOcrApiUrl.empty()) {
        const bool validPrefix =
            out.paddleOcrApiUrl.rfind(L"http://", 0) == 0 ||
            out.paddleOcrApiUrl.rfind(L"https://", 0) == 0;
        if (!validPrefix) {
            error = L"PaddleOCR 服务 URL 必须以 http:// 或 https:// 开头。";
            return false;
        }
    }
    return true;
}

void SettingsWindow::OnApplyClicked()
{
    UpdateHotkeyConflictState();
    if (HasHotkeyConflicts()) {
        MessageBoxW(hwnd_, L"快捷键存在冲突，请先调整红色边框的项后再应用。", L"设置", MB_OK | MB_ICONWARNING);
        return;
    }

    AppSettings next{};
    std::wstring error;
    if (!ReadFromControls(next, error)) {
        MessageBoxW(hwnd_, error.c_str(), L"设置", MB_OK | MB_ICONWARNING);
        return;
    }

    if (onApply_ && !onApply_(next, error)) {
        MessageBoxW(hwnd_, error.empty() ? L"设置应用失败。" : error.c_str(), L"设置", MB_OK | MB_ICONERROR);
        return;
    }

    current_ = next;
    MessageBoxW(hwnd_, L"设置已应用。", L"设置", MB_OK | MB_ICONINFORMATION);
}

void SettingsWindow::OnImportClicked()
{
    std::filesystem::path desktop = DesktopDirectory();
    wchar_t fileBuf[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = hwnd_;
    ofn.lpstrFilter     = L"INI 文件 (*.ini)\0*.ini\0所有文件 (*.*)\0*.*\0\0";
    ofn.lpstrFile       = fileBuf;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrInitialDir = desktop.c_str();
    ofn.Flags           = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    ofn.lpstrDefExt     = L"ini";
    if (!GetOpenFileNameW(&ofn)) {
        return;
    }

    AppSettings imported{};
    if (!LoadSettingsFromIni(fileBuf, imported)) {
        MessageBoxW(hwnd_, L"导入失败，文件格式不正确。", L"设置", MB_OK | MB_ICONERROR);
        return;
    }
    current_ = imported;
    ApplyToControls();
    MessageBoxW(hwnd_, L"导入成功，请点击“应用”生效。", L"设置", MB_OK | MB_ICONINFORMATION);
}

void SettingsWindow::OnExportClicked()
{
    AppSettings now{};
    std::wstring error;
    if (!ReadFromControls(now, error)) {
        MessageBoxW(hwnd_, error.c_str(), L"设置", MB_OK | MB_ICONWARNING);
        return;
    }

    std::filesystem::path desktop = DesktopDirectory();
    wchar_t fileBuf[MAX_PATH]{};
    wcscpy_s(fileBuf, L"SnapPin_settings.ini");
    OPENFILENAMEW ofn{};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = hwnd_;
    ofn.lpstrFilter     = L"INI 文件 (*.ini)\0*.ini\0所有文件 (*.*)\0*.*\0\0";
    ofn.lpstrFile       = fileBuf;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrInitialDir = desktop.c_str();
    ofn.Flags           = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt     = L"ini";
    if (!GetSaveFileNameW(&ofn)) {
        return;
    }

    if (!SaveSettingsToIni(fileBuf, now)) {
        MessageBoxW(hwnd_, L"导出失败。", L"设置", MB_OK | MB_ICONERROR);
        return;
    }
    MessageBoxW(hwnd_, L"导出成功。", L"设置", MB_OK | MB_ICONINFORMATION);
}

void SettingsWindow::OnResetDefaultsClicked()
{
    AppSettings defaults{};

    chkAutoStart_.SetChecked(defaults.autoStart);
    chkGuideLines_.SetChecked(defaults.showGuideLines);
    chkSaveAsJpeg_.SetChecked(defaults.saveAsJpeg);
    uiHistoryLimit_ = std::clamp(defaults.historyLimit, 10, 100);

    hotkeyDraft_[0] = defaults.areaCapture;
    hotkeyDraft_[1] = defaults.fullCapture;
    hotkeyDraft_[2] = defaults.pinLast;
    hotkeyDraft_[3] = defaults.closePins;
    hotkeyDraft_[4] = defaults.showHistory;
    UpdateHotkeyConflictState();
    SetHotkeyCaptureIndex(-1);

    historyLimitEdit_.SetText(std::to_wstring(uiHistoryLimit_));
    uiPinSavePath_ = DefaultPinSavePath();
    SetWindowTextW(btnPinSavePath_, uiPinSavePath_.c_str());
    fileNamePatternEdit_.SetText(defaults.fileNamePattern);
    paddleOcrApiUrlEdit_.SetText(defaults.paddleOcrApiUrl);
    paddleOcrTokenEdit_.SetText(defaults.paddleOcrAccessToken);

    for (HWND hkField : cmbHotkeyMods_) {
        if (hkField) {
            InvalidateRect(hkField, nullptr, FALSE);
        }
    }
    MessageBoxW(hwnd_, L"已恢复默认设置，请点击“应用”生效。", L"设置", MB_OK | MB_ICONINFORMATION);
}

void SettingsWindow::OnChoosePinSavePathClicked()
{
    const std::wstring initial = uiPinSavePath_.empty() ? DefaultPinSavePath() : uiPinSavePath_;
    const auto picked          = BrowseFolderPath(hwnd_, initial);
    if (!picked.has_value()) {
        return;
    }
    uiPinSavePath_ = *picked;
    std::error_code ec;
    std::filesystem::create_directories(uiPinSavePath_, ec);
    SetWindowTextW(btnPinSavePath_, uiPinSavePath_.c_str());
    InvalidateRect(btnPinSavePath_, nullptr, FALSE);
}

void SettingsWindow::OpenAuthorLink() const
{
    ShellExecuteW(hwnd_, L"open", kAuthorUrl, nullptr, nullptr, SW_SHOWNORMAL);
}

void SettingsWindow::OpenPaddleOcrLink() const
{
    ShellExecuteW(hwnd_, L"open", kPaddleOcrPortalUrl, nullptr, nullptr, SW_SHOWNORMAL);
}

bool SettingsWindow::LoadSettingsFromIni(const std::filesystem::path &path, AppSettings &out) const
{
    if (path.empty()) {
        return false;
    }
    const std::wstring file = path.wstring();
    auto readInt            = [&file](const wchar_t *sec, const wchar_t *key, int defVal) -> int {
        return GetPrivateProfileIntW(sec, key, defVal, file.c_str());
    };
    auto readStr = [&file](const wchar_t *sec, const wchar_t *key, const wchar_t *defVal) -> std::wstring {
        wchar_t buf[260]{};
        GetPrivateProfileStringW(sec, key, defVal, buf, static_cast<DWORD>(std::size(buf)), file.c_str());
        return buf;
    };

    out.areaCapture.modifiers = static_cast<UINT>(readInt(L"hotkeys", L"area_mod", 0));
    out.areaCapture.vk        = static_cast<UINT>(readInt(L"hotkeys", L"area_vk", VK_F1));
    out.fullCapture.modifiers = static_cast<UINT>(readInt(L"hotkeys", L"full_mod", 0));
    out.fullCapture.vk        = static_cast<UINT>(readInt(L"hotkeys", L"full_vk", VK_F2));
    out.pinLast.modifiers     = static_cast<UINT>(readInt(L"hotkeys", L"pin_mod", 0));
    out.pinLast.vk            = static_cast<UINT>(readInt(L"hotkeys", L"pin_vk", VK_F3));
    out.showHistory.modifiers = static_cast<UINT>(readInt(L"hotkeys", L"history_mod", 0));
    out.showHistory.vk        = static_cast<UINT>(readInt(L"hotkeys", L"history_vk", VK_F5));
    out.closePins.modifiers   = static_cast<UINT>(readInt(L"hotkeys", L"close_mod", 0));
    out.closePins.vk          = static_cast<UINT>(readInt(L"hotkeys", L"close_vk", VK_F4));

    out.autoStart            = readInt(L"general", L"autostart", 0) != 0;
    out.showGuideLines       = readInt(L"general", L"show_guide_lines", 1) != 0;
    out.historyLimit         = std::clamp(readInt(L"general", L"history_limit", 100), 10, 100);
    out.pinSavePath          = readStr(L"general", L"pin_save_path", DefaultPinSavePath().c_str());
    out.fileNamePattern      = readStr(L"general", L"filename_pattern", L"yyyyMMdd_HHmmss");
    out.saveAsJpeg           = readInt(L"general", L"save_as_jpeg", 0) != 0;
    out.paddleOcrApiUrl      = readStr(L"ocr", L"api_url", L"");
    out.paddleOcrAccessToken = readStr(L"ocr", L"access_token", L"");
    return true;
}

bool SettingsWindow::SaveSettingsToIni(const std::filesystem::path &path, const AppSettings &in) const
{
    if (path.empty()) {
        return false;
    }
    const std::wstring file = path.wstring();
    auto writeInt           = [&file](const wchar_t *sec, const wchar_t *key, int value) {
        wchar_t buf[32]{};
        swprintf_s(buf, L"%d", value);
        WritePrivateProfileStringW(sec, key, buf, file.c_str());
    };

    writeInt(L"hotkeys", L"area_mod", static_cast<int>(in.areaCapture.modifiers));
    writeInt(L"hotkeys", L"area_vk", static_cast<int>(in.areaCapture.vk));
    writeInt(L"hotkeys", L"full_mod", static_cast<int>(in.fullCapture.modifiers));
    writeInt(L"hotkeys", L"full_vk", static_cast<int>(in.fullCapture.vk));
    writeInt(L"hotkeys", L"pin_mod", static_cast<int>(in.pinLast.modifiers));
    writeInt(L"hotkeys", L"pin_vk", static_cast<int>(in.pinLast.vk));
    writeInt(L"hotkeys", L"history_mod", static_cast<int>(in.showHistory.modifiers));
    writeInt(L"hotkeys", L"history_vk", static_cast<int>(in.showHistory.vk));
    writeInt(L"hotkeys", L"close_mod", static_cast<int>(in.closePins.modifiers));
    writeInt(L"hotkeys", L"close_vk", static_cast<int>(in.closePins.vk));

    writeInt(L"general", L"autostart", in.autoStart ? 1 : 0);
    writeInt(L"general", L"show_guide_lines", in.showGuideLines ? 1 : 0);
    writeInt(L"general", L"history_limit", std::clamp(in.historyLimit, 10, 100));
    WritePrivateProfileStringW(L"general", L"pin_save_path", in.pinSavePath.c_str(), file.c_str());
    WritePrivateProfileStringW(L"general", L"filename_pattern", in.fileNamePattern.c_str(), file.c_str());
    writeInt(L"general", L"save_as_jpeg", in.saveAsJpeg ? 1 : 0);
    WritePrivateProfileStringW(L"ocr", L"api_url", in.paddleOcrApiUrl.c_str(), file.c_str());
    WritePrivateProfileStringW(L"ocr", L"access_token", in.paddleOcrAccessToken.c_str(), file.c_str());
    return true;
}

std::filesystem::path SettingsWindow::DesktopDirectory() const
{
    return KnownFolderUtil::GetPathOr(FOLDERID_Desktop, std::filesystem::temp_directory_path());
}

LRESULT SettingsWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_GETMINMAXINFO: {
        auto *mmi             = reinterpret_cast<MINMAXINFO *>(lParam);
        const UINT dpi        = GetWindowDpiSafe(hwnd_);
        mmi->ptMinTrackSize.x = DpiScale(780, dpi);
        mmi->ptMinTrackSize.y = DpiScale(560, dpi);
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT *suggested = reinterpret_cast<const RECT *>(lParam);
        if (suggested) {
            SetWindowPos(hwnd_, nullptr,
                         suggested->left,
                         suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        EnsureFont();
        Layout();
        return 0;
    }
    case WM_SIZE:
        Layout();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC paintDc = BeginPaint(hwnd_, &ps);
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        const int widthPx  = std::max<int>(1, static_cast<int>(rc.right - rc.left));
        const int heightPx = std::max<int>(1, static_cast<int>(rc.bottom - rc.top));

        HDC hdc = CreateCompatibleDC(paintDc);
        UiGdi::ScopedGdiObject<HBITMAP> backBmp(CreateCompatibleBitmap(paintDc, widthPx, heightPx));
        UiGdi::ScopedSelectObject selectedBitmap(hdc, backBmp.Get());
        FillRectColor(hdc, rc, kBgColor);

        const UINT dpi          = GetWindowDpiSafe(hwnd_);
        const float panelRadius = static_cast<float>(DpiScale(6, dpi));

        if (lstCategories_ && IsWindowVisible(lstCategories_)) {
            RECT listRc{};
            GetWindowRect(lstCategories_, &listRc);
            MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT *>(&listRc), 2);
            DrawRoundedFillStroke(hdc, listRc, kPanelColor, kBorderColor, 1.0f, panelRadius, true);
        }

        if (panelCard_ && IsWindowVisible(panelCard_)) {
            RECT card{};
            GetWindowRect(panelCard_, &card);
            MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT *>(&card), 2);
            DrawRoundedFillStroke(hdc, card, kCardColor, kBorderColor, 1.0f, panelRadius, true);
        }

        BitBlt(paintDc, ps.rcPaint.left, ps.rcPaint.top,
               ps.rcPaint.right - ps.rcPaint.left,
               ps.rcPaint.bottom - ps.rcPaint.top,
               hdc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        DeleteDC(hdc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const HWND edits[] = {
            historyLimitEdit_.EditHandle(),
            paddleOcrApiUrlEdit_.EditHandle(),
            paddleOcrTokenEdit_.EditHandle(),
            fileNamePatternEdit_.EditHandle()};
        bool inEdit = false;
        for (HWND edit : edits) {
            if (!edit || !IsWindowVisible(edit)) {
                continue;
            }
            RECT er{};
            GetWindowRect(edit, &er);
            MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT *>(&er), 2);
            if (PtInRect(&er, pt)) {
                inEdit = true;
                break;
            }
        }
        if (!inEdit) {
            SetFocus(hwnd_);
            SetHotkeyCaptureIndex(-1);
        }
        break;
    }
    case WM_COMMAND: {
        const int id   = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id == IDC_CATEGORY_LIST && code == LBN_SELCHANGE) {
            const int sel = static_cast<int>(SendMessageW(lstCategories_, LB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel <= static_cast<int>(Category::About)) {
                SetCategory(static_cast<Category>(sel));
            }
            return 0;
        }
        if (id == IDC_BTN_APPLY && code == BN_CLICKED) {
            OnApplyClicked();
            return 0;
        }
        if (id == IDC_BTN_CLOSE && code == BN_CLICKED) {
            ShowWindow(hwnd_, SW_HIDE);
            return 0;
        }
        if (id == IDC_BTN_IMPORT && code == BN_CLICKED) {
            OnImportClicked();
            return 0;
        }
        if (id == IDC_BTN_EXPORT && code == BN_CLICKED) {
            OnExportClicked();
            return 0;
        }
        if (id == IDC_BTN_RESET_DEFAULTS && code == BN_CLICKED) {
            OnResetDefaultsClicked();
            return 0;
        }
        if (id == IDC_BTN_PIN_SAVE_PATH && code == BN_CLICKED) {
            OnChoosePinSavePathClicked();
            return 0;
        }
        if ((id == IDC_CHK_AUTOSTART || id == IDC_CHK_GUIDE_LINES || id == IDC_CHK_SAVE_JPEG) && code == BN_CLICKED) {
            CheckBoxControl *cb = nullptr;
            if (id == IDC_CHK_AUTOSTART) {
                cb = &chkAutoStart_;
            } else if (id == IDC_CHK_GUIDE_LINES) {
                cb = &chkGuideLines_;
            } else {
                cb = &chkSaveAsJpeg_;
            }
            if (!cb->HandleClickFromCurrentMessage()) {
                return 0;
            }
            return 0;
        }
        if (id == IDC_EDT_HISTORY_LIMIT && code == EN_KILLFOCUS) {
            const std::wstring text = historyLimitEdit_.Text();
            bool valid              = false;
            int value               = uiHistoryLimit_;
            try {
                size_t pos       = 0;
                const int parsed = std::stoi(text, &pos);
                if (pos == text.size() && parsed >= 10 && parsed <= 100) {
                    value = parsed;
                    valid = true;
                }
            } catch (...) {
                valid = false;
            }
            if (valid) {
                uiHistoryLimit_ = value;
            }
            historyLimitEdit_.SetText(std::to_wstring(uiHistoryLimit_));
            return 0;
        }
        if (IsHotkeyFieldId(id) && code == BN_CLICKED) {
            SetHotkeyCaptureIndex(id - IDC_HK_MOD_BASE);
            return 0;
        }
        if (id == IDC_LINK_AUTHOR && code == BN_CLICKED) {
            OpenAuthorLink();
            return 0;
        }
        if (id == IDC_LINK_PADDLE_OCR && code == BN_CLICKED) {
            OpenPaddleOcrLink();
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if (hotkeyCaptureIndex_ >= 0 && hotkeyCaptureIndex_ < kHotkeyCount) {
            if (wParam == VK_ESCAPE) {
                SetHotkeyCaptureIndex(-1);
                return 0;
            }
            HotkeyConfig hk = hotkeyDraft_[static_cast<size_t>(hotkeyCaptureIndex_)];
            hk.modifiers    = 0;
            if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) {
                hk.modifiers |= MOD_CONTROL;
            }
            if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) {
                hk.modifiers |= MOD_ALT;
            }
            if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) {
                hk.modifiers |= MOD_SHIFT;
            }
            if ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0) {
                hk.modifiers |= MOD_WIN;
            }
            if (!IsModifierVk(static_cast<UINT>(wParam))) {
                hk.vk = static_cast<UINT>(wParam);
            } else {
                hk.vk = 0;
            }
            hotkeyDraft_[static_cast<size_t>(hotkeyCaptureIndex_)] = hk;
            UpdateHotkeyConflictState();
            if (cmbHotkeyMods_[static_cast<size_t>(hotkeyCaptureIndex_)]) {
                InvalidateRect(cmbHotkeyMods_[static_cast<size_t>(hotkeyCaptureIndex_)], nullptr, FALSE);
            }
            return 0;
        }
        break;
    }
    case WM_DRAWITEM: {
        const auto *dis = reinterpret_cast<const DRAWITEMSTRUCT *>(lParam);
        if (!dis) {
            break;
        }
        const int id        = static_cast<int>(dis->CtlID);
        const bool selected = (dis->itemState & ODS_SELECTED) != 0;
        const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
        HDC hdc             = dis->hDC;
        RECT rc             = dis->rcItem;
        SetBkMode(hdc, TRANSPARENT);
        UiGdi::ScopedSelectObject selectedFont(hdc, font_);

        if (id == IDC_CATEGORY_LIST && dis->CtlType == ODT_LISTBOX) {
            if (dis->itemID == static_cast<UINT>(-1)) {
                return TRUE;
            }
            const bool catSel = (dis->itemState & ODS_SELECTED) != 0;
            const int count   = static_cast<int>(SendMessageW(dis->hwndItem, LB_GETCOUNT, 0, 0));
            RECT bg           = rc;
            if (dis->itemID == 0) {
                bg.top += DpiScale(6, GetWindowDpiSafe(hwnd_));
            }
            if (count > 0 && static_cast<int>(dis->itemID) == count - 1) {
                bg.bottom -= DpiScale(6, GetWindowDpiSafe(hwnd_));
            }
            FillRectColor(hdc, bg, kPanelColor);
            if (catSel) {
                RECT hi        = rc;
                const int padX = DpiScale(4, GetWindowDpiSafe(hwnd_));
                int padTop     = DpiScale(3, GetWindowDpiSafe(hwnd_));
                int padBottom  = DpiScale(3, GetWindowDpiSafe(hwnd_));
                if (dis->itemID == 0) {
                    padTop = DpiScale(6, GetWindowDpiSafe(hwnd_));
                }
                if (count > 0 && static_cast<int>(dis->itemID) == count - 1) {
                    padBottom = DpiScale(6, GetWindowDpiSafe(hwnd_));
                }
                hi.left += padX;
                hi.right -= padX;
                hi.top += padTop;
                hi.bottom -= padBottom;
                DrawRoundedFillStroke(
                    hdc,
                    hi,
                    kAccentColor,
                    RGB(78, 154, 255),
                    1.0f,
                    static_cast<float>(DpiScale(4, GetWindowDpiSafe(hwnd_))),
                    true);
            }
            SetTextColor(hdc, catSel ? RGB(255, 255, 255) : kTextColor);
            wchar_t text[128]{};
            SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, reinterpret_cast<LPARAM>(text));
            RECT tr = rc;
            tr.left += DpiScale(14, GetWindowDpiSafe(hwnd_));
            DrawTextW(hdc, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            return TRUE;
        }

        if (id == IDC_CHK_AUTOSTART || id == IDC_CHK_GUIDE_LINES || id == IDC_CHK_SAVE_JPEG) {
            CheckBoxRenderStyle style{};
            style.panelColor    = kPanelColor;
            style.textColor     = kTextColor;
            style.inputColor    = kInputColor;
            style.accentColor   = kAccentColor;
            style.borderDefault = kEditBorderDefault;
            style.borderHover   = kEditBorderHover;
            style.borderActive  = kEditBorderActive;
            CheckBoxControl *cb = nullptr;
            if (id == IDC_CHK_AUTOSTART) {
                cb = &chkAutoStart_;
            } else if (id == IDC_CHK_GUIDE_LINES) {
                cb = &chkGuideLines_;
            } else {
                cb = &chkSaveAsJpeg_;
            }
            return cb->Draw(dis, style, GetWindowDpiSafe(hwnd_)) ? TRUE : FALSE;
        }

        if (IsHotkeyFieldId(id)) {
            const int idx       = id - IDC_HK_MOD_BASE;
            const bool active   = (idx == hotkeyCaptureIndex_);
            const bool hovered  = IsHoverControl(dis->hwndItem);
            const bool conflict = hotkeyConflicts_[static_cast<size_t>(idx)];
            COLORREF stroke     = UiUtil::UnifiedBorderColor(
                active, hovered, kEditBorderDefault, kEditBorderHover, kEditBorderActive);
            if (!active && conflict) {
                stroke = hovered ? RGB(235, 118, 118) : RGB(215, 86, 86);
            }
            DrawRoundedFillStroke(
                hdc,
                rc,
                kInputColor,
                stroke,
                1.0f,
                static_cast<float>(DpiScale(4, GetWindowDpiSafe(hwnd_))),
                true);
            std::wstring text = HotkeyToText(hotkeyDraft_[static_cast<size_t>(idx)]);
            if (active) {
                text += L"   (Esc 结束)";
            }
            RECT tr = rc;
            SetTextColor(hdc, kTextColor);
            DrawTextW(hdc, text.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_END_ELLIPSIS);
            return TRUE;
        }

        if (id == IDC_LINK_AUTHOR || id == IDC_LINK_PADDLE_OCR) {
            FillRectColor(hdc, rc, kPanelColor);
            SetTextColor(hdc, kAccentColor);
            wchar_t text[256]{};
            GetWindowTextW(dis->hwndItem, text, static_cast<int>(std::size(text)));
            DrawTextW(hdc, text, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
            return TRUE;
        }

        if (id == IDC_BTN_PIN_SAVE_PATH) {
            const bool hovered    = IsHoverControl(dis->hwndItem);
            const COLORREF fill   = hovered ? RGB(56, 61, 70) : kInputColor;
            const COLORREF stroke = UiUtil::UnifiedBorderColor(
                selected, hovered, kEditBorderDefault, kEditBorderHover, kEditBorderActive);
            DrawRoundedFillStroke(
                hdc,
                rc,
                fill,
                stroke,
                1.0f,
                static_cast<float>(DpiScale(4, GetWindowDpiSafe(hwnd_))),
                true);
            RECT tr = rc;
            tr.left += DpiScale(10, GetWindowDpiSafe(hwnd_));
            tr.right -= DpiScale(10, GetWindowDpiSafe(hwnd_));
            const std::wstring &text = uiPinSavePath_.empty() ? std::wstring(L"点击选择路径...") : uiPinSavePath_;
            SetTextColor(hdc, kTextColor);
            DrawTextW(hdc, text.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
            return TRUE;
        }

        const bool hovered = IsHoverControl(dis->hwndItem);
        COLORREF fill      = selected ? RGB(74, 154, 255) : (id == IDC_BTN_APPLY ? kAccentColor : kInputColor);
        COLORREF stroke    = UiUtil::UnifiedBorderColor(
            selected, hovered, kEditBorderDefault, kEditBorderHover, kEditBorderActive);
        if (!selected && hovered) {
            fill = (id == IDC_BTN_APPLY) ? RGB(74, 150, 255) : RGB(60, 65, 75);
        }
        DrawRoundedFillStroke(
            hdc,
            rc,
            fill,
            stroke,
            1.0f,
            static_cast<float>(DpiScale(8, GetWindowDpiSafe(hwnd_))),
            true);

        wchar_t text[128]{};
        GetWindowTextW(dis->hwndItem, text, static_cast<int>(std::size(text)));
        SetTextColor(hdc, disabled ? kMutedText : RGB(245, 248, 252));
        DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return TRUE;
    }
    case WM_SETCURSOR: {
        HWND target = reinterpret_cast<HWND>(wParam);
        if (target && IsWindow(target)) {
            const int id = GetDlgCtrlID(target);
            if (id == IDC_CHK_AUTOSTART || id == IDC_CHK_GUIDE_LINES || id == IDC_CHK_SAVE_JPEG) {
                CheckBoxControl *cb = nullptr;
                if (id == IDC_CHK_AUTOSTART) {
                    cb = &chkAutoStart_;
                } else if (id == IDC_CHK_GUIDE_LINES) {
                    cb = &chkGuideLines_;
                } else {
                    cb = &chkSaveAsJpeg_;
                }
                cb->HandleSetCursor();
                return TRUE;
            }
            if (IsHotkeyFieldId(id) || id == IDC_LINK_AUTHOR || id == IDC_LINK_PADDLE_OCR ||
                id == IDC_BTN_IMPORT || id == IDC_BTN_EXPORT || id == IDC_BTN_RESET_DEFAULTS ||
                id == IDC_BTN_PIN_SAVE_PATH || id == IDC_BTN_APPLY || id == IDC_BTN_CLOSE) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc     = reinterpret_cast<HDC>(wParam);
        HWND ctl    = reinterpret_cast<HWND>(lParam);
        HWND parent = GetParent(ctl);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, kTextColor);
        if (ctl == panelCard_) {
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }
        if (IsPanel(ctl, panelGeneral_, panelHotkeys_, panelSave_, panelImportExport_, panelOcr_, panelAbout_) ||
            IsPanel(parent, panelGeneral_, panelHotkeys_, panelSave_, panelImportExport_, panelOcr_, panelAbout_)) {
            SetBkColor(hdc, kPanelColor);
            return reinterpret_cast<LRESULT>(panelBrush_ ? panelBrush_ : GetSysColorBrush(COLOR_WINDOW));
        }
        if (msg == WM_CTLCOLOREDIT) {
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, kInputColor);
            SetTextColor(hdc, kTextColor);
            return reinterpret_cast<LRESULT>(editBrush_ ? editBrush_ : GetSysColorBrush(COLOR_WINDOW));
        }
        if (msg == WM_CTLCOLORLISTBOX) {
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, kPanelColor);
            SetTextColor(hdc, kTextColor);
            return reinterpret_cast<LRESULT>(panelBrush_ ? panelBrush_ : GetSysColorBrush(COLOR_WINDOW));
        }
        SetBkColor(hdc, kBgColor);
        return reinterpret_cast<LRESULT>(windowBrush_ ? windowBrush_ : GetSysColorBrush(COLOR_BTNFACE));
    }
    case WM_CLOSE:
        ShowWindow(hwnd_, SW_HIDE);
        return 0;
    case WM_DESTROY:
        hwnd_ = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}
