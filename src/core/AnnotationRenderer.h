#pragma once

#include "common.h"
#include "core/Annotation.h"

namespace AnnotationRenderer {

inline constexpr int kTextPaddingX = 12;
inline constexpr int kTextPaddingY = 8;

void ConfigureTextFormat(Gdiplus::StringFormat& format);
RECT ComputeTextLayoutRect(const RECT& outerRect);

void DrawArrow(Gdiplus::Graphics& g, const Gdiplus::Pen& pen,
    const Gdiplus::PointF& p1, const Gdiplus::PointF& p2,
    const Gdiplus::Color& color, float strokeWidth = -1.0f);

void DrawShape(Gdiplus::Graphics& g, const AnnotationShape& shape);

} // namespace AnnotationRenderer
