// Test runner: runs all parameter / build verification tests and reports results.

#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <chrono>

// Forward declarations of test functions from other test files.
namespace test_open_browser {
    bool run_all_tests();
}

namespace test_navigate {
    bool run_all_tests();
}

struct TestSuite {
    std::string name;
    std::function<bool()> runner;
};

int main() {
    std::vector<TestSuite> suites = {
        {"test_open_browser", test_open_browser::run_all_tests},
        {"test_navigate", test_navigate::run_all_tests},
    };

    int passed_count = 0;
    int failed_count = 0;
    auto total_start_time = std::chrono::steady_clock::now();

    std::cout << "=== BMCPS Test Runner ===" << std::endl;
    std::cout << std::endl;

    for (const auto &suite : suites) {
        std::cout << "--- " << suite.name << " ---" << std::endl;
        auto suite_start_time = std::chrono::steady_clock::now();

        bool suite_passed = suite.runner();

        auto suite_elapsed = std::chrono::steady_clock::now() - suite_start_time;
        long suite_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(suite_elapsed).count();

        if (suite_passed) {
            std::cout << "  PASSED (" << suite_milliseconds << " ms)" << std::endl;
            passed_count++;
        } else {
            std::cout << "  FAILED (" << suite_milliseconds << " ms)" << std::endl;
            failed_count++;
        }
        std::cout << std::endl;
    }

    auto total_elapsed = std::chrono::steady_clock::now() - total_start_time;
    long total_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(total_elapsed).count();

    std::cout << "=== Results ===" << std::endl;
    std::cout << "  Passed: " << passed_count << std::endl;
    std::cout << "  Failed: " << failed_count << std::endl;
    std::cout << "  Total time: " << total_milliseconds << " ms" << std::endl;

    return (failed_count == 0) ? 0 : 1;
}
