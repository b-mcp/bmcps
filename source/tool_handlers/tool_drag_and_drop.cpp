#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_drag_and_drop(const json &arguments) {
    json result;

    if (!arguments.contains("source_selector") || !arguments["source_selector"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "drag_and_drop requires a string 'source_selector'.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }
    if (!arguments.contains("target_selector") || !arguments["target_selector"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "drag_and_drop requires a string 'target_selector'.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string source_selector = arguments["source_selector"].get<std::string>();
    std::string target_selector = arguments["target_selector"].get<std::string>();

    debug_log::log("drag_and_drop invoked");
    browser_driver::DriverResult drag_result = cdp_driver::drag_and_drop_selectors(source_selector, target_selector);

    if (!drag_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "drag_and_drop failed: " + drag_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = drag_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_drag_and_drop {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"source_selector", {{"type", "string"}, {"description", "CSS selector of the element to drag."}}},
        {"target_selector", {{"type", "string"}, {"description", "CSS selector of the drop target."}}}
    };
    input_schema["required"] = json::array({"source_selector", "target_selector"});

    mcp_tools::register_tool({
        "drag_and_drop",
        "Drag an element to another by selectors. Use selectors from list_interactive_elements. Browser must be open and a tab attached.",
        input_schema,
        handle_drag_and_drop
    });
}

} // namespace tool_drag_and_drop
