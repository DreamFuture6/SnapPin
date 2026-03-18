#include "app/AppController.h"
#include "app/AppIds.h"
#include "core/CrashHandler.h"
#include "core/ImageCodecUtil.h"
#include "core/Logger.h"
#include "common/KnownFolderUtil.h"
#include "common/WindowMessagePayload.h"
#include "ui/WindowUtil.h"
#include <thread>

namespace {
AppController* g_appController = nullptr;
constexpr wchar_t kMainWindowClassName[] = L"SnapPinMainHiddenWindowClass";

struct PendingOcrResult {
    OcrResult result;
    std::optional<RECT> selectionScreenRect;
};

bool IsOcrNotificationWhitespace(wchar_t ch) {
    return ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r' || ch == 0x3000;
}

std::wstring BuildOcrNotificationPreview(const std::wstring& text) {
    constexpr size_t kMaxPreviewChars = 220;

    std::wstring preview;
    preview.reserve((std::min)(text.size(), kMaxPreviewChars));
    bool lastWasSpace = false;
    bool truncated = false;

    for (wchar_t ch : text) {
        if (IsOcrNotificationWhitespace(ch)) {
            if (!preview.empty() && !lastWasSpace) {
                preview.push_back(L' ');
                lastWasSpace = true;
            }
            continue;
        }

        if (preview.size() >= kMaxPreviewChars) {
            truncated = true;
            break;
        }

        preview.push_back(ch);
        lastWasSpace = false;
    }

    while (!preview.empty() && preview.back() == L' ') {
        preview.pop_back();
    }

    if (preview.empty()) {
        return L"OCR 已完成，但没有可显示的文本。";
    }

    if (truncated) {
        preview.push_back(L'…');
    }
    return preview;
}

std::wstring NowText() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[64]{};
    swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::filesystem::path GetDesktopDirectory() {
    return KnownFolderUtil::GetPathOr(FOLDERID_Desktop, std::filesystem::temp_directory_path());
}

std::filesystem::path EffectivePinSaveDir(const AppSettings& settings, const SettingsService& service) {
    if (!settings.pinSavePath.empty()) {
        return std::filesystem::path(settings.pinSavePath);
    }
    return service.HistoryDir();
}

bool PathEqualsNoCase(const std::filesystem::path& a, const std::filesystem::path& b) {
    return _wcsicmp(a.wstring().c_str(), b.wstring().c_str()) == 0;
}

bool WriteHistoryIndexFile(const std::filesystem::path& rootDir, const std::vector<HistoryItem>& items) {
    std::error_code ec;
    std::filesystem::create_directories(rootDir, ec);
    std::wofstream out(rootDir / L"history_index.txt", std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    for (const auto& it : items) {
        out << it.createdAt << L"|" << it.width << L"|" << it.height << L"|" << it.filePath << L"\n";
    }
    return true;
}

bool LoadImageFromFile(const std::filesystem::path& path, Image& out) {
    if (!ImageCodecUtil::EnsureGdiplus()) {
        return false;
    }

    Gdiplus::Bitmap bmp(path.c_str());
    if (bmp.GetLastStatus() != Gdiplus::Ok) {
        return false;
    }

    const int w = static_cast<int>(bmp.GetWidth());
    const int h = static_cast<int>(bmp.GetHeight());
    if (w <= 0 || h <= 0) {
        return false;
    }

    out = Image::Create(w, h);
    Gdiplus::Rect rect(0, 0, w, h);
    Gdiplus::BitmapData data{};
    if (bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return false;
    }

    for (int y = 0; y < h; ++y) {
        const uint8_t* src = static_cast<const uint8_t*>(data.Scan0) + static_cast<size_t>(data.Stride) * static_cast<size_t>(y);
        uint8_t* dst = out.bgra.data() + static_cast<size_t>(w) * static_cast<size_t>(y) * 4;
        memcpy(dst, src, static_cast<size_t>(w) * 4);
    }
    bmp.UnlockBits(&data);
    return true;
}

}

bool AppController::Initialize(HINSTANCE hInstance) {
    hInstance_ = hInstance;
    g_appController = this;

    if (!settingsService_.Initialize()) {
        return false;
    }
    settingsService_.Load(settings_);
    const auto pinSaveDir = EnsureValidPinSavePath(false);
    CrashHandler::Install(settingsService_.BaseDir() / L"Dumps");

    history_.Initialize(pinSaveDir, settings_.historyLimit);
    history_.Compact();
    LoadPinStates();

    static std::once_flag classOnce;
    WindowUtil::RegisterWindowClassOnce(
        classOnce,
        hInstance_,
        kMainWindowClassName,
        AppController::WndProc);

    hwnd_ = CreateWindowExW(
        0,
        kMainWindowClassName,
        L"SnapPinMain",
        WS_OVERLAPPED,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        nullptr,
        nullptr,
        hInstance_,
        this
    );

    if (!hwnd_) {
        return false;
    }

    if (!ImageCodecUtil::EnsureGdiplus()) {
        Logger::Instance().Error(L"GDI+ initialization failed during app startup.");
        MessageBoxW(hwnd_, L"GDI+ 初始化失败，截图与贴图功能无法使用。", L"SnapPin", MB_OK | MB_ICONERROR);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }

    WarmUpCaptureUi();

    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");

    if (!tray_.Initialize(hwnd_)) {
        MessageBoxW(hwnd_, L"托盘图标创建失败。", L"SnapPin", MB_ICONERROR);
    }

    std::wstring error;
    if (!hotkeys_.Register(hwnd_, settings_, error)) {
        MessageBoxW(hwnd_, (error + L"\n程序将继续运行，但快捷键功能不可用。").c_str(), L"快捷键注册失败", MB_OK | MB_ICONWARNING);
    }

    ApplyAutoStart(settings_.autoStart);

    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, AppController::KeyboardProc, nullptr, 0);

