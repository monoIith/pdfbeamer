#include "PdfEditor/Core/Geometry.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pdfeditor::core::geometry
{
    int NormalizeRotation(int rotation) noexcept
    {
        rotation %= 360;
        if (rotation < 0)
        {
            rotation += 360;
        }
        const int quarterTurns = static_cast<int>(std::lround(rotation / 90.0)) % 4;
        return quarterTurns * 90;
    }

    Size RotatedSize(const PageGeometry& page) noexcept
    {
        const auto size = page.unrotatedSize();
        const auto rotation = NormalizeRotation(page.rotation);
        return rotation == 90 || rotation == 270
            ? Size{ size.height, size.width }
            : size;
    }

    Point ToView(const PageGeometry& page, Point point) noexcept
    {
        const auto size = page.unrotatedSize();
        switch (NormalizeRotation(page.rotation))
        {
        case 90:
            return { size.height - point.y, point.x };
        case 180:
            return { size.width - point.x, size.height - point.y };
        case 270:
            return { point.y, size.width - point.x };
        default:
            return point;
        }
    }

    Point FromView(const PageGeometry& page, Point point) noexcept
    {
        const auto size = page.unrotatedSize();
        switch (NormalizeRotation(page.rotation))
        {
        case 90:
            return { point.y, size.height - point.x };
        case 180:
            return { size.width - point.x, size.height - point.y };
        case 270:
            return { size.width - point.y, point.x };
        default:
            return point;
        }
    }

    namespace
    {
        template <typename Transform>
        Rect TransformRect(const Rect& rect, Transform&& transform) noexcept
        {
            const std::array<Point, 4> points{
                transform(Point{ rect.x, rect.y }),
                transform(Point{ rect.right(), rect.y }),
                transform(Point{ rect.x, rect.bottom() }),
                transform(Point{ rect.right(), rect.bottom() }),
            };

            double minX = points[0].x;
            double minY = points[0].y;
            double maxX = points[0].x;
            double maxY = points[0].y;
            for (const auto& point : points)
            {
                minX = std::min(minX, point.x);
                minY = std::min(minY, point.y);
                maxX = std::max(maxX, point.x);
                maxY = std::max(maxY, point.y);
            }
            return { minX, minY, maxX - minX, maxY - minY };
        }
    }

    Rect ToView(const PageGeometry& page, const Rect& rect) noexcept
    {
        return TransformRect(rect, [&page](Point point) { return ToView(page, point); });
    }

    Rect FromView(const PageGeometry& page, const Rect& rect) noexcept
    {
        return TransformRect(rect, [&page](Point point) { return FromView(page, point); });
    }

    Rect ClampToCropBox(const PageGeometry& page, const Rect& rect) noexcept
    {
        const auto size = page.unrotatedSize();
        Rect result = rect;
        result.width = std::clamp(result.width, 0.0, size.width);
        result.height = std::clamp(result.height, 0.0, size.height);
        result.x = std::clamp(result.x, 0.0, std::max(0.0, size.width - result.width));
        result.y = std::clamp(result.y, 0.0, std::max(0.0, size.height - result.height));
        return result;
    }

    double PixelsPerPoint(double zoomPercent, double dpi) noexcept
    {
        if (zoomPercent <= 0.0 || dpi <= 0.0)
        {
            return 0.0;
        }
        return (zoomPercent / 100.0) * (dpi / 72.0);
    }
}

