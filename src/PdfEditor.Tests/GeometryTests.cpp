#include "TestHarness.h"

#include "PdfEditor/Core/Geometry.h"

using namespace pdfeditor::core;

namespace
{
    PageGeometry Page(int rotation)
    {
        return { { 0, 0, 600, 800 }, { 0, 0, 600, 800 }, rotation };
    }
}

TEST(Geometry, NormalizesRotation)
{
    EXPECT_EQ(geometry::NormalizeRotation(-90), 270);
    EXPECT_EQ(geometry::NormalizeRotation(450), 90);
    EXPECT_EQ(geometry::NormalizeRotation(181), 180);
}

TEST(Geometry, RoundTripsPointsAtEveryRotation)
{
    for (const int rotation : { 0, 90, 180, 270 })
    {
        const auto page = Page(rotation);
        const Point original{ 123.5, 456.25 };
        const auto roundTrip = geometry::FromView(page, geometry::ToView(page, original));
        EXPECT_NEAR(roundTrip.x, original.x, 0.0001);
        EXPECT_NEAR(roundTrip.y, original.y, 0.0001);
    }
}

TEST(Geometry, RotatesRectangleNinetyDegrees)
{
    const auto result = geometry::ToView(Page(90), Rect{ 10, 20, 100, 50 });
    EXPECT_NEAR(result.x, 730.0, 0.0001);
    EXPECT_NEAR(result.y, 10.0, 0.0001);
    EXPECT_NEAR(result.width, 50.0, 0.0001);
    EXPECT_NEAR(result.height, 100.0, 0.0001);
}

TEST(Geometry, ClampsBoxesInsideCropArea)
{
    const auto result = geometry::ClampToCropBox(Page(0), Rect{ 590, -20, 100, 900 });
    EXPECT_EQ(result, (Rect{ 500, 0, 100, 800 }));
}

TEST(Geometry, ConvertsZoomAndDpiToPixels)
{
    EXPECT_NEAR(geometry::PixelsPerPoint(100.0, 96.0), 96.0 / 72.0, 0.0001);
    EXPECT_NEAR(geometry::PixelsPerPoint(150.0, 144.0), 3.0, 0.0001);
}

