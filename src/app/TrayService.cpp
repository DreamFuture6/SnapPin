#include "app/TrayService.h"
#include "app/AppIds.h"
#include "resource.h"

namespace {
constexpr const wchar_t* kAppNameWithAuthor = L"SnapPin";

std::wstring NormalizeAppName(std::wstring name) {
    if (name.empty()) {
        wchar_t exe[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, exe, MAX_PATH) > 0) {
            name = std::filesystem::path(exe).stem().wstring();
        }
    }
    if (name.empty()) {
        name = L"SnapPin";
    }
    if (name.size() > 4) {
        const std::wstring tail = name.substr(name.size() - 4);
        if (_wcsicmp(tail.c_str(), L".exe") == 0) {
            name = name.substr(0, name.size() - 4);
        }
    }
    return name;
}
}

bool TrayService::Initialize(HWND hwnd) {
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd;
    nid_.uID = 1;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
#ifdef NIF_SHOWTIP
    nid_.uFlags |= NIF_SHOWTIP;
#endif
    nid_.uCallbackMessage = WMAPP_TRAY;
    nid_.hIcon = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        0,
        0,
        LR_DEFAULTSIZE | LR_SHARED
    ));
    if (!nid_.hIcon) {
        nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    const std::wstring appName = NormalizeAppName(kAppNameWithAuthor);
    wcsncpy_s(nid_.szTip, appName.c_str(), _TRUNCATE);

    if (!Shell_NotifyIconW(NIM_ADD, &nid_)) {
        return false;
    }
    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);

    initialized_ = true;
    return true;
}

bool TrayService::Restore() {
    if (!initialized_) {
        return false;
    }
    if (!Shell_NotifyIconW(NIM_ADD, &nid_)) {
        return false;
    }
    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);

    NOTIFYICONDATAW tip = nid_;
    tip.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &tip);
    return true;
}

void TrayService::Remove() {
    if (!initialized_) {
        return;
    }
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    initialized_ = false;
}

void TrayService::ShowMenu(HWND hwnd, POINT pt, bool autoStartEnabled) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }

    AppendMenuW(menu, MF_STRING, ID_TRAY_HISTORY, L"查看历史");
    AppendMenuW(menu, MF_STRING, ID_TRAY_SETTINGS, L"打开设置");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (autoStartEnabled ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_AUTOSTART, L"开机启动");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"退出");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

void TrayService::ShowNotification(const std::wstring& title, const std::wstring& message, DWORD timeoutMs) {
    if (!initialized_) {
        return;
    }

    // Force-close any currently visible balloon first so the new one shows immediately.
    NOTIFYICONDATAW clearTip = nid_;
    clearTip.uFlags = NIF_INFO;
#ifdef NIF_REALTIME
    clearTip.uFlags |= NIF_REALTIME;
#endif
    clearTip.dwInfoFlags = NIIF_NONE;
    clearTip.uTimeout = 1000;
    clearTip.szInfo[0] = L'\0';
    clearTip.szInfoTitle[0] = L'\0';
    Shell_NotifyIconW(NIM_MODIFY, &clearTip);

    NOTIFYICONDATAW tip = nid_;
    tip.uFlags = NIF_INFO;
#ifdef NIF_REALTIME
    tip.uFlags |= NIF_REALTIME;
#endif
    tip.dwInfoFlags = NIIF_INFO;
    tip.uTimeout = std::clamp<DWORD>(timeoutMs, 1000, 10000);
    std::wstring cleanTitle = NormalizeAppName(title);
    if (cleanTitle.empty() || _wcsicmp(cleanTitle.c_str(), L"SnapPin") == 0) {
        cleanTitle = NormalizeAppName(kAppNameWithAuthor);
    }
    wcsncpy_s(tip.szInfoTitle, cleanTitle.c_str(), _TRUNCATE);
    wcsncpy_s(tip.szInfo, message.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &tip);
}
