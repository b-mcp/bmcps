#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_switch_tab(const json &arguments) {
    int index = 0;
    if (arguments.contains("index") && arguments["index"].is_number_integer()) {
        index = arguments["index"].get<int>();
    }

    debug_log::log("switch_tab invoked, index=" + std::to_string(index));
    browser_driver::DriverResult driver_result = cdp_driver::switch_tab(index);

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

namespace tool_switch_tab {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();
    input_schema["properties"]["index"] = {
        {"type", "integer"},
        {"description", "0-based tab index (page targets only)"}
    };

    mcp_tools::register_tool({
        "switch_tab",
        "Switch to a tab by 0-based index. Use list_tabs to see tab order. "
        "Call open_browser first.",
        input_schema,
        handle_switch_tab
    });
}

} // namespace tool_switch_tab
