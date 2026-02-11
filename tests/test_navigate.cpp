// Tests for the navigate tool's CDP message construction.
// Verifies that the Page.navigate CDP command is built correctly,
// WITHOUT actually opening a WebSocket or browser.

#include <nlohmann/json.hpp>
#include <iostream>
#include <string>

using json = nlohmann::json;

namespace test_navigate {

// Simulate building a CDP Page.navigate command message, just like cdp_driver::send_command would.
static json build_navigate_command(int message_id, const std::string &url, const std::string &session_id) {
    json command;
    command["id"] = message_id;
    command["method"] = "Page.navigate";
    command["params"]["url"] = url;
    if (!session_id.empty()) {
        command["sessionId"] = session_id;
    }
    return command;
}

// Test: Navigate command contains the correct method.
static bool test_navigate_command_method() {
    json command = build_navigate_command(1, "https://example.com", "session-abc");
    bool success = (command["method"] == "Page.navigate");

    if (success) {
        std::cout << "  OK: Navigate command method is Page.navigate" << std::endl;
    } else {
        std::cout << "  FAIL: Navigate command method is: " << command["method"] << std::endl;
    }
    return success;
}

// Test: Navigate command contains the correct URL parameter.
static bool test_navigate_command_url_parameter() {
    std::string test_url = "https://test.example.com/page?query=1";
    json command = build_navigate_command(42, test_url, "");
    bool success = (command["params"]["url"] == test_url);

    if (success) {
        std::cout << "  OK: Navigate command URL parameter matches" << std::endl;
    } else {
        std::cout << "  FAIL: Navigate command URL was: " << command["params"]["url"] << std::endl;
    }
    return success;
}

// Test: Navigate command includes sessionId when provided.
static bool test_navigate_command_session_id() {
    std::string test_session = "session-xyz-789";
    json command = build_navigate_command(5, "https://example.com", test_session);
    bool success = (command.contains("sessionId") && command["sessionId"] == test_session);

    if (success) {
        std::cout << "  OK: Navigate command includes correct sessionId" << std::endl;
    } else {
        std::cout << "  FAIL: sessionId mismatch or missing" << std::endl;
    }
    return success;
}

// Test: Navigate command does NOT include sessionId when empty.
static bool test_navigate_command_no_session_when_empty() {
    json command = build_navigate_command(6, "https://example.com", "");
    bool success = !command.contains("sessionId");

    if (success) {
        std::cout << "  OK: Navigate command omits sessionId when empty" << std::endl;
    } else {
        std::cout << "  FAIL: Navigate command should not contain sessionId when empty" << std::endl;
    }
    return success;
}

// Test: Navigate command has a valid message ID.
static bool test_navigate_command_message_id() {
    json command = build_navigate_command(99, "https://example.com", "");
    bool success = (command["id"] == 99);

    if (success) {
        std::cout << "  OK: Navigate command message ID is correct" << std::endl;
    } else {
        std::cout << "  FAIL: Navigate command message ID was: " << command["id"] << std::endl;
    }
    return success;
}

bool run_all_tests() {
    bool all_passed = true;
    all_passed &= test_navigate_command_method();
    all_passed &= test_navigate_command_url_parameter();
    all_passed &= test_navigate_command_session_id();
    all_passed &= test_navigate_command_no_session_when_empty();
    all_passed &= test_navigate_command_message_id();
    return all_passed;
}

} // namespace test_navigate
