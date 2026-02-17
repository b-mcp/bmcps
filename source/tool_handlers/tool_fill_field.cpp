#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_fill_field(const json &arguments) {
    json result;

    if (!arguments.contains("selector") || !arguments["selector"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "fill_field requires a string 'selector' (from list_interactive_elements).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }
    if (!arguments.contains("value")) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "fill_field requires 'value' (string to type).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string selector = arguments["selector"].get<std::string>();
    std::string value = arguments["value"].is_string() ? arguments["value"].get<std::string>() : arguments["value"].dump();
    bool clear_first = arguments.value("clear_first", true);

    debug_log::log("fill_field invoked selector=" + selector);
    browser_driver::DriverResult fill_result = cdp_driver::fill_field(selector, value, clear_first);

    if (!fill_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "fill_field failed: " + fill_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = fill_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_fill_field {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"selector", {{"type", "string"}, {"description", "CSS selector (e.g. from list_interactive_elements)."}}},
        {"value", {{"type", "string"}, {"description", "Text to type into the field."}}},
        {"clear_first", {{"type", "boolean"}, {"description", "Clear the field before typing. Default true."}, {"default", true}}}
    };
    input_schema["required"] = json::array({"selector", "value"});

    mcp_tools::register_tool({
        "fill_field",
        "Fill an input or textarea by selector. Use selectors from list_interactive_elements. Optionally clear the field first (default true). Browser must be open and a tab attached.",
        input_schema,
        handle_fill_field
    });
}

} // namespace tool_fill_field
