#pragma once
// Minimal self-contained test harness (no external dependency).
//
//   TEST_CASE("name") { CHECK(cond); CHECK_NEAR(a, b, tol); CHECK_THROWS_AS(expr, Type); }

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace wbtest {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failureCount() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

inline void reportFailure(const char* file, int line, const std::string& message) {
    ++failureCount();
    std::cerr << "    FAILED " << file << ":" << line << "  " << message << "\n";
}

} // namespace wbtest

#define WB_CONCAT_INNER(a, b) a##b
#define WB_CONCAT(a, b) WB_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                                     \
    static void WB_CONCAT(wb_test_fn_, __LINE__)();                                         \
    static const wbtest::Registrar WB_CONCAT(wb_test_reg_, __LINE__)(name, &WB_CONCAT(wb_test_fn_, __LINE__)); \
    static void WB_CONCAT(wb_test_fn_, __LINE__)()

#define CHECK(cond)                                                                         \
    do {                                                                                    \
        if (!(cond)) wbtest::reportFailure(__FILE__, __LINE__, "CHECK(" #cond ")");         \
    } while (0)

#define CHECK_NEAR(actual, expected, tol)                                                   \
    do {                                                                                    \
        const double wb_a = (actual), wb_e = (expected);                                    \
        if (!(std::abs(wb_a - wb_e) <= (tol))) {                                            \
            std::ostringstream wb_os;                                                       \
            wb_os.precision(15);                                                            \
            wb_os << "CHECK_NEAR(" #actual ", " #expected ") actual=" << wb_a               \
                  << " expected=" << wb_e << " tol=" << (tol);                              \
            wbtest::reportFailure(__FILE__, __LINE__, wb_os.str());                         \
        }                                                                                   \
    } while (0)

#define CHECK_THROWS_AS(expr, ExceptionType)                                                \
    do {                                                                                    \
        bool wb_thrown = false;                                                             \
        try { (void)(expr); } catch (const ExceptionType&) { wb_thrown = true; } catch (...) {} \
        if (!wb_thrown) wbtest::reportFailure(__FILE__, __LINE__, "expected " #ExceptionType " from " #expr); \
    } while (0)

// Checks that `expr` throws ExceptionType whose what() contains `fragment`.
#define CHECK_THROWS_WITH(expr, ExceptionType, fragment)                                    \
    do {                                                                                    \
        std::string wb_msg;                                                                 \
        bool wb_thrown = false;                                                             \
        try { (void)(expr); } catch (const ExceptionType& e) { wb_thrown = true; wb_msg = e.what(); } catch (...) {} \
        if (!wb_thrown || wb_msg.find(fragment) == std::string::npos)                       \
            wbtest::reportFailure(__FILE__, __LINE__, "expected " #ExceptionType " containing '" \
                                  + std::string(fragment) + "' from " #expr ", got: '" + wb_msg + "'"); \
    } while (0)
