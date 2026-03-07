#include "core/Settings.h"
#include "core/Logger.h"

namespace {
std::filesystem::path GetKnownFolder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    std::filesystem::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)) && path) {
        out = path;
        CoTaskMemFree(path);
    }
    return out;
}

int ReadInt(const std::wstring& file, const wchar_t* section, const wchar_t* key, int defaultValue) {
    return GetPrivateProfileIntW(section, key, defaultValue, file.c_str());
}

std::wstring ReadString(const std::wstring& file, const wchar_t* section, const wchar_t* key, const wchar_t* defaultValue) {
    wchar_t buf[260]{};
    GetPrivateProfileStringW(section, key, defaultValue, buf, static_cast<DWORD>(std::size(buf)), file.c_str());
    return buf;
}

void WriteInt(const std::wstring& file, const wchar_t* section, const wchar_t* key, int value) {
    wchar_t buf[32]{};
    swprintf_s(buf, L"%d", value);
    WritePrivateProfileStringW(section, key, buf, file.c_str());
}
}

bool SettingsService::Initialize() {
    baseDir_ = GetKnownFolder(FOLDERID_LocalAppData) / L"SnapPin";
    historyDir_ = baseDir_ / L"History";
    tempDir_ = baseDir_ / L"Temp";
    configPath_ = baseDir_ / L"settings.ini";

    std::error_code ec;
    std::filesystem::create_directories(baseDir_, ec);
    std::filesystem::create_directories(historyDir_, ec);
    std::filesystem::create_directories(tempDir_, ec);

    Logger::Instance().Initialize(baseDir_);
    Logger::Instance().Info(L"SettingsService initialized.");
    return true;
}

bool SettingsService::Load(AppSettings& out) {
    const auto cfg = configPath_.wstring();
    out.areaCapture.modifiers = static_cast<UINT>(ReadInt(cfg, L"hotkeys", L"area_mod", 0));
    out.areaCapture.vk = static_cast<UINT>(ReadInt(cfg, L"hotkeys", L"area_vk", VK_F1));
    out.fullCapture.modifiers = static_cast<UINT>(ReadInt(cfg, L"hotkeys", L"full_mod", 0));
    out.fullCapture.vk = static_cast<UINT>(ReadInt(cfg, L"hotkeys", L"full_vk", VK_F2));
    out.pinLast.modifiers = static_cast<UINT>(ReadInt(cfg, L"hotkeys", L"pin_mod", 0));
    out.pinLast.vk = static_cast<UINT>(ReadInt(cfg, L"hotkeys", L"pin_vk", VK_F3));
    out.showHistory.modifiers = static_cast<UINT>(ReadInt(cfg, L"hotkeys", L"history_mod", 0));
    out.showHistory.vk = static_cast<UINT>(ReadInt(cfg, L"hotkeys", L"history_vk", VK_F5));
    out.closePins.modifiers = static_cast<UINT>(ReadInt(cfg, L"hotkeys", L"close_mod", 0));
    out.closePins.vk = static_cast<UINT>(ReadInt(cfg, L"hotkeys", L"close_vk", VK_F4));

    out.autoStart = ReadInt(cfg, L"general", L"autostart", 0) != 0;
    out.showGuideLines = ReadInt(cfg, L"general", L"show_guide_lines", 1) != 0;
    out.historyLimit = std::clamp(ReadInt(cfg, L"general", L"history_limit", 100), 10, 100);
    out.pinSavePath = ReadString(cfg, L"general", L"pin_save_path", historyDir_.wstring().c_str());
    if (out.pinSavePath.empty()) {
        out.pinSavePath = historyDir_.wstring();
    }
    std::error_code ec;
    std::filesystem::create_directories(out.pinSavePath, ec);
    out.fileNamePattern = ReadString(cfg, L"general", L"filename_pattern", L"yyyyMMdd_HHmmss");
    out.saveAsJpeg = ReadInt(cfg, L"general", L"save_as_jpeg", 0) != 0;
    out.paddleOcrApiUrl = ReadString(cfg, L"ocr", L"api_url", L"");
    out.paddleOcrAccessToken = ReadString(cfg, L"ocr", L"access_token", L"");

    if (out.showHistory.modifiers == 0 && out.showHistory.vk == VK_F4 &&
        out.closePins.modifiers == 0 && out.closePins.vk == VK_F5) {
        out.showHistory.vk = VK_F5;
        out.closePins.vk = VK_F4;
    }
    return true;
}

bool SettingsService::Save(const AppSettings& settings) {
    const auto cfg = configPath_.wstring();
    WriteInt(cfg, L"hotkeys", L"area_mod", settings.areaCapture.modifiers);
    WriteInt(cfg, L"hotkeys", L"area_vk", settings.areaCapture.vk);
    WriteInt(cfg, L"hotkeys", L"full_mod", settings.fullCapture.modifiers);
    WriteInt(cfg, L"hotkeys", L"full_vk", settings.fullCapture.vk);
    WriteInt(cfg, L"hotkeys", L"pin_mod", settings.pinLast.modifiers);
    WriteInt(cfg, L"hotkeys", L"pin_vk", settings.pinLast.vk);
    WriteInt(cfg, L"hotkeys", L"history_mod", settings.showHistory.modifiers);
    WriteInt(cfg, L"hotkeys", L"history_vk", settings.showHistory.vk);
    WriteInt(cfg, L"hotkeys", L"close_mod", settings.closePins.modifiers);
    WriteInt(cfg, L"hotkeys", L"close_vk", settings.closePins.vk);

    WriteInt(cfg, L"general", L"autostart", settings.autoStart ? 1 : 0);
    WriteInt(cfg, L"general", L"show_guide_lines", settings.showGuideLines ? 1 : 0);
    WriteInt(cfg, L"general", L"history_limit", std::clamp(settings.historyLimit, 10, 100));
    const std::wstring savePath = settings.pinSavePath.empty() ? historyDir_.wstring() : settings.pinSavePath;
    WritePrivateProfileStringW(L"general", L"pin_save_path", savePath.c_str(), cfg.c_str());
    WritePrivateProfileStringW(L"general", L"filename_pattern", settings.fileNamePattern.c_str(), cfg.c_str());
    WriteInt(cfg, L"general", L"save_as_jpeg", settings.saveAsJpeg ? 1 : 0);
    WritePrivateProfileStringW(L"ocr", L"api_url", settings.paddleOcrApiUrl.c_str(), cfg.c_str());
    WritePrivateProfileStringW(L"ocr", L"access_token", settings.paddleOcrAccessToken.c_str(), cfg.c_str());
    return true;
}
