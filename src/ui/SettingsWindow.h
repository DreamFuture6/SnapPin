#pragma once
#include "common.h"
#include "core/Settings.h"
#include "ui/widgets/CheckBox.h"
#include "ui/widgets/TextEdit.h"

class SettingsWindow {
public:
    using ApplyCallback = std::function<bool(const AppSettings&, std::wstring&)>;

    bool Show(HINSTANCE hInstance, HWND owner, const AppSettings& settings, ApplyCallback onApply);
    void Destroy();
    bool IsOpen() const { return hwnd_ != nullptr && IsWindow(hwnd_) != FALSE; }
    bool IsCapturingHotkey() const { return hotkeyCaptureIndex_ >= 0; }
    bool CaptureHotkeyFromRegistered(UINT modifiers, UINT vk);

private:
    enum class Category {
        General = 0,
        Hotkeys = 1,
        Save = 2,
        ImportExport = 3,
        Ocr = 4,
        About = 5,
    };

    static constexpr int kHotkeyCount = 5;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void EnsureWindow(HINSTANCE hInstance, HWND owner);
    void CreateControls();
    void EnsureFont();
    void Layout();
    void SetCategory(Category category);
    void UpdateVisiblePanel();

    bool IsModifierVk(UINT vk) const;
    std::wstring HotkeyToText(const HotkeyConfig& hk) const;
    void SetHotkeyCaptureIndex(int index);
    void UpdateHotkeyConflictState();
    bool HasHotkeyConflicts() const;

    void ApplyToControls();
    bool ReadFromControls(AppSettings& out, std::wstring& error) const;
    void OnApplyClicked();
    void OnImportClicked();
    void OnExportClicked();
    void OnResetDefaultsClicked();
    void OnChoosePinSavePathClicked();
    void OpenAuthorLink() const;
    void OpenPaddleOcrLink() const;

    bool LoadSettingsFromIni(const std::filesystem::path& path, AppSettings& out) const;
    bool SaveSettingsToIni(const std::filesystem::path& path, const AppSettings& in) const;
    std::filesystem::path DesktopDirectory() const;

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;

    HFONT font_ = nullptr;
    HFONT fontSmall_ = nullptr;
    HFONT fontTitle_ = nullptr;
    HBRUSH windowBrush_ = nullptr;
    HBRUSH panelBrush_ = nullptr;
    HBRUSH editBrush_ = nullptr;

    HWND lstCategories_ = nullptr;
    HWND lblTitle_ = nullptr;

    HWND panelGeneral_ = nullptr;
    HWND panelHotkeys_ = nullptr;
    HWND panelSave_ = nullptr;
    HWND panelImportExport_ = nullptr;
    HWND panelOcr_ = nullptr;
    HWND panelAbout_ = nullptr;

    CheckBoxControl chkAutoStart_;
    CheckBoxControl chkGuideLines_;
    HWND lblHistoryLimit_ = nullptr;
    HWND lblHistoryHint_ = nullptr;
    TextEditControl historyLimitEdit_;
    HWND lblPaddleOcrApiUrl_ = nullptr;
    TextEditControl paddleOcrApiUrlEdit_;
    HWND lblPaddleOcrToken_ = nullptr;
    TextEditControl paddleOcrTokenEdit_;
    HWND lblPaddleOcrHint_ = nullptr;
    HWND lblPaddleOcrLinkPrefix_ = nullptr;
    HWND btnPaddleOcrLink_ = nullptr;

    std::array<HWND, kHotkeyCount> lblHotkey_{};
    std::array<HWND, kHotkeyCount> cmbHotkeyMods_{};

    HWND lblPinSavePath_ = nullptr;
    HWND btnPinSavePath_ = nullptr;
    HWND lblFileNamePattern_ = nullptr;
    TextEditControl fileNamePatternEdit_;
    CheckBoxControl chkSaveAsJpeg_;

    HWND btnImport_ = nullptr;
    HWND btnExport_ = nullptr;
    HWND btnResetDefaults_ = nullptr;
    HWND lblImportTip_ = nullptr;

    HWND lblVersion_ = nullptr;
    HWND lblAuthorPrefix_ = nullptr;
    HWND lblAuthor_ = nullptr;
    HWND lblBuild_ = nullptr;
    HWND panelCard_ = nullptr;

    HWND btnApply_ = nullptr;
    HWND btnClose_ = nullptr;

    Category category_ = Category::General;
    AppSettings current_{};
    std::array<HotkeyConfig, kHotkeyCount> hotkeyDraft_{};
    std::array<bool, kHotkeyCount> hotkeyConflicts_{};
    std::wstring uiPinSavePath_;
    int uiHistoryLimit_ = 100;
    int hotkeyCaptureIndex_ = -1;
    ApplyCallback onApply_;
};
