#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_refresh(const json &arguments) {
    (void)arguments;

    debug_log::log("refresh invoked");
    browser_driver::DriverResult driver_result = cdp_driver::refresh();

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

namespace tool_refresh {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();
    input_schema["required"] = json::array();

    mcp_tools::register_tool({
        "refresh",
        "Reload the current tab. "
        "The browser must be open and a tab must be attached (call open_browser first).",
        input_schema,
        handle_refresh
    });
}

} // namespace tool_refresh
