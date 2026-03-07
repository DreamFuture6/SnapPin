#pragma once

#include "common.h"

/**
 * @file WindowProc.h
 * @brief 窗口过程回调的通用基础设施
 * 
 * 提供：
 * - WM_NCCREATE 处理的统一模板
 * - 便利宏减少 reinterpret_cast 冗余
 * 
 * 背景：
 * 在 Win32 中，窗口过程（WndProc）是静态函数，无法直接访问成员变量。
 * 标准做法是：
 * 1. 在 WM_NCCREATE 消息中，从 CREATESTRUCT 提取 this 指针
 * 2. 将 this 指针存储在 GWLP_USERDATA 中
 * 3. 后续消息中从 GWLP_USERDATA 恢复 this 指针，调用成员方法
 * 
 * 本文件提供了这个模式的统一实现。
 */

namespace WindowProc {

/**
 * 处理 WM_NCCREATE 消息，提取 this 指针
 * 
 * @tparam WindowClassT 窗口类的类型
 * @param hwnd          窗口句柄
 * @param msg           消息号
 * @param lParam        消息参数（包含 CREATESTRUCT 指针）
 * @param outSelf       输出参数，接收 this 指针
 * @return FALSE        继续正常处理 WM_NCCREATE
 * 
 * 用法：
 *   LRESULT CALLBACK MyWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
 *       MyWindow* self = nullptr;
 *       WindowProc::HandleWindowCreate(hwnd, msg, lParam, self);
 *       
 *       if (self) {
 *           return self->HandleMessage(msg, wParam, lParam);
 *       }
 *       return DefWindowProcW(hwnd, msg, wParam, lParam);
 *   }
 */
template <typename WindowClassT>
inline void HandleWindowCreate(HWND hwnd, UINT msg, LPARAM lParam, WindowClassT*& outSelf)
{
    // 首先尝试从 GWLP_USERDATA 恢复 this 指针
    // 这对于重入调用或多次 WM_NCCREATE 很重要
    outSelf = reinterpret_cast<WindowClassT*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    
    // 处理初始化消息
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        outSelf = reinterpret_cast<WindowClassT*>(cs->lpCreateParams);
        if (outSelf) {
            // 将 this 指针存储在窗口的用户数据中
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(outSelf));
        }
    }
}

} // namespace WindowProc

// ========== 便利宏 ==========

/**
 * 在窗口过程中简化 this 指针的提取
 * 
 * 用法：
 *   LRESULT CALLBACK MyWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
 *       auto* self = WINDOW_PROC_GET_THIS(MyWindow);
 *       WindowProc::HandleWindowCreate(hwnd, msg, lParam, self);
 *       // ... 后续代码
 *   }
 * 
 * 在 HandleWindowCreate 调用后：
 *   if (self) {
 *       return self->HandleMessage(msg, wParam, lParam);
 *   }
 */
#define WINDOW_PROC_GET_THIS(WindowClassT) \
    reinterpret_cast<WindowClassT*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))

/**
 * 简化 CREATESTRUCT 的提取
 */
#define WINDOW_CREATE_STRUCT(lParam) \
    reinterpret_cast<CREATESTRUCTW*>(lParam)

/**
 * 完整的窗口过程框架宏
 * 
 * 用法：
 *   LRESULT CALLBACK MyWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
 *       WINDOW_PROC_DISPATCH(MyWindow, self) {
 *           return self->HandleMessage(msg, wParam, lParam);
 *       }
 *       return DefWindowProcW(hwnd, msg, wParam, lParam);
 *   }
 */
#define WINDOW_PROC_DISPATCH(WindowClassT, selfVar) \
    auto* selfVar = WINDOW_PROC_GET_THIS(WindowClassT); \
    if (!selfVar && WINDOW_CREATE_STRUCT(lParam)) { \
        selfVar = reinterpret_cast<WindowClassT*>(WINDOW_CREATE_STRUCT(lParam)->lpCreateParams); \
        if (selfVar) { \
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(selfVar)); \
        } \
    } \
    if (selfVar)
