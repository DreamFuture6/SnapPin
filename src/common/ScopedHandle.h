#pragma once

/**
 * @file ScopedHandle.h
 * @brief 通用 RAII 句柄包装器模板
 * 
 * 用于包装 Windows API 句柄，自动调用对应的清理函数。
 * 支持移动语义，禁用复制以防止双重释放。
 * 
 * 用法示例：
 *   // 使用 ScopedHandle 包装 HGLOBAL，自动调用 GlobalFree
 *   using ScopedGlobalMemory = ScopedHandle<HGLOBAL, GlobalFree>;
 *   
 *   ScopedGlobalMemory hMem(GlobalAlloc(GMEM_FIXED, 1024));
 *   if (hMem) {
 *       void* ptr = hMem.Get();
 *       // ... 使用内存
 *   }  // 自动调用 GlobalFree
 * 
 * 支持的 Deleter：
 *   - GlobalFree（HGLOBAL）
 *   - DeleteObject（HGDIOBJ）
 *   - ReleaseDC（HDC）- 需配合 GetDC 返回值使用
 *   - WinHttpCloseHandle（HINTERNET）
 *   - CloseHandle（HANDLE）
 */

template <typename HandleT, auto Deleter>
class ScopedHandle {
public:
    // 默认构造（空句柄）
    ScopedHandle() = default;
    
    // 显式构造
    explicit ScopedHandle(HandleT handle) : handle_(handle) {}
    
    // 析构（自动调用 Deleter）
    ~ScopedHandle() {
        reset();
    }
    
    // 禁用复制语义（防止双重释放）
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    
    // 支持移动语义
    ScopedHandle(ScopedHandle&& other) noexcept 
        : handle_(other.Release()) {}
    
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            reset(other.Release());
        }
        return *this;
    }
    
    // 获取句柄值（仅读）
    HandleT Get() const { 
        return handle_; 
    }
    
    // bool 转换：检查句柄是否有效
    // 支持 if (scoped_handle) 的写法
    explicit operator bool() const { 
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; 
    }
    
    // 释放所有权（调用者负责清理）
    HandleT Release() {
        HandleT released = handle_;
        handle_ = nullptr;
        return released;
    }
    
    // 重置句柄（释放旧句柄，设置新句柄）
    void reset(HandleT newHandle = nullptr) {
        if (handle_ != nullptr) {
            Deleter(handle_);
        }
        handle_ = newHandle;
    }

private:
    HandleT handle_ = nullptr;
};

/**
 * 特化：支持 INVALID_HANDLE_VALUE (-1) 的 HANDLE
 * 
 * 用法：
 *   ScopedHandleEx<HANDLE, CloseHandle> hFile(CreateFileW(...));
 *   if (hFile) {  // 检查 != INVALID_HANDLE_VALUE
 *       // ... 使用文件句柄
 *   }
 */
template <typename HandleT, auto Deleter>
class ScopedHandleEx : public ScopedHandle<HandleT, Deleter> {
public:
    using ScopedHandle<HandleT, Deleter>::ScopedHandle;
    
    // 覆盖 operator bool，处理 INVALID_HANDLE_VALUE
    explicit operator bool() const {
        return this->Get() != nullptr && this->Get() != INVALID_HANDLE_VALUE;
    }
};

// ========== 常用的 Deleter 适配器 ==========

// 对于没有直接函数指针的情况，使用 lambda 包装

namespace HandleDeleters {

// HDC 需要 ReleaseDC(nullptr, ...) - 用于 GetDC(nullptr) 获取的屏幕 DC
inline void ReleaseDcNull(HDC dc) {
    if (dc) {
        ReleaseDC(nullptr, dc);
    }
}

// 用于 WinHttp 句柄
inline void CloseWinHttpHandle(HINTERNET handle) {
    if (handle) {
        WinHttpCloseHandle(handle);
    }
}

// 通用的 HANDLE 关闭
inline void CloseHandleWrap(HANDLE handle) {
    if (handle && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
    }
}

} // namespace HandleDeleters
