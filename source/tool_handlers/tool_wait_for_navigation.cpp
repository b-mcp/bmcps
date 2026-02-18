#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_wait_for_navigation(const json &arguments) {
    json result;

    int timeout_milliseconds = 10000;
    if (arguments.contains("timeout_milliseconds") && arguments["timeout_milliseconds"].is_number_integer()) {
        timeout_milliseconds = arguments["timeout_milliseconds"].get<int>();
    }

    debug_log::log("wait_for_navigation invoked");
    browser_driver::DriverResult wait_result = cdp_driver::wait_for_navigation(timeout_milliseconds);

    if (!wait_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "wait_for_navigation failed: " + wait_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = wait_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_wait_for_navigation {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"timeout_milliseconds", {{"type", "integer"}, {"description", "Timeout in ms (default 10000)."}}}
    };

    mcp_tools::register_tool({
        "wait_for_navigation",
        "Wait until document.readyState is complete. Browser must be open and a tab attached.",
        input_schema,
        handle_wait_for_navigation
    });
}

} // namespace tool_wait_for_navigation
