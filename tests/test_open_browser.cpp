// Tests for the Chrome launch command-line building logic.
// Verifies that the generated argv contains the expected flags and binary name,
// WITHOUT actually spawning a process.

#include "browser/cdp/cdp_chrome_launch.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <vector>
#include <cstdio>

namespace test_open_browser {

static bool check_argument_present(const std::vector<std::string> &arguments,
                                    const std::string &expected_prefix,
                                    const std::string &test_description) {
    bool found = false;
    for (const auto &argument : arguments) {
        if (argument.find(expected_prefix) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        std::cout << "  FAIL: " << test_description << " (expected prefix '"
                  << expected_prefix << "' not found in arguments)" << std::endl;
    } else {
        std::cout << "  OK: " << test_description << std::endl;
    }
    return found;
}

// Test: Chrome command line contains --remote-debugging-port.
static bool test_command_line_has_remote_debugging_port() {
    auto command_line = cdp_chrome_launch::build_chrome_command_line("/tmp/test_profile");
    return check_argument_present(command_line.arguments,
                                   "--remote-debugging-port",
                                   "Command line contains --remote-debugging-port");
}

// Test: Chrome command line contains --user-data-dir with the given path.
static bool test_command_line_has_user_data_directory() {
    std::string test_directory = "/tmp/test_profile_xyz";
    auto command_line = cdp_chrome_launch::build_chrome_command_line(test_directory);
    return check_argument_present(command_line.arguments,
                                   "--user-data-dir=" + test_directory,
                                   "Command line contains --user-data-dir with correct path");
}

// Test: Chrome command line contains --no-first-run.
static bool test_command_line_has_no_first_run() {
    auto command_line = cdp_chrome_launch::build_chrome_command_line("/tmp/test_profile");
    return check_argument_present(command_line.arguments,
                                   "--no-first-run",
                                   "Command line contains --no-first-run");
}

// Test: Chrome executable path is non-empty (Chrome must be installed for this).
static bool test_chrome_executable_found() {
    std::string executable = cdp_chrome_launch::find_chrome_executable();
    bool found = !executable.empty();
    if (found) {
        std::cout << "  OK: Chrome executable found at: " << executable << std::endl;
    } else {
        std::cout << "  WARN: Chrome executable not found (not installed?). "
                  << "This test is informational only." << std::endl;
        // Don't fail the test suite if Chrome is not installed.
        found = true;
    }
    return found;
}

// Test: DevToolsActivePort parser with synthetic content.
static bool test_parse_devtools_active_port() {
    // Create a temporary file with known content.
    std::string temp_file = "/tmp/bmcps_test_devtools_port";
    {
        std::ofstream file(temp_file);
        file << "9333\n";
        file << "/devtools/browser/abc-123-def\n";
    }

    int parsed_port = cdp_chrome_launch::parse_devtools_active_port(temp_file);
    bool success = (parsed_port == 9333);

    if (success) {
        std::cout << "  OK: DevToolsActivePort parsed correctly (port=" << parsed_port << ")" << std::endl;
    } else {
        std::cout << "  FAIL: DevToolsActivePort parse returned " << parsed_port
                  << " (expected 9333)" << std::endl;
    }

    // Cleanup.
    std::remove(temp_file.c_str());
    return success;
}

// Test: WebSocket URL building (path already with single leading slash).
static bool test_build_websocket_url() {
    std::string url = cdp_chrome_launch::build_websocket_url(9333, "/devtools/browser/abc-123");
    bool success = (url == "ws://127.0.0.1:9333/devtools/browser/abc-123");

    if (success) {
        std::cout << "  OK: WebSocket URL built correctly: " << url << std::endl;
    } else {
        std::cout << "  FAIL: WebSocket URL was: " << url << std::endl;
    }
    return success;
}

// Test: WebSocket URL when path has multiple leading slashes (normalized to single).
static bool test_build_websocket_url_path_with_leading_slash() {
    std::string url = cdp_chrome_launch::build_websocket_url(9333, "/devtools/browser/xyz");
    bool success = (url == "ws://127.0.0.1:9333/devtools/browser/xyz");

    if (!success) {
        std::cout << "  FAIL: WebSocket URL with single slash path was: " << url << std::endl;
        return false;
    }
    url = cdp_chrome_launch::build_websocket_url(9333, "//devtools/browser/xyz");
    success = (url == "ws://127.0.0.1:9333/devtools/browser/xyz");
    if (success) {
        std::cout << "  OK: WebSocket URL with double slash path normalized to single: " << url << std::endl;
    } else {
        std::cout << "  FAIL: WebSocket URL with // path was: " << url << std::endl;
    }
    return success;
}

bool run_all_tests() {
    bool all_passed = true;
    all_passed &= test_command_line_has_remote_debugging_port();
    all_passed &= test_command_line_has_user_data_directory();
    all_passed &= test_command_line_has_no_first_run();
    all_passed &= test_chrome_executable_found();
    all_passed &= test_parse_devtools_active_port();
    all_passed &= test_build_websocket_url();
    all_passed &= test_build_websocket_url_path_with_leading_slash();
    return all_passed;
}

} // namespace test_open_browser
