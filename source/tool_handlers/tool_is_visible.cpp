#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_is_visible(const json &arguments) {
    json result;

    if (!arguments.contains("selector") || !arguments["selector"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "is_visible requires a string selector.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string selector = arguments["selector"].get<std::string>();

    debug_log::log("is_visible invoked selector=" + selector);
    bool visible = false;
    browser_driver::DriverResult vis_result = cdp_driver::is_visible(selector, visible);

    if (!vis_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "is_visible failed: " + vis_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = visible ? "true" : "false";

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_is_visible {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"selector", {{"type", "string"}, {"description", "CSS selector."}}}
    };
    input_schema["required"] = json::array({"selector"});

    mcp_tools::register_tool({
        "is_visible",
        "Check if element is visible. Browser must be open.",
        input_schema,
        handle_is_visible
    });
}

} // namespace tool_is_visible
