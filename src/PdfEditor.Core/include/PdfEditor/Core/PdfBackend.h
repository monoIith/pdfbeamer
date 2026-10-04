#pragma once

#include "Models.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

namespace pdfeditor::core
{
    class PdfException final : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    class CancellationToken
    {
    public:
        CancellationToken();
        void cancel() const noexcept;
        [[nodiscard]] bool cancelled() const noexcept;

    private:
        std::shared_ptr<std::atomic_bool> cancelled_;
    };

    class IPdfBackend
    {
    public:
        virtual ~IPdfBackend() = default;
        virtual OpenDocumentResult open(const std::filesystem::path& path) = 0;
        virtual PixelBuffer renderPage(std::size_t pageIndex,
                                       double pixelsPerPoint,
                                       const CancellationToken& cancellation) = 0;
        virtual PixelBuffer renderTextBox(const TextBoxModel& box,
                                          const PageGeometry& page,
                                          double pixelsPerPoint,
                                          const CancellationToken& cancellation) = 0;
        virtual std::string extractText(std::size_t pageIndex) = 0;
        virtual LayoutResult validateTextBox(const TextBoxModel& box,
                                             const PageGeometry& page) = 0;
        virtual SaveResult saveAs(const std::filesystem::path& sourcePath,
                                  const std::filesystem::path& destinationPath,
                                  std::span<const PageGeometry> pages,
                                  std::span<const TextBoxModel> textBoxes,
                                  const CancellationToken& cancellation) = 0;
    };
}
