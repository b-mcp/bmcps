#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_navigate_back(const json &arguments) {
    (void)arguments;

    debug_log::log("navigate_back invoked");
    browser_driver::DriverResult driver_result = cdp_driver::navigate_back();

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = driver_result.message;

    json result;
    result["content"] = json::array({text_content});
    result["isError"] = !driver_result.success;
    if (!driver_result.success && !driver_result.error_detail.empty()) {
        json detail_content;
        detail_content["type"] = "text";
        detail_content["text"] = "Detail: " + driver_result.error_detail;
        result["content"].push_back(detail_content);
    }
    return result;
}

namespace tool_navigate_back {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();
    input_schema["required"] = json::array();

    mcp_tools::register_tool({
        "navigate_back",
        "Go back in the current tab's navigation history. "
        "The browser must be open and a tab must be attached (call open_browser first).",
        input_schema,
        handle_navigate_back
    });
}

} // namespace tool_navigate_back
