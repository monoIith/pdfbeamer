#include "TestHarness.h"

#include "PdfEditor/Core/MuPdfBackend.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace pdfeditor::core;

    fs::path FindRepositoryRoot()
    {
        std::vector<fs::path> starts{ fs::current_path(), fs::path(__FILE__).parent_path() };
        for (auto start : starts)
        {
            if (start.empty()) continue;
            start = fs::absolute(start);
            for (auto current = start; !current.empty(); current = current.parent_path())
            {
                if (fs::exists(current / "src/PdfEditor.App/Assets/Fonts/NotoSans-Regular.ttf"))
                {
                    return current;
                }
                if (current == current.root_path()) break;
            }
        }
        throw std::runtime_error("Could not locate the repository root for integration-test fonts.");
    }

    std::string MinimalTwoPagePdf()
    {
        const std::vector<std::string> objects{
            "<< /Type /Catalog /Pages 2 0 R >>",
            "<< /Type /Pages /Count 2 /Kids [3 0 R 5 0 R] >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> /Contents 4 0 R >>",
            "<< /Length 25 >>\nstream\n0.2 w 36 36 540 720 re S\nendstream",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /CropBox [50 100 550 700] /Rotate 90 /Resources << >> /Contents 6 0 R >>",
            "<< /Length 0 >>\nstream\nendstream",
        };

        std::ostringstream pdf;
        pdf << "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
        std::vector<std::streamoff> offsets(objects.size() + 1);
        for (std::size_t index = 0; index < objects.size(); ++index)
        {
            offsets[index + 1] = pdf.tellp();
            pdf << index + 1 << " 0 obj\n" << objects[index] << "\nendobj\n";
        }
        const auto xref = pdf.tellp();
        pdf << "xref\n0 " << objects.size() + 1 << "\n0000000000 65535 f \n";
        for (std::size_t index = 1; index < offsets.size(); ++index)
        {
            pdf.width(10);
            pdf.fill('0');
            pdf << offsets[index] << " 00000 n \n";
        }
        pdf << "trailer\n<< /Size " << objects.size() + 1 << " /Root 1 0 R >>\n"
            << "startxref\n" << xref << "\n%%EOF\n";
        return pdf.str();
    }

    std::string ReadFile(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    }
}

TEST(MuPdfIntegration, SavesSearchableUnicodeWithoutChangingSource)
{
    const auto root = FindRepositoryRoot();
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = fs::temp_directory_path() / ("pdfeditor-integration-" + unique);
    fs::create_directories(directory);
    const auto source = directory / "source.pdf";
    const auto output = directory / "output.pdf";
    {
        std::ofstream stream(source, std::ios::binary);
        stream << MinimalTwoPagePdf();
    }
    const auto sourceBefore = ReadFile(source);

    MuPdfBackend backend(root / "src/PdfEditor.App/Assets/Fonts");
    const auto opened = backend.open(source);
    ASSERT_EQ(opened.pages.size(), 2u);
    EXPECT_EQ(opened.pages[1].rotation, 90);
    EXPECT_EQ(opened.pages[1].cropBox.width, 500.0);
    EXPECT_EQ(opened.pages[1].cropBox.height, 600.0);

    TextBoxModel box;
    box.id = "integration-text";
    box.pageIndex = 1;
    box.bounds = { 40, 50, 320, 100 };
    box.text = u"Résumé — Ελληνικά — Привет";
    box.textStyle.sizePoints = 16;
    box.boxStyle.fillColor = { 255, 224, 80 };
    box.boxStyle.fillOpacity = 0.35;
    EXPECT_TRUE(backend.validateTextBox(box, opened.pages[1]).fits);

    for (int family = 0; family < 3; ++family)
    {
        for (int style = 0; style < 4; ++style)
        {
            auto styled = box;
            styled.textStyle.family = static_cast<FontFamily>(family);
            styled.textStyle.bold = (style & 1) != 0;
            styled.textStyle.italic = (style & 2) != 0;
            EXPECT_TRUE(backend.validateTextBox(styled, opened.pages[1]).fits);
            const auto preview = backend.renderTextBox(styled, opened.pages[1], 0.5, CancellationToken{});
            EXPECT_TRUE(preview.width > 0);
            EXPECT_TRUE(std::any_of(preview.pixels.begin(), preview.pixels.end(), [](std::uint8_t value)
            {
                return value != 0;
            }));
        }
    }

    TextBoxModel firstPageBox;
    firstPageBox.id = "first-page-text";
    firstPageBox.pageIndex = 0;
    firstPageBox.bounds = { 72, 72, 220, 60 };
    firstPageBox.text = u"First page overlay";
    firstPageBox.textStyle.family = FontFamily::notoSerif;
    firstPageBox.textStyle.bold = true;
    firstPageBox.textStyle.italic = true;
    EXPECT_TRUE(backend.validateTextBox(firstPageBox, opened.pages[0]).fits);

    const std::vector<TextBoxModel> boxes{ box, firstPageBox };
    const auto saved = backend.saveAs(source, output, opened.pages, boxes, CancellationToken{});
    EXPECT_TRUE(saved.bytesWritten > 0);
    EXPECT_EQ(ReadFile(source), sourceBefore);

    const auto reopened = backend.open(output);
    ASSERT_EQ(reopened.pages.size(), 2u);
    EXPECT_EQ(reopened.pages[1].rotation, opened.pages[1].rotation);
    EXPECT_EQ(reopened.pages[1].cropBox, opened.pages[1].cropBox);
    EXPECT_EQ(reopened.pages[1].mediaBox, opened.pages[1].mediaBox);
    EXPECT_TRUE(backend.extractText(0).find("First page overlay") != std::string::npos);
    const auto extracted = backend.extractText(1);
    EXPECT_TRUE(extracted.find("Résumé") != std::string::npos);
    EXPECT_TRUE(extracted.find("Ελληνικά") != std::string::npos);
    EXPECT_TRUE(extracted.find("Привет") != std::string::npos);
    const auto rendered = backend.renderPage(1, 0.5, CancellationToken{});
    EXPECT_EQ(rendered.width, 300);
    EXPECT_EQ(rendered.height, 250);

    fs::remove_all(directory);
}

TEST(MuPdfIntegration, RefusesToOverwriteTheSource)
{
    const auto root = FindRepositoryRoot();
    const auto source = fs::temp_directory_path() / "pdfeditor-same-path.pdf";
    {
        std::ofstream stream(source, std::ios::binary);
        stream << MinimalTwoPagePdf();
    }
    MuPdfBackend backend(root / "src/PdfEditor.App/Assets/Fonts");
    const auto opened = backend.open(source);
    EXPECT_THROW(backend.saveAs(source, source, opened.pages,
                                std::span<const TextBoxModel>{}, CancellationToken{}), PdfException);
    fs::remove(source);
}
