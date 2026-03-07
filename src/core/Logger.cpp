#include "core/Logger.h"

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Initialize(const std::filesystem::path& baseDir) {
    std::scoped_lock lock(mutex_);
    std::error_code ec;
    std::filesystem::create_directories(baseDir, ec);
    filePath_ = baseDir / L"SnapPin.log";
    initialized_ = true;
}

void Logger::Info(const std::wstring& message) {
    Write(L"INFO", message);
}

void Logger::Error(const std::wstring& message) {
    Write(L"ERROR", message);
}

void Logger::Write(const wchar_t* level, const std::wstring& message) {
    std::scoped_lock lock(mutex_);
    if (!initialized_) {
        return;
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);

    std::wofstream out(filePath_, std::ios::app);
    if (!out.is_open()) {
        return;
    }

    wchar_t prefix[128]{};
    swprintf_s(prefix, L"[%04d-%02d-%02d %02d:%02d:%02d.%03d][%s] ",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        level);

    out << prefix << message << L"\n";
}
