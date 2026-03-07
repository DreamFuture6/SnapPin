#include "ui/GdiResourceCache.h"

// FontKey 哈希和比较实现
bool GdiResourceCache::FontKey::operator==(const FontKey& other) const {
    return fontName == other.fontName &&
           sizePoints == other.sizePoints &&
           weight == other.weight &&
           italic == other.italic &&
           underline == other.underline;
}

size_t GdiResourceCache::FontKeyHash::operator()(const FontKey& key) const {
    size_t h = std::hash<std::wstring>()(key.fontName);
    h ^= std::hash<int>()(key.sizePoints) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>()(key.weight) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>()(key.italic) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>()(key.underline) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

GdiResourceCache::GdiResourceCache() = default;

GdiResourceCache::~GdiResourceCache() {
    Clear();
}

GdiResourceCache& GdiResourceCache::Instance() {
    static GdiResourceCache instance;
    return instance;
}

HFONT GdiResourceCache::GetFont(LPCWSTR fontName, int sizePoints, int weight,
                                 BOOL italic, BOOL underline) {
    if (!fontName || sizePoints <= 0) {
        return nullptr;
    }

    FontKey key{ fontName, sizePoints, weight, italic, underline };
    
    auto it = fontCache_.find(key);
    if (it != fontCache_.end()) {
        return it->second;
    }

    // 避免缓存过度增长
    if (fontCache_.size() >= kMaxFontCache) {
        return nullptr; // 返回 nullptr，调用者应有备用方案
    }

    // 转换点数为像素（标准 96 DPI）
    const int heightPixels = -MulDiv(sizePoints, 96, 72);
    HFONT hFont = CreateFontW(
        heightPixels,           // 高度
        0,                      // 宽度（自动计算）
        0,                      // 方向
        0,                      // 倾斜
        weight,                 // 粗细
        italic,                 // 斜体
        underline,              // 下划线
        FALSE,                  // 删除线
        DEFAULT_CHARSET,        // 字符集
        OUT_DEFAULT_PRECIS,     // 输出精度
        CLIP_DEFAULT_PRECIS,    // 剪裁精度
        CLEARTYPE_QUALITY,      // 质量
        DEFAULT_PITCH | FF_DONTCARE, // 间距和家族
        fontName
    );

    if (hFont) {
        fontCache_[key] = hFont;
    }

    return hFont;
}

HBRUSH GdiResourceCache::GetBrush(COLORREF color) {
    // 使用16进制颜色值作为 key
    wchar_t colorKey[16]{};
    swprintf_s(colorKey, L"%06X", color & 0xFFFFFF);
    std::wstring key(colorKey);

    auto it = brushCache_.find(key);
    if (it != brushCache_.end()) {
        // 验证颜色值是否一致（防止哈希冲突）
        if (it->second.second == color) {
            return it->second.first;
        }
    }

    // 避免缓存过度增长
    if (brushCache_.size() >= kMaxBrushCache) {
        return nullptr; // 返回 nullptr，调用者应自己创建
    }

    HBRUSH hBrush = CreateSolidBrush(color);
    if (hBrush) {
        brushCache_[key] = { hBrush, color };
    }

    return hBrush;
}

void GdiResourceCache::Clear() {
    for (auto& p : fontCache_) {
        if (p.second) {
            DeleteObject(p.second);
        }
    }
    fontCache_.clear();

    for (auto& p : brushCache_) {
        // p 是 pair<const std::wstring, std::pair<HBRUSH, COLORREF>>，
        // 应当访问 p.second.first 来获取 HBRUSH
        if (p.second.first) {
            DeleteObject(p.second.first);
        }
    }
    brushCache_.clear();
}
