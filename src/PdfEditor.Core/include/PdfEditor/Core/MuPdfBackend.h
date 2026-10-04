#pragma once

#include "PdfBackend.h"

#include <filesystem>
#include <memory>

namespace pdfeditor::core
{
    class MuPdfBackend final : public IPdfBackend
    {
    public:
        explicit MuPdfBackend(std::filesystem::path fontDirectory);
        ~MuPdfBackend() override;
        MuPdfBackend(const MuPdfBackend&) = delete;
        MuPdfBackend& operator=(const MuPdfBackend&) = delete;

        OpenDocumentResult open(const std::filesystem::path& path) override;
        PixelBuffer renderPage(std::size_t pageIndex,
                               double pixelsPerPoint,
                               const CancellationToken& cancellation) override;
        PixelBuffer renderTextBox(const TextBoxModel& box,
                                  const PageGeometry& page,
                                  double pixelsPerPoint,
                                  const CancellationToken& cancellation) override;
        std::string extractText(std::size_t pageIndex) override;
        LayoutResult validateTextBox(const TextBoxModel& box,
                                     const PageGeometry& page) override;
        SaveResult saveAs(const std::filesystem::path& sourcePath,
                          const std::filesystem::path& destinationPath,
                          std::span<const PageGeometry> pages,
                          std::span<const TextBoxModel> textBoxes,
                          const CancellationToken& cancellation) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