    Logger::Instance().Info(L"SnapPin initialized.");
    return true;
}

int AppController::Run() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void AppController::WarmUpCaptureUi() {
    OverlayWindow::PreloadUi(hInstance_);
    ToolbarWindow::WarmUp(hwnd_, hInstance_);
    PinWindow::PreloadClass(hInstance_);
}

LRESULT CALLBACK AppController::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppController* self = reinterpret_cast<AppController*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<AppController*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK AppController::KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && g_appController) {
        if (g_appController->settingsWindow_.IsOpen() && g_appController->settingsWindow_.IsCapturingHotkey()) {
            // While capturing a new hotkey in settings, bypass all app-level keyboard behaviors.
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }
        if (g_appController->overlay_ && g_appController->overlay_->IsOpen()) {
            if (g_appController->overlay_->IsEscPassthroughMode()) {
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }
            if (g_appController->overlay_->IsInputSuppressed()) {
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }
            const auto* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            const bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            if (k && k->vkCode == VK_ESCAPE) {
                if (HWND overlayHwnd = g_appController->overlay_->Hwnd()) {
                    PostMessageW(overlayHwnd, WM_KEYDOWN, VK_ESCAPE, 0);
                    return 1;
                }
            }
            if (k && !ctrlDown && (k->vkCode == 'C' || k->vkCode == 'c')) {
                if (g_appController->overlay_->TryHandleColorCopyHotkey()) {
                    g_appController->overlay_->CancelCapture();
                    return 1;
                }
            }
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }
        const auto* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (k && k->vkCode == VK_ESCAPE && g_appController->historyWindow_.IsVisible()) {
            const HWND historyHwnd = g_appController->historyWindow_.Hwnd();
            const HWND fg = GetForegroundWindow();
            if (historyHwnd && fg && (fg == historyHwnd || IsChild(historyHwnd, fg))) {
                g_appController->historyWindow_.Hide();
                return 1;
            }
        }
        if (k && (k->vkCode == VK_ESCAPE || k->vkCode == VK_DELETE)) {
            if (g_appController->HandlePinKey(static_cast<UINT>(k->vkCode))) {
                return 1;
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

bool AppController::HandlePinKey(UINT vk) {
    if (pins_.empty()) {
        return false;
    }

    PinWindow* targetPin = nullptr;
    std::wstring historyPath;
    const HWND fg = GetForegroundWindow();
    if (fg) {
        for (auto& entry : pins_) {
            if (!entry.window || !entry.window->Hwnd()) {
                continue;
            }
            if (entry.window->Hwnd() == fg) {
                targetPin = entry.window.get();
                historyPath = entry.historyPath;
                break;
            }
        }
    }

    if (!targetPin) {
        POINT pt{};
        GetCursorPos(&pt);
        for (auto& entry : pins_) {
            if (!entry.window || !entry.window->Hwnd()) {
                continue;
            }
            RECT rc{};
            if (GetWindowRect(entry.window->Hwnd(), &rc) && PtInRect(&rc, pt)) {
                targetPin = entry.window.get();
                historyPath = entry.historyPath;
                break;
            }
        }
    }

    if (!targetPin) {
        return false;
    }

    if (vk == VK_DELETE && !historyPath.empty()) {
        history_.RemoveByPath(historyPath, true);
        f3PinnedHistory_.erase(historyPath);
        pinStatesByHistory_.erase(historyPath);
        SavePinStates();
        if (historyWindow_.IsOpen()) {
            OpenHistoryWindow();
        }
    }

    targetPin->Destroy();
    return true;
}

void AppController::CloseAllPins() {
    std::vector<PinWindow*> toClose;
    toClose.reserve(pins_.size());
    for (auto& p : pins_) {
        if (p.window && p.window->Hwnd()) {
            toClose.push_back(p.window.get());
        }
    }
    for (auto* p : toClose) {
        p->Destroy();
    }
}

bool AppController::TogglePinsOnCurrentMonitorVisibility() {
    if (pins_.empty()) {
        return false;
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR currentMon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    if (!currentMon) {
        return false;
    }

    std::vector<PinEntry*> monitorPins;
    monitorPins.reserve(pins_.size());
    bool hasVisible = false;
    for (auto& entry : pins_) {
        if (!entry.window || !entry.window->Hwnd()) {
            continue;
        }
        RECT rc{};
        if (!GetWindowRect(entry.window->Hwnd(), &rc)) {
            continue;
        }
        POINT center{ (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
        HMONITOR mon = MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST);
        if (mon != currentMon) {
            continue;
        }
        monitorPins.push_back(&entry);
        if (!entry.hidden && IsWindowVisible(entry.window->Hwnd())) {
            hasVisible = true;
        }
    }

    if (monitorPins.empty()) {
        return false;
    }

    if (hasVisible) {
        for (auto* entry : monitorPins) {
            if (!entry->hidden && entry->window && entry->window->Hwnd()) {
                ShowWindow(entry->window->Hwnd(), SW_HIDE);
                entry->hidden = true;
            }
        }
    } else {
        for (auto* entry : monitorPins) {
            if (entry->hidden && entry->window && entry->window->Hwnd()) {
                ShowWindow(entry->window->Hwnd(), SW_SHOWNA);
                SetWindowPos(entry->window->Hwnd(), HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                entry->hidden = false;
            }
        }
    }
    return true;
}

int AppController::ClosePinsOnCurrentMonitor() {
    if (pins_.empty()) {
        return 0;
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR currentMon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    if (!currentMon) {
        return 0;
    }

    std::vector<PinWindow*> toClose;
    toClose.reserve(pins_.size());
    for (auto& entry : pins_) {
        if (!entry.window || !entry.window->Hwnd()) {
            continue;
        }
        RECT rc{};
        if (!GetWindowRect(entry.window->Hwnd(), &rc)) {
            continue;
        }
        POINT center{ (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
        HMONITOR mon = MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST);
        if (mon == currentMon) {
            toClose.push_back(entry.window.get());
        }
    }

    for (auto* p : toClose) {
        p->Destroy();
    }
    return static_cast<int>(toClose.size());
}

void AppController::OnPinControlHotkey() {
    const DWORD now = GetTickCount();
    constexpr DWORD kDoubleTapMs = 320;
    const bool isDoubleTap = (now - lastPinControlHotkeyTick_) <= kDoubleTapMs;
    lastPinControlHotkeyTick_ = now;
    if (isDoubleTap) {
        ClosePinsOnCurrentMonitor();
        tray_.ShowNotification(L"SnapPin", L"已清除固定的所有贴图。", 1200);
        return;
    }
    TogglePinsOnCurrentMonitorVisibility();
}

LRESULT AppController::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreatedMsg_ != 0 && msg == taskbarCreatedMsg_) {
        tray_.Restore();
        return 0;
    }

    if (overlay_ && overlay_->IsOpen() && overlay_->IsInputSuppressed()) {
        switch (msg) {
        case WM_HOTKEY:
        case WMAPP_TRAY:
        case WM_COMMAND:
            return 0;
        default:
            break;
        }
    }

    switch (msg) {
    case WM_HOTKEY:
        if (settingsWindow_.IsOpen() && settingsWindow_.IsCapturingHotkey()) {
            const UINT modifiers = LOWORD(static_cast<DWORD>(lParam));
            const UINT vk = HIWORD(static_cast<DWORD>(lParam));
            settingsWindow_.CaptureHotkeyFromRegistered(modifiers, vk);
            return 0;
        }
        switch (wParam) {
        case HOTKEY_ID_AREA: StartAreaCapture(); break;
        case HOTKEY_ID_FULL: StartFullCapture(); break;
        case HOTKEY_ID_PIN_LAST: {
            const DWORD now = GetTickCount();
            if (now - lastPinHotkeyTick_ < 120) {
                break;
            }
            lastPinHotkeyTick_ = now;
            try {
                const bool pinned = PinLastCapture();
                if (!pinned) {
                    if (now - lastPinMissTick_ <= 1600) {
                        ++pinMissBurstCount_;
                    } else {
                        pinMissBurstCount_ = 1;
                    }
                    lastPinMissTick_ = now;
                    if (pinMissBurstCount_ >= 2 && (now - lastPinMissToastTick_ > 3000)) {
                        tray_.ShowNotification(L"SnapPin", L"没有可贴出的历史截图。");
                        lastPinMissToastTick_ = now;
                    }
                } else {
                    pinMissBurstCount_ = 0;
                }
            } catch (const std::exception& ex) {
                Logger::Instance().Error(L"F3 hotkey failed: " + Utf8ToWide(ex.what()));
            } catch (...) {
                Logger::Instance().Error(L"F3 hotkey failed: unknown exception.");
            }
            break;
        }
        case HOTKEY_ID_HISTORY: {
            const DWORD now = GetTickCount();
            if (now - lastHistoryHotkeyTick_ < 120) {
                break;
            }
            lastHistoryHotkeyTick_ = now;
            try {
                if (historyWindow_.IsVisible()) {
                    historyWindow_.Hide();
                } else {
                    OpenHistoryWindow();
                }
            } catch (const std::exception& ex) {
                Logger::Instance().Error(L"F5 hotkey failed: " + Utf8ToWide(ex.what()));
            } catch (...) {
                Logger::Instance().Error(L"F5 hotkey failed: unknown exception.");
            }
            break;
        }
        case HOTKEY_ID_CLOSE_PINS:
            OnPinControlHotkey();
            break;
        default: break;
        }
        return 0;
    case WMAPP_TRAY:
    {
        const UINT code = LOWORD(static_cast<DWORD>(lParam));
        if (code == WM_RBUTTONUP || code == WM_CONTEXTMENU) {
            POINT p{};
            p.x = GET_X_LPARAM(wParam);
            p.y = GET_Y_LPARAM(wParam);
            if ((p.x == 0 && p.y == 0) || (p.x == -1 && p.y == -1)) {
                GetCursorPos(&p);
            }
            tray_.ShowMenu(hwnd_, p, settings_.autoStart);
        }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_SETTINGS:
            OpenSettingsFile();
            break;
        case ID_TRAY_HISTORY:
            OpenHistoryWindow();
            break;
        case ID_TRAY_AUTOSTART:
            ToggleAutoStart();
            break;
        case ID_TRAY_EXIT:
            DestroyWindow(hwnd_);
            break;
        default:
            break;
        }
        return 0;
    case WMAPP_PIN_CLOSED:
        RemovePin(reinterpret_cast<PinWindow*>(wParam));
        return 0;
    case WMAPP_OCR_COMPLETE: {
        auto payload = WindowMessagePayload::Take<PendingOcrResult>(lParam);
        if (!payload) {
            return 0;
        }
        const bool success = payload->result.success;
        const std::wstring body = payload->result.text.empty()
            ? (success ? L"OCR 已完成，但没有可显示的文本。" : L"OCR 执行失败。")
            : payload->result.text;
        const bool copied = success && clipboard_.CopyText(hwnd_, body);
        const std::wstring successTitle = copied
            ? L"OCR 识别成功（已复制到剪贴板）"
            : L"OCR 识别成功";
        if (success) {
            if (!ocrResultPopup_.Show(hInstance_, hwnd_, successTitle, body, payload->selectionScreenRect)) {
                Logger::Instance().Error(L"OCR result popup failed to show.");
                tray_.ShowNotification(successTitle, BuildOcrNotificationPreview(body), 4500);
            }
        } else {
            tray_.ShowNotification(payload->result.title, BuildOcrNotificationPreview(body), 3000);
        }
        return 0;
    }
    case WM_DESTROY:
        SavePinStates();
        ocrResultPopup_.Close();
        settingsWindow_.Destroy();
        if (keyboardHook_) {
            UnhookWindowsHookEx(keyboardHook_);
            keyboardHook_ = nullptr;
        }
        g_appController = nullptr;
        tray_.Remove();
        hotkeys_.Unregister(hwnd_);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd_, msg, wParam, lParam);
    }
}

void AppController::StartAreaCapture() {
    if (overlay_ && overlay_->IsOpen()) {
        return;
    }

    ScreenCapture cap;
    if (!capture_.CaptureVirtualScreen(cap)) {
        MessageBoxW(hwnd_, L"截图失败。", L"SnapPin", MB_ICONERROR);
        return;
    }

    overlay_ = std::make_unique<OverlayWindow>();
    overlay_->SetGuideLinesEnabled(settings_.showGuideLines);
    if (!overlay_->Show(hInstance_, cap, false, [this](const OverlayResult& result) {
        HandleOverlayResult(result);
    })) {
        Logger::Instance().Error(L"OverlayWindow::Show failed for area capture.");
        overlay_.reset();
        MessageBoxW(hwnd_, L"截图界面打开失败。", L"SnapPin", MB_OK | MB_ICONERROR);
    }
}

void AppController::StartFullCapture() {
    if (overlay_ && overlay_->IsOpen()) {
        return;
    }

    ScreenCapture cap;
    if (!capture_.CaptureVirtualScreen(cap)) {
        MessageBoxW(hwnd_, L"截图失败。", L"SnapPin", MB_ICONERROR);
        return;
    }

    std::optional<RECT> currentMonitorRect;
    POINT cursor{};
    if (GetCursorPos(&cursor)) {
        HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (mon && GetMonitorInfoW(mon, &mi)) {
            currentMonitorRect = mi.rcMonitor;
        }
    }

    overlay_ = std::make_unique<OverlayWindow>();
    overlay_->SetGuideLinesEnabled(settings_.showGuideLines);
    if (!overlay_->Show(hInstance_, cap, true, [this](const OverlayResult& result) {
        HandleOverlayResult(result);
    }, currentMonitorRect)) {
        Logger::Instance().Error(L"OverlayWindow::Show failed for full capture.");
        overlay_.reset();
        MessageBoxW(hwnd_, L"截图界面打开失败。", L"SnapPin", MB_OK | MB_ICONERROR);
    }
}

bool AppController::PinLastCapture() {
    auto items = history_.List();
    if (items.empty()) {
        return false;
    }

    struct Candidate {
        std::wstring path;
        std::filesystem::file_time_type closedTime;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(items.size());

    for (const auto& it : items) {
        if (it.filePath.empty()) {
            continue;
        }
        if (f3PinnedHistory_.find(it.filePath) != f3PinnedHistory_.end()) {
            continue;
        }
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(it.filePath, ec);
        candidates.push_back(Candidate{
            it.filePath,
            ec ? std::filesystem::file_time_type::min() : t
        });
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.closedTime != b.closedTime) {
            return a.closedTime > b.closedTime;
        }
        return _wcsicmp(a.path.c_str(), b.path.c_str()) < 0;
    });

    for (const auto& c : candidates) {
        Image img;
        if (!LoadImageFromFile(c.path, img)) {
            continue;
        }

        std::optional<PinWindow::State> initialState;
        auto itState = pinStatesByHistory_.find(c.path);
        if (itState != pinStatesByHistory_.end()) {
            initialState = itState->second;
        }

        if (AddPin(img, c.path, std::nullopt, initialState)) {
            lastImage_ = img;
            lastHistoryPath_ = c.path;
            return true;
        }
    }
    return false;
}

void AppController::StartOcrAsync(Image image, const std::optional<RECT>& selectionScreenRect) {
    if (!image.IsValid()) {
        tray_.ShowNotification(L"SnapPin", L"OCR 失败：当前图像无效。", 1800);
        return;
    }

    Logger::Instance().Info(L"OCR started.");

    const HWND owner = hwnd_;
    const OcrRequestConfig config{ settings_.paddleOcrApiUrl, settings_.paddleOcrAccessToken };
    std::thread([owner, service = ocr_, image = std::move(image), config, selectionScreenRect]() mutable {
        OcrResult result;
        try {
            result = service.Recognize(image, config);
        } catch (const std::exception& ex) {
            result.success = false;
            result.title = L"OCR 识别失败";
            result.text = L"OCR 线程异常: " + Utf8ToWide(ex.what());
            Logger::Instance().Error(result.text);
        } catch (...) {
            result.success = false;
            result.title = L"OCR 识别失败";
            result.text = L"OCR 线程异常: 未知错误。";
            Logger::Instance().Error(result.text);
        }

        auto payload = std::make_unique<PendingOcrResult>(PendingOcrResult{ std::move(result), selectionScreenRect });
        if (!WindowMessagePayload::Post(owner, WMAPP_OCR_COMPLETE, 0, std::move(payload))) {
            Logger::Instance().Error(L"OCR result dispatch failed.");
        }
    }).detach();
}

void AppController::HandleOverlayResult(const OverlayResult& result) {
    overlay_.reset();

    if (result.action == OverlayAction::Cancel || !result.image.IsValid()) {
        return;
    }

    lastImage_ = result.image;
    const auto rememberDefaultPinState = [&](const std::optional<std::filesystem::path>& savedPath) {
        if (!savedPath.has_value() || !result.selectionScreenRect.has_value()) {
            return;
        }
        PinWindow::State state{};
        state.windowRect = *result.selectionScreenRect;
        state.zoom = 1.0f;
        pinStatesByHistory_[savedPath->wstring()] = state;
        SavePinStates();
    };

    switch (result.action) {
    case OverlayAction::Save: {
        wchar_t path[MAX_PATH]{};
        const auto defaultPath = BuildOutputPath(settings_.saveAsJpeg, false);
        const auto defaultName = defaultPath.filename().wstring();
        wcscpy_s(path, defaultName.c_str());
        const std::wstring initialDir = GetDesktopDirectory().wstring();

        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd_;
        ofn.lpstrFilter = L"PNG (*.png)\0*.png\0JPEG (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrInitialDir = initialDir.c_str();
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        ofn.lpstrDefExt = settings_.saveAsJpeg ? L"jpg" : L"png";

        if (GetSaveFileNameW(&ofn)) {
            std::filesystem::path output = path;
            const auto ext = output.extension().wstring();
            const bool jpeg = (_wcsicmp(ext.c_str(), L".jpg") == 0 || _wcsicmp(ext.c_str(), L".jpeg") == 0);
            const auto saved = SaveAndRecord(result.image, jpeg, false, output);
            rememberDefaultPinState(saved);
        }
        break;
    }
    case OverlayAction::Copy: {
        clipboard_.CopyImage(hwnd_, result.image);
        const auto saved = SaveAndRecord(result.image, false, false, BuildOutputPath(false, false));
        rememberDefaultPinState(saved);
        break;
    }
    case OverlayAction::QuickSave: {
        const auto saved = SaveAndRecord(result.image, false, false, BuildOutputPath(false, false));
        rememberDefaultPinState(saved);
        break;
    }
    case OverlayAction::CopyFile: {
        const auto tmp = BuildOutputPath(false, true);
        const auto saved = SaveAndRecord(result.image, false, false, tmp);
        if (saved.has_value()) {
            clipboard_.CopyFilePath(hwnd_, *saved);
        }
        break;
    }
    case OverlayAction::Pin: {
        const auto saved = SaveAndRecord(result.image, false, false, BuildOutputPath(false, false));
        AddPin(result.image, saved.has_value() ? saved->wstring() : L"", result.selectionScreenRect);
        break;
    }
    case OverlayAction::Ocr:
        StartOcrAsync(result.image, result.selectionScreenRect);
        break;
    default:
        break;
    }
}

std::optional<std::filesystem::path> AppController::SaveAndRecord(const Image& image, bool useJpeg, bool openFolder, std::filesystem::path forcedPath) {
    if (!image.IsValid()) {
        return std::nullopt;
    }

    std::filesystem::path output = forcedPath.empty() ? BuildOutputPath(useJpeg, false) : forcedPath;
    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);

    if (!exporter_.SaveImage(image, output, useJpeg)) {
        MessageBoxW(hwnd_, L"保存失败。", L"SnapPin", MB_ICONERROR);
        return std::nullopt;
    }

    HistoryItem item;
    item.filePath = output.wstring();
    item.createdAt = NowText();
    item.width = image.width;
    item.height = image.height;
    history_.Add(item);
    PrunePinStates();
    SavePinStates();
    lastHistoryPath_ = item.filePath;

    if (openFolder) {
        ShellExecuteW(hwnd_, L"open", output.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    return output;
}

std::filesystem::path AppController::BuildOutputPath(bool jpeg, bool temp) {
    const auto ext = jpeg ? L".jpg" : L".png";
    const std::wstring baseName = FormatNowForFile();
    const auto dir = temp ? settingsService_.TempDir() : EnsureValidPinSavePath(true);

    std::filesystem::path path = dir / (baseName + ext);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return path;
    }

    for (int i = 1; i < 10000; ++i) {
        std::wstring name = baseName + L"_" + std::to_wstring(i) + ext;
        path = dir / name;
        ec.clear();
        if (!std::filesystem::exists(path, ec)) {
            return path;
        }
    }

    return dir / (baseName + L"_" + std::to_wstring(GetTickCount64()) + ext);
}

bool AppController::AddPin(const Image& image, const std::wstring& historyPath, const std::optional<RECT>& screenRect,
    const std::optional<PinWindow::State>& initialState) {
    std::optional<PinWindow::State> resolvedState = initialState;
    if (!resolvedState.has_value() && !historyPath.empty()) {
        auto it = pinStatesByHistory_.find(historyPath);
        if (it != pinStatesByHistory_.end()) {
            resolvedState = it->second;
        }
    }

    const std::wstring historyKey = historyPath;
    auto pin = std::make_unique<PinWindow>();
    if (!pin->Create(
        hInstance_,
        image,
        screenRect,
        resolvedState,
        [this, historyKey](PinWindow* p, const PinWindow::State& state) {
            if (!historyKey.empty()) {
                pinStatesByHistory_[historyKey] = state;
                std::error_code ec;
                std::filesystem::last_write_time(historyKey, std::filesystem::file_time_type::clock::now(), ec);
                f3PinnedHistory_.erase(historyKey);
                SavePinStates();
            }
            if (hwnd_) {
                PostMessageW(hwnd_, WMAPP_PIN_CLOSED, reinterpret_cast<WPARAM>(p), 0);
            }
        },
        [this, historyKey](const PinWindow::State& state) {
            if (!historyKey.empty()) {
                pinStatesByHistory_[historyKey] = state;
            }
        })) {
        return false;
    }
    PinEntry entry;
    entry.window = std::move(pin);
    entry.historyPath = historyPath;
    pins_.push_back(std::move(entry));
    if (!historyPath.empty()) {
        f3PinnedHistory_.insert(historyPath);
    }
    return true;
}

void AppController::RemovePin(PinWindow* pin) {
    std::wstring historyPath;
    auto it = std::find_if(pins_.begin(), pins_.end(), [pin](const PinEntry& entry) {
        return entry.window.get() == pin;
    });
    if (it != pins_.end()) {
        historyPath = it->historyPath;
        pins_.erase(it);
    }
    if (!historyPath.empty()) {
        f3PinnedHistory_.erase(historyPath);
    }
}

void AppController::ToggleAutoStart() {
    settings_.autoStart = !settings_.autoStart;
    if (!ApplyAutoStart(settings_.autoStart)) {
        settings_.autoStart = !settings_.autoStart;
    } else {
        settingsService_.Save(settings_);
    }
}

bool AppController::ApplyAutoStart(bool enabled) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    const wchar_t* valueName = L"SnapPin";
    LONG rc = ERROR_SUCCESS;
    if (enabled) {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        rc = RegSetValueExW(hKey, valueName, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(exe), static_cast<DWORD>((wcslen(exe) + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(hKey, valueName);
        if (rc == ERROR_FILE_NOT_FOUND) {
            rc = ERROR_SUCCESS;
        }
    }

    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
}

std::filesystem::path AppController::EnsureValidPinSavePath(bool reinitHistory) {
    std::filesystem::path currentDir = EffectivePinSaveDir(settings_, settingsService_);
    std::error_code ec;
    const bool exists = std::filesystem::exists(currentDir, ec);
    const bool isDir = exists && !ec && std::filesystem::is_directory(currentDir, ec);
    if (isDir && !ec) {
        return currentDir;
    }

    const std::filesystem::path fallback = settingsService_.HistoryDir();
    std::error_code mkEc;
    std::filesystem::create_directories(fallback, mkEc);
    settings_.pinSavePath = fallback.wstring();
    settingsService_.Save(settings_);

    if (reinitHistory) {
        history_.Initialize(fallback, settings_.historyLimit);
        history_.Compact();
    }
    return fallback;
}

bool AppController::MigrateHistoryStorage(const std::filesystem::path& oldDir, const std::filesystem::path& newDir, std::wstring& error) {
    if (PathEqualsNoCase(oldDir, newDir)) {
        return true;
    }

    std::error_code ec;
    std::filesystem::create_directories(newDir, ec);
    if (ec) {
        error = L"新贴图保存路径不可用。";
        return false;
    }

    auto items = history_.List();
    if (items.empty()) {
        return true;
    }

    struct MovedPair {
        std::filesystem::path from;
        std::filesystem::path to;
    };
    std::vector<MovedPair> moved;
    moved.reserve(items.size());

    std::vector<HistoryItem> migratedItems = items;
    auto makeTarget = [&newDir](const std::filesystem::path& src) {
        std::filesystem::path target = newDir / src.filename();
        if (!std::filesystem::exists(target)) {
            return target;
        }
        const auto stem = src.stem().wstring();
        const auto ext = src.extension().wstring();
        for (int i = 1; i < 100000; ++i) {
            target = newDir / (stem + L"_" + std::to_wstring(i) + ext);
            if (!std::filesystem::exists(target)) {
                return target;
            }
        }
        return newDir / (stem + L"_" + std::to_wstring(GetTickCount64()) + ext);
    };

    for (size_t i = 0; i < items.size(); ++i) {
        const std::filesystem::path src(items[i].filePath);
        std::filesystem::path dst = makeTarget(src);
        if (PathEqualsNoCase(src, dst)) {
            migratedItems[i].filePath = dst.wstring();
            continue;
        }

        if (!MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_COPY_ALLOWED)) {
            const DWORD le = GetLastError();
            for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
                MoveFileExW(it->to.c_str(), it->from.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING);
            }
            error = (le == ERROR_SHARING_VIOLATION || le == ERROR_ACCESS_DENIED)
                ? L"迁移失败：存在文件占用。已回滚路径设置。"
                : L"迁移失败：无法移动历史文件。已回滚路径设置。";
            return false;
        }
        moved.push_back({ src, dst });
        migratedItems[i].filePath = dst.wstring();
    }

    if (!WriteHistoryIndexFile(newDir, migratedItems)) {
        for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
            MoveFileExW(it->to.c_str(), it->from.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING);
        }
        error = L"迁移失败：写入新历史索引失败。已回滚路径设置。";
        return false;
    }

    auto remapPath = [&moved](const std::wstring& path) -> std::wstring {
        for (const auto& m : moved) {
            if (_wcsicmp(path.c_str(), m.from.c_str()) == 0) {
                return m.to.wstring();
            }
        }
        return path;
    };

    lastHistoryPath_ = remapPath(lastHistoryPath_);
    for (auto& p : pins_) {
        p.historyPath = remapPath(p.historyPath);
    }

    std::unordered_set<std::wstring> remappedPinned;
    remappedPinned.reserve(f3PinnedHistory_.size());
    for (const auto& p : f3PinnedHistory_) {
        remappedPinned.insert(remapPath(p));
    }
    f3PinnedHistory_ = std::move(remappedPinned);

    std::unordered_map<std::wstring, PinWindow::State> remappedStates;
    remappedStates.reserve(pinStatesByHistory_.size());
    for (const auto& [path, state] : pinStatesByHistory_) {
        remappedStates[remapPath(path)] = state;
    }
    pinStatesByHistory_ = std::move(remappedStates);
    SavePinStates();
    return true;
}

bool AppController::ApplySettingsFromWindow(const AppSettings& next, std::wstring& error) {
    const AppSettings old = settings_;
    const auto rollbackRuntimeChanges = [this, &old, &next]() {
        if (next.autoStart != old.autoStart) {
            ApplyAutoStart(old.autoStart);
        }
        std::wstring rollbackErr;
        hotkeys_.Unregister(hwnd_);
        hotkeys_.Register(hwnd_, old, rollbackErr);
    };

    if (next.autoStart != old.autoStart) {
        if (!ApplyAutoStart(next.autoStart)) {
            error = L"开机启动设置应用失败。";
            return false;
        }
    }

    hotkeys_.Unregister(hwnd_);
    std::wstring hotkeyErr;
    if (!hotkeys_.Register(hwnd_, next, hotkeyErr)) {
        rollbackRuntimeChanges();
        error = hotkeyErr.empty() ? L"快捷键注册失败。" : hotkeyErr;
        return false;
    }

    const std::filesystem::path oldDir = EffectivePinSaveDir(old, settingsService_);
    const std::filesystem::path newDir = EffectivePinSaveDir(next, settingsService_);
    if (!PathEqualsNoCase(oldDir, newDir)) {
        std::wstring migrateErr;
        if (!MigrateHistoryStorage(oldDir, newDir, migrateErr)) {
            rollbackRuntimeChanges();
            error = migrateErr.empty() ? L"迁移贴图保存路径失败。已回滚。" : migrateErr;
            return false;
        }
    }

    settings_ = next;
    settingsService_.Save(settings_);
    history_.Initialize(EnsureValidPinSavePath(false), settings_.historyLimit);
    history_.Compact();
    return true;
}

void AppController::OpenSettingsFile() {
    if (!settingsWindow_.Show(
        hInstance_,
        hwnd_,
        settings_,
        [this](const AppSettings& updated, std::wstring& err) {
            return ApplySettingsFromWindow(updated, err);
        })) {
        MessageBoxW(hwnd_, L"设置窗口打开失败。", L"SnapPin", MB_OK | MB_ICONERROR);
    }
}

void AppController::OpenHistoryWindow() {
    auto items = history_.List();
    historyWindow_.Show(hInstance_, items, [this](HistoryWindow::Action action, const std::optional<HistoryItem>& itemOpt, const std::wstring& extra) {
        if (action == HistoryWindow::Action::ClearAll) {
            CloseAllPins();
            history_.ClearAll(true);
            f3PinnedHistory_.clear();
            pinStatesByHistory_.clear();
            SavePinStates();
            OpenHistoryWindow();
            return;
        }

        if (!itemOpt.has_value()) {
            return;
        }
        const auto& item = *itemOpt;

        if (action == HistoryWindow::Action::Rename) {
            std::wstring newName = extra;
            const size_t first = newName.find_first_not_of(L" \t\r\n");
            if (first == std::wstring::npos) {
                MessageBoxW(hwnd_, L"文件名不能为空。", L"SnapPin", MB_ICONWARNING);
                return;
            }
            const size_t last = newName.find_last_not_of(L" \t\r\n");
            newName = newName.substr(first, last - first + 1);
            if (newName.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
                MessageBoxW(hwnd_, L"文件名包含非法字符。", L"SnapPin", MB_ICONWARNING);
                return;
            }

            historyWindow_.ReleasePreview();
            std::filesystem::path oldPath(item.filePath);
            std::filesystem::path newPath = oldPath.parent_path() / (newName + oldPath.extension().wstring());
            if (_wcsicmp(oldPath.c_str(), newPath.c_str()) == 0) {
                return;
            }
            if (std::filesystem::exists(newPath)) {
                MessageBoxW(hwnd_, L"目标文件名已存在。", L"SnapPin", MB_ICONWARNING);
                return;
            }

            std::error_code ec;
            std::filesystem::rename(oldPath, newPath, ec);
            if (ec) {
                MessageBoxW(hwnd_, L"重命名失败。", L"SnapPin", MB_ICONERROR);
                return;
            }
            history_.RenamePath(oldPath.wstring(), newPath.wstring());
            auto itPinned = f3PinnedHistory_.find(oldPath.wstring());
            if (itPinned != f3PinnedHistory_.end()) {
                f3PinnedHistory_.erase(itPinned);
                f3PinnedHistory_.insert(newPath.wstring());
            }
            auto itState = pinStatesByHistory_.find(oldPath.wstring());
            if (itState != pinStatesByHistory_.end()) {
                pinStatesByHistory_[newPath.wstring()] = itState->second;
                pinStatesByHistory_.erase(itState);
                SavePinStates();
            }
            if (_wcsicmp(lastHistoryPath_.c_str(), oldPath.c_str()) == 0) {
                lastHistoryPath_ = newPath.wstring();
            }
            for (auto& p : pins_) {
                if (_wcsicmp(p.historyPath.c_str(), oldPath.c_str()) == 0) {
                    p.historyPath = newPath.wstring();
                }
            }
            OpenHistoryWindow();
            return;
        }

        if (action == HistoryWindow::Action::OpenFile) {
            ShellExecuteW(hwnd_, L"open", item.filePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }

        Image img;
        if (!LoadImageFromFile(item.filePath, img)) {
            MessageBoxW(hwnd_, L"无法加载历史截图文件。", L"SnapPin", MB_ICONWARNING);
            return;
        }

        lastImage_ = img;
        lastHistoryPath_ = item.filePath;
        if (action == HistoryWindow::Action::Copy) {
            clipboard_.CopyImage(hwnd_, img);
        } else if (action == HistoryWindow::Action::Pin) {
            AddPin(img, item.filePath);
        }
    });
}

std::filesystem::path AppController::PinStateIndexPath() const {
    return settingsService_.BaseDir() / L"pin_states.txt";
}

void AppController::PrunePinStates() {
    for (auto it = pinStatesByHistory_.begin(); it != pinStatesByHistory_.end();) {
        std::error_code ec;
        if (it->first.empty() || !std::filesystem::exists(it->first, ec) || ec) {
            it = pinStatesByHistory_.erase(it);
            continue;
        }
        ++it;
    }
}

void AppController::LoadPinStates() {
    pinStatesByHistory_.clear();
    const auto file = PinStateIndexPath();
    std::wifstream in(file);
    if (!in.is_open()) {
        return;
    }

    std::wstring line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }

        std::wistringstream ss(line);
        std::wstring path;
        std::wstring left;
        std::wstring top;
        std::wstring right;
        std::wstring bottom;
        std::wstring zoomMilli;
        if (!std::getline(ss, path, L'|') ||
            !std::getline(ss, left, L'|') ||
            !std::getline(ss, top, L'|') ||
            !std::getline(ss, right, L'|') ||
            !std::getline(ss, bottom, L'|') ||
            !std::getline(ss, zoomMilli)) {
            continue;
        }

        PinWindow::State state{};
        state.windowRect.left = _wtoi(left.c_str());
        state.windowRect.top = _wtoi(top.c_str());
        state.windowRect.right = _wtoi(right.c_str());
        state.windowRect.bottom = _wtoi(bottom.c_str());
        state.zoom = std::clamp(static_cast<float>(_wtoi(zoomMilli.c_str())) / 1000.0f, 0.2f, 2.5f);
        if (!IsRectValid(state.windowRect)) {
            continue;
        }

        std::error_code ec;
        if (path.empty() || !std::filesystem::exists(path, ec) || ec) {
            continue;
        }
        pinStatesByHistory_[path] = state;
    }
    PrunePinStates();
}

void AppController::SavePinStates() {
    PrunePinStates();
    const auto file = PinStateIndexPath();
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    std::wofstream out(file, std::ios::trunc);
    if (!out.is_open()) {
        return;
    }

    for (const auto& [path, state] : pinStatesByHistory_) {
        if (path.empty() || !IsRectValid(state.windowRect)) {
            continue;
        }
        const int zoomMilli = std::clamp(static_cast<int>(std::lround(state.zoom * 1000.0f)), 200, 2500);
        out << path
            << L"|" << state.windowRect.left
            << L"|" << state.windowRect.top
            << L"|" << state.windowRect.right
            << L"|" << state.windowRect.bottom
            << L"|" << zoomMilli
            << L"\n";
    }
}
