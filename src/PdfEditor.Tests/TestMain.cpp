#include "TestHarness.h"

#ifndef PDFEDITOR_USING_GTEST
int main()
{
    int failures = 0;
    for (const auto& [name, function] : pdfeditor::tests::Registry())
    {
        try
        {
            function();
            std::cout << "[PASS] " << name << '\n';
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << pdfeditor::tests::Registry().size() - failures << " passed, " << failures << " failed\n";
    return failures == 0 ? 0 : 1;
}
#endif

