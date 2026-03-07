#pragma once
#include "common.h"

class TrayService {
public:
    bool Initialize(HWND hwnd);
    bool Restore();
    void Remove();
    void ShowMenu(HWND hwnd, POINT pt, bool autoStartEnabled);
    void ShowNotification(const std::wstring& title, const std::wstring& message, DWORD timeoutMs = 1500);

private:
    NOTIFYICONDATAW nid_{};
    bool initialized_ = false;
};
