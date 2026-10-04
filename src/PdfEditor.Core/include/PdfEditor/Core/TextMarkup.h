#pragma once

#include "Models.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace pdfeditor::core::markup
{
    [[nodiscard]] std::string Utf16ToUtf8(std::u16string_view value);
    [[nodiscard]] std::string EscapeHtml(std::string_view value);
    [[nodiscard]] std::string FontCssName(FontFamily family);
    [[nodiscard]] std::string BuildStoryHtml(const TextBoxModel& box);
    [[nodiscard]] std::string BuildStoryCss(const TextBoxModel& box);
}

