#pragma once
#include "common.h"
#include "core/Image.h"

class ClipboardService {
public:
    ClipboardService() = default;
    ~ClipboardService() = default;

    bool CopyImage(HWND owner, const Image& image) const;
    bool CopyText(HWND owner, const std::wstring& text) const;
    bool CopyFilePath(HWND owner, const std::filesystem::path& path) const;

};
