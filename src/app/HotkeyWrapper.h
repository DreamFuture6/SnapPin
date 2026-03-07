#pragma once

#include "common.h"
#include "core/Logger.h"
#include <functional>

/**
 * @file HotkeyWrapper.h
 * @brief 快捷键处理的统一包装
 * 
 * 提供：
 * - 快捷键防抖（debounce）处理
 * - 统一的异常处理与日志记录
 * - 活动状态追踪（可选）
 * 
 * 背景：
 * 多个快捷键处理函数都存在类似的模式：
 * 1. 检查防抖时间（避免快速连续触发）
 * 2. 更新最后触发时间
 * 3. 用 try-catch 包装业务逻辑
 * 4. 统一记录异常到日志
 * 
 * 本文件将这些重复的逻辑提取为通用函数。
 */

namespace HotkeyHandler {

/**
 * 执行快捷键处理函数（带防抖和异常处理）
 * 
 * @tparam FuncT                    可调用对象类型
 * @param lastTickRef               上次执行时间戳的引用（输入/输出）
 * @param debounceMs                防抖延迟（毫秒）
 * @param hotkeyName                快捷键名称（用于日志）
 * @param func                      要执行的函数
 * @return true                     如果函数执行成功；false 表示防抖中或异常
 * 
 * 用法示例：
 *   // 原始代码：
 *   case HOTKEY_ID_PIN_LAST: {
 *       const DWORD now = GetTickCount();
 *       if (now - lastPinHotkeyTick_ < 120) {
 *           break;
 *       }
 *       lastPinHotkeyTick_ = now;
 *       try {
 *           PinLastCapture();
 *       } catch (const std::exception& ex) {
 *           Logger::Instance().Error(L"F3 hotkey failed: " + Utf8ToWide(ex.what()));
 *       } catch (...) {
 *           Logger::Instance().Error(L"F3 hotkey failed: unknown exception.");
 *       }
 *       break;
 *   }
 * 
 *   // 优化后：
 *   case HOTKEY_ID_PIN_LAST: {
 *       HotkeyHandler::ExecuteWithDebounce(
 *           lastPinHotkeyTick_, 120, L"F3 hotkey",
 *           [this]() { PinLastCapture(); }
 *       );
 *       break;
 *   }
 */
template <typename FuncT>
inline bool ExecuteWithDebounce(
    DWORD& lastTickRef,
    DWORD debounceMs,
    const std::wstring& hotkeyName,
    FuncT&& func)
{
    const DWORD now = GetTickCount();
    
    // 防抖检查
    if (now - lastTickRef < debounceMs) {
        return false;  // 触发太频繁，忽略
    }
    
    // 更新时间戳
    lastTickRef = now;
    
    // 执行函数并捕获异常
    try {
        func();
        return true;
    } catch (const std::exception& ex) {
        Logger::Instance().Error(hotkeyName + L" failed: " + Utf8ToWide(ex.what()));
        return false;
    } catch (...) {
        Logger::Instance().Error(hotkeyName + L" failed: unknown exception.");
        return false;
    }
}

/**
 * 执行快捷键处理函数，支持额外的成功/失败回调
 * 
 * @tparam FuncT                    可调用对象类型（返回 bool，true=成功）
 * @param lastTickRef               上次执行时间戳的引用
 * @param debounceMs                防抖延迟（毫秒）
 * @param hotkeyName                快捷键名称
 * @param func                      业务函数（返回 bool 表示成功）
 * @param onSuccess                 成功回调（可选）
 * @param onFailure                 失败回调（可选）
 * @return true                     如果函数执行成功
 * 
 * 用法示例：
 *   HotkeyHandler::ExecuteWithDebounceEx(
 *       lastTickRef, 120, L"F3 hotkey",
 *       [this]() { return PinLastCapture(); },
 *       [this]() { /* 成功时的额外处理 */ },
 *       [this](bool debounced) { 
 *           if (!debounced) {
 *               // 处理"没有历史截图"的情况
 *               tray_.ShowNotification(L"SnapPin", L"没有可贴出的历史截图。");
 *           }
 *       }
 *   );
 */
template <typename FuncT, typename OnSuccessT = std::nullptr_t, typename OnFailureT = std::nullptr_t>
inline bool ExecuteWithDebounceEx(
    DWORD& lastTickRef,
    DWORD debounceMs,
    const std::wstring& hotkeyName,
    FuncT&& func,
    OnSuccessT&& onSuccess = nullptr,
    OnFailureT&& onFailure = nullptr)
{
    const DWORD now = GetTickCount();
    
    // 防抖检查
    if (now - lastTickRef < debounceMs) {
        return false;
    }
    
    lastTickRef = now;
    
    try {
        bool success = func();
        
        if (success && onSuccess != nullptr) {
            onSuccess();
        }
        
        if (!success && onFailure != nullptr) {
            onFailure(false);  // false = 不是因为防抖失败
        }
        
        return success;
    } catch (const std::exception& ex) {
        Logger::Instance().Error(hotkeyName + L" failed: " + Utf8ToWide(ex.what()));
        if (onFailure != nullptr) {
            onFailure(false);
        }
        return false;
    } catch (...) {
        Logger::Instance().Error(hotkeyName + L" failed: unknown exception.");
        if (onFailure != nullptr) {
            onFailure(false);
        }
        return false;
    }
}

/**
 * 检查快捷键是否处于防抖状态
 * 
 * @param lastTickRef               上次执行时间戳
 * @param debounceMs                防抖延迟（毫秒）
 * @return true                     仍在防抖期间
 * 
 * 用法：
 *   if (HotkeyHandler::IsDebouncing(lastPinHotkeyTick_, 120)) {
 *       return;  // 快捷键触发太频繁，忽略
 *   }
 */
inline bool IsDebouncing(DWORD lastTick, DWORD debounceMs) {
    return (GetTickCount() - lastTick) < debounceMs;
}

} // namespace HotkeyHandler
