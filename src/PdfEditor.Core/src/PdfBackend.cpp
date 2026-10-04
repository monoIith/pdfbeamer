#include "PdfEditor/Core/PdfBackend.h"

#include <atomic>

namespace pdfeditor::core
{
    CancellationToken::CancellationToken()
        : cancelled_(std::make_shared<std::atomic_bool>(false))
    {
    }

    void CancellationToken::cancel() const noexcept
    {
        cancelled_->store(true, std::memory_order_relaxed);
    }

    bool CancellationToken::cancelled() const noexcept
    {
        return cancelled_->load(std::memory_order_relaxed);
    }
}

