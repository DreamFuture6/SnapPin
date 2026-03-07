#pragma once
#include "common.h"
#include "app/HotkeyService.h"
#include "app/TrayService.h"
#include "core/CaptureService.h"
#include "core/ClipboardService.h"
#include "core/Exporter.h"
#include "core/HistoryService.h"
#include "core/OcrService.h"
#include "core/Settings.h"
#include "ui/HistoryWindow.h"
#include "ui/OverlayWindow.h"
#include "ui/OcrResultPopupWindow.h"
#include "ui/PinWindow.h"
#include "ui/SettingsWindow.h"
#include <unordered_set>

class AppController {
public:
    bool Initialize(HINSTANCE hInstance);
    int Run();

private:
    struct PinEntry {
        std::unique_ptr<PinWindow> window;
        std::wstring historyPath;
        bool hidden = false;
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    bool HandlePinKey(UINT vk);
    void CloseAllPins();
    bool TogglePinsOnCurrentMonitorVisibility();
    int ClosePinsOnCurrentMonitor();
    void OnPinControlHotkey();
    void WarmUpCaptureUi();

    void StartAreaCapture();
    void StartFullCapture();
    bool PinLastCapture();
    void StartOcrAsync(Image image, const std::optional<RECT>& selectionScreenRect);

    void HandleOverlayResult(const OverlayResult& result);
    std::optional<std::filesystem::path> SaveAndRecord(const Image& image, bool useJpeg = false, bool openFolder = false, std::filesystem::path forcedPath = {});
    std::filesystem::path BuildOutputPath(bool jpeg, bool temp = false);
    bool AddPin(const Image& image, const std::wstring& historyPath = L"", const std::optional<RECT>& screenRect = std::nullopt,
        const std::optional<PinWindow::State>& initialState = std::nullopt);
    void RemovePin(PinWindow* pin);

    void ToggleAutoStart();
    bool ApplyAutoStart(bool enabled);
    bool ApplySettingsFromWindow(const AppSettings& next, std::wstring& error);
    std::filesystem::path EnsureValidPinSavePath(bool reinitHistory);
    bool MigrateHistoryStorage(const std::filesystem::path& oldDir, const std::filesystem::path& newDir, std::wstring& error);
    void OpenSettingsFile();
    void OpenHistoryWindow();
    std::filesystem::path PinStateIndexPath() const;
    void LoadPinStates();
    void SavePinStates();
    void PrunePinStates();

    HINSTANCE hInstance_ = nullptr;
    HWND hwnd_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;

    SettingsService settingsService_;
    AppSettings settings_{};
    TrayService tray_;
    HotkeyService hotkeys_;

    CaptureService capture_;
    Exporter exporter_;
    ClipboardService clipboard_;
    HistoryService history_;
    OcrService ocr_;

    std::unique_ptr<OverlayWindow> overlay_;
    OcrResultPopupWindow ocrResultPopup_;
    HistoryWindow historyWindow_;
    SettingsWindow settingsWindow_;
    std::vector<PinEntry> pins_;

    HHOOK keyboardHook_ = nullptr;

    Image lastImage_{};
    std::wstring lastHistoryPath_;
    std::unordered_set<std::wstring> f3PinnedHistory_;
    std::unordered_map<std::wstring, PinWindow::State> pinStatesByHistory_;
    DWORD lastPinHotkeyTick_ = 0;
    DWORD lastHistoryHotkeyTick_ = 0;
    DWORD lastPinControlHotkeyTick_ = 0;
    DWORD lastPinMissToastTick_ = 0;
    DWORD lastPinMissTick_ = 0;
    int pinMissBurstCount_ = 0;
};
