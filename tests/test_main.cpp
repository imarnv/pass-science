#include "test_framework.h"

#include <chrono>
#include <cstring>

// Usage: wb_tests [name-filter]
int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedTests = 0;
    for (const auto& test : wbtest::registry()) {
        if (filter && test.name.find(filter) == std::string::npos) continue;
        const int before = wbtest::failureCount();
        const auto start = std::chrono::steady_clock::now();
        try {
            test.body();
        } catch (const std::exception& e) {
            wbtest::reportFailure(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        }
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const bool ok = wbtest::failureCount() == before;
        std::cout << (ok ? "[ PASS ] " : "[ FAIL ] ") << test.name << "  (" << ms << " ms)\n";
        ++run;
        if (!ok) ++failedTests;
    }
    std::cout << "\n" << run - failedTests << "/" << run << " tests passed, "
              << wbtest::failureCount() << " failed checks\n";
    return failedTests == 0 ? 0 : 1;
}
