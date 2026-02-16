#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Tool handler for "open_browser".
// Launches Chrome, connects via CDP, discovers targets, and attaches
// to a default tab. Stores currentTargetId and currentSessionId for
// subsequent tool calls (navigate, etc.).

static json handle_open_browser(const json &arguments) {
    (void)arguments; // No required parameters for now.

    debug_log::log("open_browser invoked");
    browser_driver::DriverResult driver_result = cdp_driver::open_browser();

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = driver_result.message;

    json result;
    result["content"] = json::array({text_content});

    if (!driver_result.success) {
        result["isError"] = true;
        // Append error detail if available.
        if (!driver_result.error_detail.empty()) {
            json detail_content;
            detail_content["type"] = "text";
            detail_content["text"] = "Detail: " + driver_result.error_detail;
            result["content"].push_back(detail_content);
        }
    } else {
        result["isError"] = false;
    }

    return result;
}

namespace tool_open_browser {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();
    // No required parameters; the tool launches Chrome with defaults.

    mcp_tools::register_tool({
        "open_browser",
        "Launch a browser (Chrome) and connect to it via CDP. "
        "Discovers available tabs and attaches to the default page tab. "
        "Must be called before navigate or other browser tools.",
        input_schema,
        handle_open_browser
    });
}

} // namespace tool_open_browser
