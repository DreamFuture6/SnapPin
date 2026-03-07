#pragma once

#include "common.h"

/**
 * @file ThemeColors.h
 * @brief 应用全局主题颜色定义
 * 
 * 目的：
 * - 统一管理应用的所有颜色，减少编译后体积
 * - 便于主题切换和颜色管理
 * - 提高代码可读性和可维护性
 * 
 * 使用示例：
 *   using namespace ThemeColors;
 *   COLORREF bg = Basic::Background;
 */

namespace ThemeColors {

// ============== 基础色系 ==============
namespace Basic {
    constexpr COLORREF Background     = RGB(14, 16, 20);       // 深色背景
    constexpr COLORREF Surface        = RGB(39, 43, 50);       // 卡片/面板背景
    constexpr COLORREF Border         = RGB(58, 62, 72);       // 边框色
    constexpr COLORREF Text           = RGB(234, 238, 244);    // 主文本
    constexpr COLORREF TextMuted      = RGB(163, 170, 181);    // 次要文本
}

// ============== UI 组件特定色 ==============
namespace Component {
    // PinWindow 相关色
    namespace PinWindow {
        constexpr COLORREF BorderColor = RGB(255, 255, 255);   // 窗口边框（alpha 单独处理）
        constexpr COLORREF HintBgColor = RGB(16, 18, 22);      // 缩放提示背景
        constexpr COLORREF HintTextColor = RGB(234, 238, 244); // 提示文字
    }

    // OcrResultPopupWindow 相关色
    namespace OcrPopup {
        constexpr COLORREF BackgroundColor = RGB(20, 22, 26);
        constexpr COLORREF PanelColor      = RGB(44, 48, 56);
        constexpr COLORREF BorderColor     = RGB(70, 76, 88);
        constexpr COLORREF TitleColor      = RGB(235, 239, 244);
        constexpr COLORREF BodyColor       = RGB(200, 205, 212);
    }

    // ToolbarWindow 相关色
    namespace Toolbar {
        constexpr COLORREF ComboBoxBgColor = RGB(49, 54, 62);
        constexpr COLORREF ComboBoxTextColor = RGB(234, 238, 244);
        constexpr COLORREF ButtonBg        = RGB(39, 43, 50);
        constexpr COLORREF ButtonFg        = RGB(234, 238, 244);
        constexpr COLORREF Separator       = RGB(58, 62, 72);
    }
}

// ============== 交互状态色 ==============
namespace State {
    constexpr COLORREF EditBorderDefault = RGB(130, 138, 152);  // 默认状态
    constexpr COLORREF EditBorderHover   = RGB(156, 164, 178);  // 悬停状态
    constexpr COLORREF EditBorderActive  = RGB(95, 165, 255);   // 激活/焦点状态

    constexpr COLORREF AccentDefault = RGB(95, 165, 255);
    constexpr COLORREF AccentHover   = RGB(130, 170, 255);
    constexpr COLORREF AccentActive  = RGB(60, 136, 246);
}

// ============== 便利函数 ==============
inline COLORREF UnifiedBorderColor(bool active, bool hovered,
                                   COLORREF borderDefault, COLORREF borderHover, COLORREF borderActive)
{
    return active ? borderActive : (hovered ? borderHover : borderDefault);
}

} // namespace ThemeColors
