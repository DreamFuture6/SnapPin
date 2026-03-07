#pragma once

#include "common.h"
#include <memory>

/**
 * @file FontManager.h
 * @brief Gdiplus 字体的缓存和复用管理
 * 
 * 目的：缓存 Gdiplus::Font 对象以降低 PinWindow 等高频绘制场景的 CPU 占用
 * 
 * 使用示例：
 *   auto& mgr = FontManager::Instance();
 *   Gdiplus::Font* pFont = mgr.GetFont(L"Segoe UI", 12.0f, Gdiplus::FontStyleBold);
 *   if (pFont) {
 *       g.DrawString(text, -1, pFont, rect, nullptr, &brush);
 *   }
 */

class FontManager {
public:
    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;
    FontManager(FontManager&&) = delete;
    FontManager& operator=(FontManager&&) = delete;

    /**
     * 获取单例实例
     */
    static FontManager& Instance();

    /**
     * 获取或创建 Gdiplus 字体
     * @param fontName 字体名称（如 "Segoe UI"）
     * @param sizePoints 字体大小（点数）
     * @param style 字体风格（Gdiplus::FontStyleRegular 等）
     * @return Gdiplus::Font* 指针（调用者不应删除，由 FontManager 管理）
     */
    Gdiplus::Font* GetFont(LPCWSTR fontName, float sizePoints, INT style = Gdiplus::FontStyleRegular);

    /**
     * 清空所有缓存
     */
    void Clear();

    /**
     * 获取当前缓存的字体数
     */
    size_t Count() const { return fonts_.size(); }

private:
    struct FontDesc {
        std::wstring fontName;
        float sizePoints;
        INT style;

        bool operator==(const FontDesc& other) const;
    };

    struct FontDescHash {
        size_t operator()(const FontDesc& desc) const;
    };

    FontManager();
    ~FontManager();

    std::unordered_map<FontDesc, std::unique_ptr<Gdiplus::Font>, FontDescHash> fonts_;
    static constexpr size_t kMaxFontCount = 256;
};
