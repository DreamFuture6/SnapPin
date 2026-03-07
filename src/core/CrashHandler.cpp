#include "core/CrashHandler.h"
#include <dbghelp.h>

#pragma comment(lib, "dbghelp.lib")

namespace {
std::filesystem::path g_dumpDir;

LONG WINAPI DumpHandler(EXCEPTION_POINTERS* info) {
    std::error_code ec;
    std::filesystem::create_directories(g_dumpDir, ec);
    std::filesystem::path dumpPath = g_dumpDir / (L"crash_" + FormatNowForFile() + L".dmp");

    HANDLE hFile = CreateFileW(
        dumpPath.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exInfo{};
        exInfo.ThreadId = GetCurrentThreadId();
        exInfo.ExceptionPointers = info;
        exInfo.ClientPointers = FALSE;

        MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            hFile,
            MiniDumpNormal,
            info ? &exInfo : nullptr,
            nullptr,
            nullptr
        );
        CloseHandle(hFile);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}
}

void CrashHandler::Install(const std::filesystem::path& dumpDir) {
    g_dumpDir = dumpDir;
    SetUnhandledExceptionFilter(DumpHandler);
}
