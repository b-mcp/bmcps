#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_close_tab(const json &arguments) {
    (void)arguments;

    debug_log::log("close_tab invoked");
    browser_driver::DriverResult driver_result = cdp_driver::close_tab();

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

namespace tool_close_tab {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();

    mcp_tools::register_tool({
        "close_tab",
        "Close the current tab. If other tabs exist, attaches to the first one. "
        "Call open_browser first.",
        input_schema,
        handle_close_tab
    });
}

} // namespace tool_close_tab
