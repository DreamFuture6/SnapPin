#pragma once

#include "common.h"

namespace UiGdi {

template <typename HandleT>
void ResetGdiObject(HandleT& handle) {
    if (handle) {
        DeleteObject(handle);
        handle = nullptr;
    }
}

template <typename HandleT>
class ScopedGdiObject {
public:
    explicit ScopedGdiObject(HandleT handle = nullptr)
        : handle_(handle) {
    }

    ~ScopedGdiObject() {
        ResetGdiObject(handle_);
    }

    ScopedGdiObject(const ScopedGdiObject&) = delete;
    ScopedGdiObject& operator=(const ScopedGdiObject&) = delete;

    ScopedGdiObject(ScopedGdiObject&& other) noexcept
        : handle_(other.Release()) {
    }

    ScopedGdiObject& operator=(ScopedGdiObject&& other) noexcept {
        if (this != &other) {
            ResetGdiObject(handle_);
            handle_ = other.Release();
        }
        return *this;
    }

    HandleT Get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

    HandleT Release() {
        const HandleT released = handle_;
        handle_ = nullptr;
        return released;
    }

private:
    HandleT handle_ = nullptr;
};

class ScopedSelectObject {
public:
    ScopedSelectObject(HDC dc, HGDIOBJ object)
        : dc_(dc),
          oldObject_((dc && object) ? SelectObject(dc, object) : nullptr) {
    }

    ~ScopedSelectObject() {
        if (dc_ && oldObject_ && oldObject_ != HGDI_ERROR) {
            SelectObject(dc_, oldObject_);
        }
    }

    ScopedSelectObject(const ScopedSelectObject&) = delete;
    ScopedSelectObject& operator=(const ScopedSelectObject&) = delete;

    bool IsValid() const {
        return oldObject_ != nullptr && oldObject_ != HGDI_ERROR;
    }

private:
    HDC dc_ = nullptr;
    HGDIOBJ oldObject_ = nullptr;
};

} // namespace UiGdi
