#pragma once
#include "common.h"

enum class ToolType {
    None,
    Rect,
    Ellipse,
    Line,
    Arrow,
    Pen,
    Mosaic,
    Text,
    Number,
    Eraser,
};

struct AnnotationShape {
    ToolType type = ToolType::None;
    COLORREF color = RGB(255, 0, 0);
    float stroke = 2.0f;
    bool fillEnabled = false;
    COLORREF fillColor = RGB(255, 255, 0);
    float textSize = 18.0f;
    INT textStyle = Gdiplus::FontStyleRegular;
    RECT rect{};
    std::vector<POINT> points;
    std::wstring text;
    int number = 0;
};

inline RECT ShapeBounds(const AnnotationShape& s) {
    if ((s.type == ToolType::Pen || s.type == ToolType::Arrow || s.type == ToolType::Line) && !s.points.empty()) {
        LONG minX = s.points[0].x;
        LONG minY = s.points[0].y;
        LONG maxX = s.points[0].x;
        LONG maxY = s.points[0].y;
        for (const auto& p : s.points) {
            minX = std::min(minX, p.x);
            minY = std::min(minY, p.y);
            maxX = std::max(maxX, p.x);
            maxY = std::max(maxY, p.y);
        }
        return RECT{minX, minY, maxX, maxY};
    }
    if (s.type == ToolType::Number) {
        // For Number type, create bounds around the center point based on radius
        const float r = std::max(12.0f, s.stroke * 5.0f);
        LONG cx = s.rect.left;
        LONG cy = s.rect.top;
        return RECT{
            static_cast<LONG>(cx - r),
            static_cast<LONG>(cy - r),
            static_cast<LONG>(cx + r),
            static_cast<LONG>(cy + r)
        };
    }
    return NormalizeRect(s.rect);
}
