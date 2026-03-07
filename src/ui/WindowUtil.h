#pragma once

#include "common.h"
#include <functional>

/**
 * @file WindowUtil.h
 * @brief 统一的 Windows 窗口管理工具
 * 
 * 提供：
 * - 统一的窗口类注册接口（RegisterWindowClassOnce、RegisterWindowClassExOnce）
 * - 常用的窗口定位标志常量
 * - 通用的窗口过程回调支持
 */

namespace WindowUtil {

// ========== 窗口类注册 ==========

/**
 * 注册 WNDCLASSW 窗口类（仅注册一次）
 * 
 * @param once              std::once_flag 引用（需是 static）
 * @param hInstance         模块实例句柄
 * @param className         窗口类名称
 * @param wndProc           窗口过程回调函数
 * @param cursor            窗口光标（nullptr 则使用默认箭头）
 * @param style             窗口类样式（默认为 0）
 * @param backgroundBrush   背景刷（nullptr 则不绘制背景）
 * @param icon              窗口图标（nullptr 则不显示）
 * 
 * 用法示例：
 *   static std::once_flag once;
 *   WindowUtil::RegisterWindowClassOnce(once, hInstance, L"MyWindowClass", MyWndProc);
 */
void RegisterWindowClassOnce(
    std::once_flag& once,
    HINSTANCE hInstance,
    LPCWSTR className,
    WNDPROC wndProc,
    HCURSOR cursor = nullptr,
    DWORD style = 0,
    HBRUSH backgroundBrush = nullptr,
    HICON icon = nullptr
);

/**
 * 注册 WNDCLASSEXW 窗口类（仅注册一次）
 * 
 * @param once              std::once_flag 引用（需是 static）
 * @param hInstance         模块实例句柄
 * @param className         窗口类名称
 * @param wndProc           窗口过程回调函数
 * @param cursor            窗口光标（nullptr 则使用默认箭头）
 * @param backgroundBrush   背景刷（nullptr 则不绘制背景）
 * @param icon              大图标
 * @param iconSmall         小图标
 * @param style             窗口类样式（默认为 CS_HREDRAW | CS_VREDRAW）
 * 
 * 用法示例（带图标）：
 *   static std::once_flag once;
 *   HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
 *   WindowUtil::RegisterWindowClassExOnce(once, hInstance, L"MyWindowClass", 
 *                                          MyWndProc, nullptr, nullptr, hIcon, hIcon);
 */
void RegisterWindowClassExOnce(
    std::once_flag& once,
    HINSTANCE hInstance,
    LPCWSTR className,
    WNDPROC wndProc,
    HCURSOR cursor = nullptr,
    HBRUSH backgroundBrush = nullptr,
    HICON icon = nullptr,
    HICON iconSmall = nullptr,
    DWORD style = CS_HREDRAW | CS_VREDRAW
);

// ========== 常用的 SetWindowPos 标志组合 ==========

// 仅移动窗口（不改变大小）
constexpr UINT POS_FLAGS_MOVE_ONLY = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE;

// 仅改变大小（不移动）
constexpr UINT POS_FLAGS_SIZE_ONLY = SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE;

// 移动和改变大小
constexpr UINT POS_FLAGS_MOVE_SIZE = SWP_NOZORDER | SWP_NOACTIVATE;

// 显示窗口（默认不激活）
constexpr UINT POS_FLAGS_SHOW = SWP_SHOWWINDOW | SWP_NOACTIVATE;

// 显示并移动到顶部
constexpr UINT POS_FLAGS_SHOW_TOPMOST = SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOZORDER;

// 保持当前位置和大小，仅更新 Z 顺序
constexpr UINT POS_FLAGS_TOPMOST = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER;

// Flyout 动画（仅移动，无激活）
constexpr UINT POS_FLAGS_FLYOUT = SWP_NOSIZE | SWP_NOACTIVATE;

// 隐藏（SWP 标志不包含 HIDEWINDOW，需用 ShowWindow）
// 但如果需要通过 SetWindowPos 保持窗口状态可用此标志集
constexpr UINT POS_FLAGS_HIDE_KEEP = SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE;

} // namespace WindowUtil
