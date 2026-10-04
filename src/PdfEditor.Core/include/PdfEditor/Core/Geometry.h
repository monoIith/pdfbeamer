#pragma once

#include "Models.h"

namespace pdfeditor::core::geometry
{
    [[nodiscard]] int NormalizeRotation(int rotation) noexcept;
    [[nodiscard]] Size RotatedSize(const PageGeometry& page) noexcept;
    [[nodiscard]] Point ToView(const PageGeometry& page, Point unrotated) noexcept;
    [[nodiscard]] Point FromView(const PageGeometry& page, Point view) noexcept;
    [[nodiscard]] Rect ToView(const PageGeometry& page, const Rect& unrotated) noexcept;
    [[nodiscard]] Rect FromView(const PageGeometry& page, const Rect& view) noexcept;
    [[nodiscard]] Rect ClampToCropBox(const PageGeometry& page, const Rect& rect) noexcept;
    [[nodiscard]] double PixelsPerPoint(double zoomPercent, double dpi) noexcept;
}

