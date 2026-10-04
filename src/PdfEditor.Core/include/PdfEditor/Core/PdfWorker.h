#pragma once

#include <condition_variable>
#include <deque>
#include <future>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace pdfeditor::core
{
    class PdfWorker
    {
    public:
        PdfWorker();
        ~PdfWorker();
        PdfWorker(const PdfWorker&) = delete;
        PdfWorker& operator=(const PdfWorker&) = delete;

        template <typename Function>
        auto submit(Function&& function) -> std::future<std::invoke_result_t<Function>>
        {
            using Result = std::invoke_result_t<Function>;
            auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Function>(function));
            auto future = task->get_future();
            {
                std::scoped_lock lock(mutex_);
                if (stopping_)
                {
                    throw std::runtime_error("The PDF worker is stopping.");
                }
                tasks_.emplace_back([task] { (*task)(); });
            }
            condition_.notify_one();
            return future;
        }

    private:
        void run();

        std::mutex mutex_;
        std::condition_variable condition_;
        std::deque<std::function<void()>> tasks_;
        bool stopping_{};
        std::thread thread_;
    };
}

