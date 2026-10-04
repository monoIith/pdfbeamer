#include "PdfEditor/Core/TextMarkup.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace pdfeditor::core::markup
{
    namespace
    {
        void AppendUtf8(std::string& output, std::uint32_t codePoint)
        {
            if (codePoint <= 0x7f)
            {
                output.push_back(static_cast<char>(codePoint));
            }
            else if (codePoint <= 0x7ff)
            {
                output.push_back(static_cast<char>(0xc0 | (codePoint >> 6)));
                output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
            }
            else if (codePoint <= 0xffff)
            {
                output.push_back(static_cast<char>(0xe0 | (codePoint >> 12)));
                output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
                output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
            }
            else
            {
                output.push_back(static_cast<char>(0xf0 | (codePoint >> 18)));
                output.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
                output.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
                output.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
            }
        }

        std::string ColorCss(const Color& color)
        {
            std::ostringstream stream;
            stream << '#' << std::hex << std::setfill('0')
                   << std::setw(2) << static_cast<int>(color.red)
                   << std::setw(2) << static_cast<int>(color.green)
                   << std::setw(2) << static_cast<int>(color.blue);
            return stream.str();
        }
    }

    std::string Utf16ToUtf8(std::u16string_view value)
    {
        std::string output;
        output.reserve(value.size());
        for (std::size_t index = 0; index < value.size(); ++index)
        {
            std::uint32_t codePoint = value[index];
            if (codePoint >= 0xd800 && codePoint <= 0xdbff && index + 1 < value.size())
            {
                const std::uint32_t low = value[index + 1];
                if (low >= 0xdc00 && low <= 0xdfff)
                {
                    codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (low - 0xdc00);
                    ++index;
                }
                else
                {
                    codePoint = 0xfffd;
                }
            }
            else if (codePoint >= 0xdc00 && codePoint <= 0xdfff)
            {
                codePoint = 0xfffd;
            }
            AppendUtf8(output, codePoint);
        }
        return output;
    }

    std::string EscapeHtml(std::string_view value)
    {
        std::string output;
        output.reserve(value.size());
        for (const char character : value)
        {
            switch (character)
            {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            case '\'': output += "&#39;"; break;
            default: output.push_back(character); break;
            }
        }
        return output;
    }

    std::string FontCssName(FontFamily family)
    {
        switch (family)
        {
        case FontFamily::notoSerif: return "Noto Serif PDE";
        case FontFamily::notoSansMono: return "Noto Sans Mono PDE";
        default: return "Noto Sans PDE";
        }
    }

    std::string BuildStoryHtml(const TextBoxModel& box)
    {
        return "<div class=\"textbox\">" + EscapeHtml(Utf16ToUtf8(box.text)) + "</div>";
    }

    std::string BuildStoryCss(const TextBoxModel& box)
    {
        const char* alignment = "left";
        if (box.textStyle.alignment == TextAlignment::center) alignment = "center";
        if (box.textStyle.alignment == TextAlignment::right) alignment = "right";

        std::ostringstream css;
        css << "@page{margin:0;}html,body{margin:0;padding:0;}"
            << "body{font-family:'" << FontCssName(box.textStyle.family) << "';"
            << "font-size:" << box.textStyle.sizePoints << "pt;"
            << "font-weight:" << (box.textStyle.bold ? 700 : 400) << ';'
            << "font-style:" << (box.textStyle.italic ? "italic" : "normal") << ';'
            << "color:" << ColorCss(box.textStyle.color) << ';'
            << "line-height:1.2;white-space:pre-wrap;text-align:" << alignment << ";}"
            << ".textbox{margin:0;padding:0;}";
        return css.str();
    }
}

