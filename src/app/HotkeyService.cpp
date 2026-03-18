#include "app/HotkeyService.h"
#include "app/AppIds.h"

bool HotkeyService::Register(HWND hwnd, const AppSettings& settings, std::wstring& errorText) {
    if (!RegisterOne(hwnd, HOTKEY_ID_AREA, settings.areaCapture, errorText)) {
        return false;
    }
    if (!RegisterOne(hwnd, HOTKEY_ID_FULL, settings.fullCapture, errorText)) {
        Unregister(hwnd);
        return false;
    }
    if (!RegisterOne(hwnd, HOTKEY_ID_PIN_LAST, settings.pinLast, errorText)) {
        Unregister(hwnd);
        return false;
    }
    if (!RegisterOne(hwnd, HOTKEY_ID_HISTORY, settings.showHistory, errorText)) {
        Unregister(hwnd);
        return false;
    }
    if (!RegisterOne(hwnd, HOTKEY_ID_CLOSE_PINS, settings.closePins, errorText)) {
        Unregister(hwnd);
        return false;
    }
    return true;
}

void HotkeyService::Unregister(HWND hwnd) {
    UnregisterHotKey(hwnd, HOTKEY_ID_AREA);
    UnregisterHotKey(hwnd, HOTKEY_ID_FULL);
    UnregisterHotKey(hwnd, HOTKEY_ID_PIN_LAST);
    UnregisterHotKey(hwnd, HOTKEY_ID_HISTORY);
    UnregisterHotKey(hwnd, HOTKEY_ID_CLOSE_PINS);
}

bool HotkeyService::RegisterOne(HWND hwnd, int id, HotkeyConfig cfg, std::wstring& errorText) {
    if (cfg.vk == 0) {
        errorText = L"Hotkey virtual key cannot be 0.";
        return false;
    }

    UINT modifiers = cfg.modifiers | MOD_NOREPEAT;
    if (!RegisterHotKey(hwnd, id, modifiers, cfg.vk)) {
        DWORD err = GetLastError();
        if (err == ERROR_INVALID_PARAMETER && (modifiers & MOD_NOREPEAT)) {
            modifiers = cfg.modifiers;
            if (RegisterHotKey(hwnd, id, modifiers, cfg.vk)) {
                return true;
            }
            err = GetLastError();
        }

        if (err == ERROR_HOTKEY_ALREADY_REGISTERED) {
            errorText = L"快捷键被占用。";
        } else {
            errorText = L"快捷键注册失败。";
        }
        return false;
    }

    return true;
}
