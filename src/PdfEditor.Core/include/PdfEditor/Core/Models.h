#pragma once

#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pdfeditor::core
{
    struct Point
    {
        double x{};
        double y{};
        auto operator<=>(const Point&) const = default;
    };

    struct Size
    {
        double width{};
        double height{};
        auto operator<=>(const Size&) const = default;
    };

    struct Rect
    {
        double x{};
        double y{};
        double width{};
        double height{};

        [[nodiscard]] double right() const noexcept { return x + width; }
        [[nodiscard]] double bottom() const noexcept { return y + height; }
        [[nodiscard]] bool empty() const noexcept { return width <= 0.0 || height <= 0.0; }
        auto operator<=>(const Rect&) const = default;
    };

    struct Color
    {
        std::uint8_t red{};
        std::uint8_t green{};
        std::uint8_t blue{};
        auto operator<=>(const Color&) const = default;
    };

    enum class FontFamily
    {
        notoSans,
        notoSerif,
        notoSansMono,
    };

    enum class TextAlignment
    {
        left,
        center,
        right,
    };

    struct TextStyle
    {
        FontFamily family{ FontFamily::notoSans };
        double sizePoints{ 12.0 };
        Color color{ 0, 0, 0 };
        bool bold{};
        bool italic{};
        TextAlignment alignment{ TextAlignment::left };
        auto operator<=>(const TextStyle&) const = default;
    };

    struct BoxStyle
    {
        Color borderColor{ 0, 0, 0 };
        double borderWidthPoints{ 1.0 };
        Color fillColor{ 255, 255, 255 };
        double fillOpacity{};
        double paddingPoints{ 4.0 };
        auto operator<=>(const BoxStyle&) const = default;
    };

    struct TextBoxModel
    {
        std::string id;
        std::size_t pageIndex{};
        // Crop-box-local, unrotated coordinates in PDF points. Origin is top-left.
        Rect bounds;
        std::u16string text;
        TextStyle textStyle;
        BoxStyle boxStyle;
        bool overflow{};

        [[nodiscard]] bool empty() const noexcept { return text.empty(); }
        auto operator<=>(const TextBoxModel&) const = default;
    };

    struct PageGeometry
    {
        Rect mediaBox;
        Rect cropBox;
        int rotation{};

        [[nodiscard]] Size unrotatedSize() const noexcept
        {
            return { cropBox.width, cropBox.height };
        }

        auto operator<=>(const PageGeometry&) const = default;
    };

    struct OpenDocumentResult
    {
        std::vector<PageGeometry> pages;
        bool hasDigitalSignatures{};
        bool wasRepaired{};
    };

    struct PixelBuffer
    {
        int width{};
        int height{};
        int stride{};
        // Premultiplied BGRA8, top-to-bottom rows.
        std::vector<std::uint8_t> pixels;
    };

    struct LayoutResult
    {
        bool fits{};
        Rect filledBounds;
        std::string message;
    };

    struct SaveResult
    {
        std::filesystem::path path;
        std::uintmax_t bytesWritten{};
    };
}
