#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_hover_element(const json &arguments) {
    json result;

    if (!arguments.contains("selector") || !arguments["selector"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "hover_element requires a string 'selector' (e.g. from list_interactive_elements).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string selector = arguments["selector"].get<std::string>();

    debug_log::log("hover_element invoked selector=" + selector);
    browser_driver::DriverResult hover_result = cdp_driver::hover_element(selector);

    if (!hover_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "hover_element failed: " + hover_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = hover_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_hover_element {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"selector", {{"type", "string"}, {"description", "CSS selector (e.g. from list_interactive_elements)."}}}
    };
    input_schema["required"] = json::array({"selector"});

    mcp_tools::register_tool({
        "hover_element",
        "Move the mouse over an element by selector (hover). Use selectors from list_interactive_elements. Browser must be open and a tab attached.",
        input_schema,
        handle_hover_element
    });
}

} // namespace tool_hover_element
