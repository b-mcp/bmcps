// Smoke E2E test: actually launches Chrome, connects via CDP,
// attaches to a tab, navigates, and verifies the page URL with Runtime.evaluate.
//
// This test requires:
// - Chrome installed on the system
// - A display server (or Xvfb for headless environments)
//
// Build separately: cmake --build build --target bmcps_smoke_test
// Run: ./build/tests/bmcps_smoke_test

#include "browser/cdp/cdp_driver.hpp"
#include "browser/cdp/cdp_chrome_launch.hpp"
#include "platform/platform_abi.hpp"

#include <iostream>
#include <chrono>
#include <string>
#include <thread>

using json = nlohmann::json;

static bool test_full_browser_lifecycle() {
    std::cout << "  Starting Chrome..." << std::endl;

    // Open browser (launch, connect, discover targets, attach).
    browser_driver::DriverResult open_result = cdp_driver::open_browser();
    if (!open_result.success) {
        std::cout << "  FAIL: open_browser failed: " << open_result.message
                  << " detail: " << open_result.error_detail << std::endl;
        return false;
    }
    std::cout << "  OK: Browser opened and connected." << std::endl;

    // List tabs.
    browser_driver::TabListResult tab_list = cdp_driver::list_tabs();
    if (!tab_list.success) {
        std::cout << "  FAIL: list_tabs failed: " << tab_list.error_detail << std::endl;
        cdp_driver::disconnect();
        return false;
    }
    std::cout << "  OK: list_tabs returned " << tab_list.tabs.size() << " tab(s)." << std::endl;

    // Navigate to a known URL.
    std::string test_url = "data:text/html,<h1>BMCPS Smoke Test</h1>";
    browser_driver::NavigateResult navigate_result = cdp_driver::navigate(test_url);
    if (!navigate_result.success) {
        std::cout << "  FAIL: navigate failed: " << navigate_result.error_text << std::endl;
        cdp_driver::disconnect();
        return false;
    }
    std::cout << "  OK: Navigated to test URL." << std::endl;

    // Give the page a moment to load.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // Verify via Runtime.evaluate that document.title or location.href is correct.
    json evaluate_params;
    evaluate_params["expression"] = "document.location.href";
    json evaluate_response = cdp_driver::send_command("Runtime.evaluate", evaluate_params,
                                                       cdp_driver::get_state().current_session_id);

    bool url_verified = false;
    if (evaluate_response.contains("result") &&
        evaluate_response["result"].contains("result") &&
        evaluate_response["result"]["result"].contains("value")) {
        std::string actual_url = evaluate_response["result"]["result"]["value"].get<std::string>();
        std::cout << "  Runtime.evaluate returned URL: " << actual_url << std::endl;
        // data: URLs may be slightly transformed, so check prefix.
        if (actual_url.find("data:text/html") == 0) {
            url_verified = true;
            std::cout << "  OK: URL verification passed." << std::endl;
        } else {
            std::cout << "  FAIL: URL mismatch. Expected data:text/html prefix." << std::endl;
        }
    } else {
        std::cout << "  FAIL: Runtime.evaluate returned unexpected response: "
                  << evaluate_response.dump() << std::endl;
    }

    // Cleanup.
    cdp_driver::disconnect();
    std::cout << "  OK: Disconnected and cleaned up." << std::endl;

    return url_verified;
}

int main() {
    std::cout << "=== BMCPS Smoke E2E Test ===" << std::endl;

    cdp_driver::initialize();

    auto start_time = std::chrono::steady_clock::now();
    bool passed = test_full_browser_lifecycle();
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    long elapsed_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    std::cout << std::endl;
    if (passed) {
        std::cout << "PASSED (" << elapsed_milliseconds << " ms)" << std::endl;
    } else {
        std::cout << "FAILED (" << elapsed_milliseconds << " ms)" << std::endl;
    }

    return passed ? 0 : 1;
}
