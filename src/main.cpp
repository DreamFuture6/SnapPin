#include "common.h"
#include "app/AppController.h"

namespace {
class ScopedKernelHandle {
public:
    explicit ScopedKernelHandle(HANDLE handle = nullptr)
        : handle_(handle) {}

    ~ScopedKernelHandle() {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

    ScopedKernelHandle(const ScopedKernelHandle&) = delete;
    ScopedKernelHandle& operator=(const ScopedKernelHandle&) = delete;

    HANDLE Get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE handle_ = nullptr;
};

class ScopedCoInitialize {
public:
    explicit ScopedCoInitialize(DWORD coInitFlags)
        : hr_(CoInitializeEx(nullptr, coInitFlags)) {}

    ~ScopedCoInitialize() {
        if (SUCCEEDED(hr_)) {
            CoUninitialize();
        }
    }

    HRESULT Result() const { return hr_; }

private:
    HRESULT hr_ = E_FAIL;
};
} // namespace

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    ScopedKernelHandle mutex(
        CreateMutexW(nullptr, TRUE, L"SnapPin.SingleInstance.Mutex"));
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"SnapPin 已在运行。", L"SnapPin", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    ScopedCoInitialize coInit(COINIT_APARTMENTTHREADED);
    if (FAILED(coInit.Result())) {
        return -1;
    }

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    AppController app;
    if (!app.Initialize(hInstance)) {
        return -2;
    }

    const int code = app.Run();

    ReleaseMutex(mutex.Get());
    return code;
}
