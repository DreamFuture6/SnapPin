#include "core/MediaFoundationUtil.h"

#include <mfapi.h>

namespace MediaFoundationUtil {

namespace {

std::once_flag g_mfStartupOnce;
std::atomic<bool> g_mfStarted = false;

} // namespace

bool EnsureStartup()
{
    std::call_once(g_mfStartupOnce, []() {
        const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        g_mfStarted.store(SUCCEEDED(hr), std::memory_order_release);
    });
    return g_mfStarted.load(std::memory_order_acquire);
}

void ShutdownForProcessExit()
{
    if (g_mfStarted.exchange(false, std::memory_order_acq_rel)) {
        MFShutdown();
    }
}

std::wstring HResultToText(HRESULT hr)
{
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(hr));
    return buffer;
}

} // namespace MediaFoundationUtil
