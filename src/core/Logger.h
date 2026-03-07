#pragma once
#include "common.h"

class Logger {
public:
    static Logger& Instance();

    void Initialize(const std::filesystem::path& baseDir);
    void Info(const std::wstring& message);
    void Error(const std::wstring& message);

private:
    Logger() = default;
    void Write(const wchar_t* level, const std::wstring& message);

    std::mutex mutex_;
    std::filesystem::path filePath_;
    bool initialized_ = false;
};
