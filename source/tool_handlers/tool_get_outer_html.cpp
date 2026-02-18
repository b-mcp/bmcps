#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_get_outer_html(const json &arguments) {
    json result;

    if (!arguments.contains("selector") || !arguments["selector"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "get_outer_html requires a string 'selector'.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string selector = arguments["selector"].get<std::string>();

    debug_log::log("get_outer_html invoked selector=" + selector);
    browser_driver::GetPageSourceResult html_result = cdp_driver::get_outer_html(selector);

    if (!html_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "get_outer_html failed: " + html_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = html_result.html;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_get_outer_html {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"selector", {{"type", "string"}, {"description", "CSS selector of the element."}}}
    };
    input_schema["required"] = json::array({"selector"});

    mcp_tools::register_tool({
        "get_outer_html",
        "Get the outer HTML of an element by selector. Browser must be open and a tab attached.",
        input_schema,
        handle_get_outer_html
    });
}

} // namespace tool_get_outer_html
