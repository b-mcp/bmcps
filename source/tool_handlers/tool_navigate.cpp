#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Tool handler for "navigate".
// Navigates the current (default) tab to the given URL via CDP Page.navigate.

static json handle_navigate(const json &arguments) {
    // Extract the required "url" parameter.
    if (!arguments.contains("url") || !arguments["url"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "Missing required parameter 'url' (string).";

        json result;
        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string url = arguments["url"].get<std::string>();

    debug_log::log("navigate invoked");
    debug_log::log("Navigating to URL: " + url);
    browser_driver::NavigateResult navigate_result = cdp_driver::navigate(url);

    json text_content;
    text_content["type"] = "text";

    json result;

    if (navigate_result.success) {
        text_content["text"] = "Navigated to " + url;
        result["isError"] = false;
    } else {
        text_content["text"] = "Navigation failed: " + navigate_result.error_text;
        result["isError"] = true;
    }

    result["content"] = json::array({text_content});
    return result;
}

namespace tool_navigate {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();
    input_schema["properties"]["url"] = {
        {"type", "string"},
        {"description", "The URL to navigate to (e.g. https://example.com)"}
    };
    input_schema["required"] = json::array({"url"});

    mcp_tools::register_tool({
        "navigate",
        "Navigate the current browser tab to the specified URL. "
        "The browser must be open and a tab must be attached (call open_browser first).",
        input_schema,
        handle_navigate
    });
}

} // namespace tool_navigate
