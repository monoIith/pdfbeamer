#include "TestHarness.h"

#include "PdfEditor/Core/PdfWorker.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using pdfeditor::core::PdfWorker;

TEST(PdfWorker, SerializesSubmittedOperations)
{
    PdfWorker worker;
    std::atomic_int active{};
    std::atomic_int maximum{};
    std::vector<std::future<int>> futures;
    for (int value = 0; value < 8; ++value)
    {
        futures.push_back(worker.submit([&, value]
        {
            const int now = ++active;
            maximum.store(std::max(maximum.load(), now));
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            --active;
            return value;
        }));
    }

    for (int value = 0; value < 8; ++value)
    {
        EXPECT_EQ(futures[static_cast<std::size_t>(value)].get(), value);
    }
    EXPECT_EQ(maximum.load(), 1);
}
