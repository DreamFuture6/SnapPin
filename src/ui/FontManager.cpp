#include "ui/FontManager.h"

bool FontManager::FontDesc::operator==(const FontDesc& other) const {
    return fontName == other.fontName &&
           sizePoints == other.sizePoints &&
           style == other.style;
}

size_t FontManager::FontDescHash::operator()(const FontDesc& desc) const {
    size_t h = std::hash<std::wstring>()(desc.fontName);
    h ^= std::hash<float>()(desc.sizePoints) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<INT>()(desc.style) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

FontManager::FontManager() = default;

FontManager::~FontManager() {
    Clear();
}

FontManager& FontManager::Instance() {
    static FontManager instance;
    return instance;
}

Gdiplus::Font* FontManager::GetFont(LPCWSTR fontName, float sizePoints, INT style) {
    if (!fontName || sizePoints <= 0.0f) {
        return nullptr;
    }

    FontDesc desc{ fontName, sizePoints, style };

    auto it = fonts_.find(desc);
    if (it != fonts_.end()) {
        return it->second.get();
    }

    // 防止缓存无限增长
    if (fonts_.size() >= kMaxFontCount) {
        return nullptr;
    }

    auto pFont = std::make_unique<Gdiplus::Font>(fontName, sizePoints, style, Gdiplus::UnitPixel);
    if (pFont && pFont->GetLastStatus() == Gdiplus::Ok) {
        Gdiplus::Font* pResult = pFont.get();
        fonts_[desc] = std::move(pFont);
        return pResult;
    }

    return nullptr;
}

void FontManager::Clear() {
    fonts_.clear();
}
