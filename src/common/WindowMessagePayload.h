#pragma once

#include "common.h"

namespace WindowMessagePayload {

template <typename T>
bool Post(HWND targetHwnd, UINT message, WPARAM wParam, std::unique_ptr<T> payload) noexcept {
    if (!payload || !targetHwnd || !IsWindow(targetHwnd)) {
        return false;
    }
    T* raw = payload.release();
    if (!PostMessageW(targetHwnd, message, wParam, reinterpret_cast<LPARAM>(raw))) {
        delete raw;
        return false;
    }
    return true;
}

template <typename T>
std::unique_ptr<T> Take(LPARAM lParam) noexcept {
    return std::unique_ptr<T>(reinterpret_cast<T*>(lParam));
}

} // namespace WindowMessagePayload
