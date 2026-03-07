#pragma once
#include "common.h"
#include "core/Settings.h"

class HotkeyService {
public:
    bool Register(HWND hwnd, const AppSettings& settings, std::wstring& errorText);
    void Unregister(HWND hwnd);

private:
    bool RegisterOne(HWND hwnd, int id, HotkeyConfig cfg, std::wstring& errorText);
};
