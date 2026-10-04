#include "PdfEditor/Core/PdfWorker.h"

namespace pdfeditor::core
{
    PdfWorker::PdfWorker()
        : thread_([this] { run(); })
    {
    }

    PdfWorker::~PdfWorker()
    {
        {
            std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        condition_.notify_all();
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    void PdfWorker::run()
    {
        for (;;)
        {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                if (stopping_ && tasks_.empty())
                {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            task();
        }
    }
}

