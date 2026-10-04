#include "TestHarness.h"

#include "PdfEditor/Core/TextMarkup.h"

using namespace pdfeditor::core;

TEST(TextMarkup, EscapesMarkupWithoutDroppingNewlines)
{
    TextBoxModel box;
    box.text = u"A < B & \"quoted\"\nCaf\u00e9";
    const auto html = markup::BuildStoryHtml(box);
    EXPECT_EQ(html, "<div class=\"textbox\">A &lt; B &amp; &quot;quoted&quot;\nCaf\xc3\xa9</div>");
}

TEST(TextMarkup, EmitsWholeBoxStyles)
{
    TextBoxModel box;
    box.textStyle.family = FontFamily::notoSerif;
    box.textStyle.sizePoints = 18;
    box.textStyle.bold = true;
    box.textStyle.italic = true;
    box.textStyle.alignment = TextAlignment::right;
    box.textStyle.color = { 12, 34, 56 };
    const auto css = markup::BuildStoryCss(box);
    EXPECT_TRUE(css.find("Noto Serif PDE") != std::string::npos);
    EXPECT_TRUE(css.find("font-size:18") != std::string::npos);
    EXPECT_TRUE(css.find("font-weight:700") != std::string::npos);
    EXPECT_TRUE(css.find("font-style:italic") != std::string::npos);
    EXPECT_TRUE(css.find("text-align:right") != std::string::npos);
    EXPECT_TRUE(css.find("#0c2238") != std::string::npos);
}

TEST(TextMarkup, ReplacesInvalidSurrogates)
{
    const std::u16string invalid{ static_cast<char16_t>(0xd800), u'A' };
    EXPECT_EQ(markup::Utf16ToUtf8(invalid), "\xef\xbf\xbd" "A");
}

