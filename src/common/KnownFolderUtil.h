#pragma once

#include "common.h"

namespace KnownFolderUtil {

struct CoTaskMemStringDeleter {
    void operator()(PWSTR ptr) const noexcept {
        if (ptr) {
            CoTaskMemFree(ptr);
        }
    }
};

using UniqueCoTaskMemString = std::unique_ptr<wchar_t, CoTaskMemStringDeleter>;

inline std::optional<std::filesystem::path> TryGetPath(REFKNOWNFOLDERID id, DWORD flags = 0, HANDLE token = nullptr) {
    PWSTR rawPath = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(id, flags, token, &rawPath);
    UniqueCoTaskMemString scopedPath(rawPath);
    if (FAILED(hr) || !scopedPath || scopedPath.get()[0] == L'\0') {
        return std::nullopt;
    }
    return std::filesystem::path(scopedPath.get());
}

inline std::filesystem::path GetPathOr(REFKNOWNFOLDERID id, std::filesystem::path fallback,
                                       DWORD flags = 0, HANDLE token = nullptr) {
    if (const auto path = TryGetPath(id, flags, token)) {
        return *path;
    }
    return fallback;
}

} // namespace KnownFolderUtil
