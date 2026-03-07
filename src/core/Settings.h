#pragma once
#include "common.h"

struct HotkeyConfig {
    UINT modifiers = 0;
    UINT vk = 0;
};

struct AppSettings {
    HotkeyConfig areaCapture{0, VK_F1};
    HotkeyConfig fullCapture{0, VK_F2};
    HotkeyConfig pinLast{0, VK_F3};
    HotkeyConfig showHistory{0, VK_F5};
    HotkeyConfig closePins{0, VK_F4};
    bool autoStart = false;
    bool showGuideLines = true;
    int historyLimit = 100;
    std::wstring pinSavePath;
    std::wstring fileNamePattern = L"yyyyMMdd_HHmmss";
    bool saveAsJpeg = false;
    std::wstring paddleOcrApiUrl;
    std::wstring paddleOcrAccessToken;
};

class SettingsService {
public:
    bool Initialize();
    bool Load(AppSettings& out);
    bool Save(const AppSettings& settings);

    const std::filesystem::path& BaseDir() const { return baseDir_; }
    const std::filesystem::path& HistoryDir() const { return historyDir_; }
    const std::filesystem::path& TempDir() const { return tempDir_; }
    const std::filesystem::path& ConfigPath() const { return configPath_; }

private:
    std::filesystem::path baseDir_;
    std::filesystem::path historyDir_;
    std::filesystem::path tempDir_;
    std::filesystem::path configPath_;
};
