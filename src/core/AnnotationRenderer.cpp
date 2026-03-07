#include "core/AnnotationRenderer.h"

namespace AnnotationRenderer {

void ConfigureTextFormat(Gdiplus::StringFormat& format) {
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
    format.SetTrimming(Gdiplus::StringTrimmingNone);
}

RECT ComputeTextLayoutRect(const RECT& outerRect) {
    RECT layout = NormalizeRect(outerRect);
    const int width = RectWidth(layout);
    const int height = RectHeight(layout);
    if (width <= 0 || height <= 0) {
        return layout;
    }

    layout.left += std::min(kTextPaddingX, std::max(0, width / 2));
    layout.top += std::min(kTextPaddingY, std::max(0, height / 2));
    layout.right -= std::min(kTextPaddingX, std::max(0, width / 2));
    layout.bottom -= std::min(kTextPaddingY, std::max(0, height / 2));
    if (layout.right < layout.left) {
        layout.right = layout.left;
    }
    if (layout.bottom < layout.top) {
        layout.bottom = layout.top;
    }
    return layout;
}

void DrawArrow(Gdiplus::Graphics& g, const Gdiplus::Pen& pen,
    const Gdiplus::PointF& p1, const Gdiplus::PointF& p2,
    const Gdiplus::Color& color, float strokeWidth) {
    const float dx = p2.X - p1.X;
    const float dy = p2.Y - p1.Y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.5f) {
        return;
    }

    const float effectiveStroke = strokeWidth > 0.0f ? strokeWidth : pen.GetWidth();
    const float ux = dx / dist;
    const float uy = dy / dist;
    const float headLen = std::max(10.0f, effectiveStroke * 4.2f);
    const float baseX = p2.X - ux * headLen;
    const float baseY = p2.Y - uy * headLen;

    g.DrawLine(&pen, p1, Gdiplus::PointF(baseX, baseY));

    const float halfW = std::max(4.0f, effectiveStroke * 1.7f);
    const float nx = -uy;
    const float ny = ux;
    Gdiplus::PointF left(baseX + nx * halfW, baseY + ny * halfW);
    Gdiplus::PointF right(baseX - nx * halfW, baseY - ny * halfW);
    Gdiplus::PointF pts[3] = { p2, left, right };
    Gdiplus::SolidBrush brush(color);
    g.FillPolygon(&brush, pts, 3);
}

void DrawShape(Gdiplus::Graphics& g, const AnnotationShape& shape) {
    Gdiplus::Color color(255, GetRValue(shape.color), GetGValue(shape.color), GetBValue(shape.color));
    Gdiplus::Pen pen(color, shape.stroke);
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    const RECT rc = NormalizeRect(shape.rect);
    const auto x = static_cast<Gdiplus::REAL>(rc.left);
    const auto y = static_cast<Gdiplus::REAL>(rc.top);
    const auto w = static_cast<Gdiplus::REAL>(RectWidth(rc));
    const auto h = static_cast<Gdiplus::REAL>(RectHeight(rc));

    switch (shape.type) {
    case ToolType::Rect:
        if (shape.fillEnabled) {
            Gdiplus::SolidBrush fill(Gdiplus::Color(255, GetRValue(shape.fillColor), GetGValue(shape.fillColor), GetBValue(shape.fillColor)));
            g.FillRectangle(&fill, x, y, w, h);
        }
        g.DrawRectangle(&pen, x, y, w, h);
        break;
    case ToolType::Ellipse:
        if (shape.fillEnabled) {
            Gdiplus::SolidBrush fill(Gdiplus::Color(255, GetRValue(shape.fillColor), GetGValue(shape.fillColor), GetBValue(shape.fillColor)));
            g.FillEllipse(&fill, x, y, w, h);
        }
        g.DrawEllipse(&pen, x, y, w, h);
        break;
    case ToolType::Line:
        if (shape.points.size() >= 2) {
            g.DrawLine(&pen,
                static_cast<Gdiplus::REAL>(shape.points[0].x), static_cast<Gdiplus::REAL>(shape.points[0].y),
                static_cast<Gdiplus::REAL>(shape.points[1].x), static_cast<Gdiplus::REAL>(shape.points[1].y));
        }
        break;
    case ToolType::Arrow:
        if (shape.points.size() >= 2) {
            DrawArrow(g, pen,
                Gdiplus::PointF(static_cast<Gdiplus::REAL>(shape.points[0].x), static_cast<Gdiplus::REAL>(shape.points[0].y)),
                Gdiplus::PointF(static_cast<Gdiplus::REAL>(shape.points[1].x), static_cast<Gdiplus::REAL>(shape.points[1].y)),
                color,
                shape.stroke);
        }
        break;
    case ToolType::Pen:
        if (shape.points.size() >= 2) {
            std::vector<Gdiplus::Point> pts;
            pts.reserve(shape.points.size());
            for (const auto& point : shape.points) {
                pts.emplace_back(point.x, point.y);
            }
            if (pts.size() >= 3) {
                g.DrawCurve(&pen, pts.data(), static_cast<INT>(pts.size()), 0.5f);
            } else {
                g.DrawLines(&pen, pts.data(), static_cast<INT>(pts.size()));
            }
        }
        break;
    case ToolType::Text: {
        const auto oldTextHint = g.GetTextRenderingHint();
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, std::max(8.0f, shape.textSize), shape.textStyle, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush brush(color);
        const RECT textLayoutRect = ComputeTextLayoutRect(shape.rect);
        Gdiplus::RectF layout(
            static_cast<Gdiplus::REAL>(textLayoutRect.left),
            static_cast<Gdiplus::REAL>(textLayoutRect.top),
            std::max(1.0f, static_cast<Gdiplus::REAL>(RectWidth(textLayoutRect))),
            std::max(1.0f, static_cast<Gdiplus::REAL>(RectHeight(textLayoutRect))));
        Gdiplus::StringFormat textFormat(0, LANG_NEUTRAL);
        ConfigureTextFormat(textFormat);
        g.DrawString(shape.text.c_str(), -1, &font, layout, &textFormat, &brush);
        g.SetTextRenderingHint(oldTextHint);
        break;
    }
    case ToolType::Number: {
        const float radius = std::max(12.0f, shape.stroke * 5.0f);
        Gdiplus::Pen border(color, std::max(1.5f, shape.stroke));
        border.SetLineJoin(Gdiplus::LineJoinRound);
        Gdiplus::SolidBrush textBrush(color);
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, radius, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        const float cx = static_cast<float>(shape.rect.left);
        const float cy = static_cast<float>(shape.rect.top);
        g.DrawEllipse(&border, cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
        std::wstring text = std::to_wstring(shape.number);
        Gdiplus::RectF textRect(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
        Gdiplus::StringFormat format(0, LANG_NEUTRAL);
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(text.c_str(), -1, &font, textRect, &format, &textBrush);
        break;
    }
    default:
        break;
    }
}

} // namespace AnnotationRenderer
