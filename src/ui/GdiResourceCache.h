#pragma once

#include "common.h"
#include <unordered_map>

/**
 * @file GdiResourceCache.h
 * @brief 统一的 GDI 资源缓存管理
 * 
 * 目的：
 * - 缓存字体、笔刷等频繁创建的 GDI 对象，避免重复创建销毁
 * - 降低 CPU 占用和编译体积
 * - 线程安全（单线程 UI 线程模型）
 * 
 * 使用示例：
 *   auto& cache = GdiResourceCache::Instance();
 *   HFONT hFont = cache.GetFont(L"Segoe UI", 12, FW_BOLD);
 *   HBRUSH hBrush = cache.GetBrush(RGB(255, 0, 0));
 */

class GdiResourceCache {
public:
    // 禁用拷贝和移动
    GdiResourceCache(const GdiResourceCache&) = delete;
    GdiResourceCache& operator=(const GdiResourceCache&) = delete;
    GdiResourceCache(GdiResourceCache&&) = delete;
    GdiResourceCache& operator=(GdiResourceCache&&) = delete;

    /**
     * 获取单例实例
     */
    static GdiResourceCache& Instance();

    /**
     * 获取或创建字体
     * @param fontName 字体名称（如 "Segoe UI"）
     * @param sizePoints 字体大小（点数，会转换为像素）
     * @param weight 字体粗细（FW_NORMAL, FW_BOLD 等）
     * @param italic 是否斜体
     * @param underline 是否下划线
     * @return HFONT 句柄（调用者不应删除）
     */
    HFONT GetFont(LPCWSTR fontName, int sizePoints, int weight = FW_NORMAL,
                  BOOL italic = FALSE, BOOL underline = FALSE);

    /**
     * 获取或创建纯色笔刷
     * @param color RGB 颜色值
     * @return HBRUSH 句柄（调用者不应删除）
     */
    HBRUSH GetBrush(COLORREF color);

    /**
     * 清空所有缓存
     */
    void Clear();

    /**
     * 获取当前缓存的字体数
     */
    size_t FontCount() const { return fontCache_.size(); }

    /**
     * 获取当前缓存的笔刷数
     */
    size_t BrushCount() const { return brushCache_.size(); }

private:
    struct FontKey {
        std::wstring fontName;
        int sizePoints;
        int weight;
        BOOL italic;
        BOOL underline;

        bool operator==(const FontKey& other) const;
    };

    struct FontKeyHash {
        size_t operator()(const FontKey& key) const;
    };

    GdiResourceCache();
    ~GdiResourceCache();

    std::unordered_map<std::wstring, std::pair<HBRUSH, COLORREF>> brushCache_;
    std::unordered_map<FontKey, HFONT, FontKeyHash> fontCache_;

    static constexpr size_t kMaxFontCache = 256;
    static constexpr size_t kMaxBrushCache = 128;
};
