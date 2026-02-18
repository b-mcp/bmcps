#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_new_tab(const json &arguments) {
    std::string url = "about:blank";
    if (arguments.contains("url") && arguments["url"].is_string()) {
        url = arguments["url"].get<std::string>();
    }

    debug_log::log("new_tab invoked, url=" + url);
    browser_driver::DriverResult driver_result = cdp_driver::new_tab(url);

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

namespace tool_new_tab {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();
    input_schema["properties"]["url"] = {
        {"type", "string"},
        {"description", "Optional URL to open in the new tab (default: about:blank)"}
    };

    mcp_tools::register_tool({
        "new_tab",
        "Open a new browser tab and attach to it. Optionally provide a URL to load. "
        "The new tab becomes the current target for subsequent navigate calls. "
        "Call open_browser first.",
        input_schema,
        handle_new_tab
    });
}

} // namespace tool_new_tab
