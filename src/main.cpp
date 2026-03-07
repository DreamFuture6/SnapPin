#include "common.h"
#include "app/AppController.h"

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"SnapPin.SingleInstance.Mutex");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"SnapPin 已在运行。", L"SnapPin", MB_OK | MB_ICONINFORMATION);
        if (mutex) {
            CloseHandle(mutex);
        }
        return 0;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        CloseHandle(mutex);
        return -1;
    }

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    AppController app;
    if (!app.Initialize(hInstance)) {
        CoUninitialize();
        CloseHandle(mutex);
        return -2;
    }

    const int code = app.Run();

    CoUninitialize();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return code;
}
