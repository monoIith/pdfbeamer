#pragma once

#if __has_include(<gtest/gtest.h>)
#include <gtest/gtest.h>
#define PDFEDITOR_USING_GTEST 1
#else
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pdfeditor::tests
{
    using TestFunction = void (*)();
    inline std::vector<std::pair<std::string, TestFunction>>& Registry()
    {
        static std::vector<std::pair<std::string, TestFunction>> tests;
        return tests;
    }

    struct Registrar
    {
        Registrar(std::string name, TestFunction function)
        {
            Registry().emplace_back(std::move(name), function);
        }
    };

    template <typename Left, typename Right>
    void ExpectEqual(const Left& left, const Right& right, const char* expression, const char* file, int line)
    {
        if (!(left == right))
        {
            std::ostringstream message;
            message << file << ':' << line << " expectation failed: " << expression;
            throw std::runtime_error(message.str());
        }
    }

    inline void ExpectNear(double left, double right, double tolerance, const char* expression, const char* file, int line)
    {
        if (std::abs(left - right) > tolerance)
        {
            std::ostringstream message;
            message << file << ':' << line << " expectation failed: " << expression
                    << " (" << left << " vs " << right << ')';
            throw std::runtime_error(message.str());
        }
    }
}

#define TEST(suite, name) \
    static void suite##_##name(); \
    static ::pdfeditor::tests::Registrar suite##_##name##_registrar(#suite "." #name, &suite##_##name); \
    static void suite##_##name()
#define EXPECT_EQ(left, right) ::pdfeditor::tests::ExpectEqual((left), (right), #left " == " #right, __FILE__, __LINE__)
#define ASSERT_EQ(left, right) EXPECT_EQ(left, right)
#define EXPECT_TRUE(value) ::pdfeditor::tests::ExpectEqual(static_cast<bool>(value), true, #value, __FILE__, __LINE__)
#define EXPECT_FALSE(value) ::pdfeditor::tests::ExpectEqual(static_cast<bool>(value), false, "!(" #value ")", __FILE__, __LINE__)
#define EXPECT_NEAR(left, right, tolerance) ::pdfeditor::tests::ExpectNear((left), (right), (tolerance), #left " ~= " #right, __FILE__, __LINE__)
#define EXPECT_THROW(statement, exceptionType) \
    do { bool threw = false; try { statement; } catch (const exceptionType&) { threw = true; } \
    ::pdfeditor::tests::ExpectEqual(threw, true, #statement " throws " #exceptionType, __FILE__, __LINE__); } while (false)
#endif

