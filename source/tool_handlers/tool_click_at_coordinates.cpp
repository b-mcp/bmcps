#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_click_at_coordinates(const json &arguments) {
    json result;

    if (!arguments.contains("x") || !arguments["x"].is_number()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "click_at_coordinates requires number 'x' (viewport CSS pixels).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }
    if (!arguments.contains("y") || !arguments["y"].is_number()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "click_at_coordinates requires number 'y' (viewport CSS pixels).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    int x = arguments["x"].get<int>();
    int y = arguments["y"].get<int>();

    debug_log::log("click_at_coordinates invoked x=" + std::to_string(x) + " y=" + std::to_string(y));
    browser_driver::DriverResult click_result = cdp_driver::click_at_coordinates(x, y);

    if (!click_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "click_at_coordinates failed: " + click_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = click_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_click_at_coordinates {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"x", {{"type", "number"}, {"description", "X coordinate in viewport (CSS pixels from left)."}}},
        {"y", {{"type", "number"}, {"description", "Y coordinate in viewport (CSS pixels from top)."}}}
    };
    input_schema["required"] = json::array({"x", "y"});

    mcp_tools::register_tool({
        "click_at_coordinates",
        "Click at viewport coordinates (x, y). Useful for canvas or when no DOM selector is available. Browser must be open and a tab attached.",
        input_schema,
        handle_click_at_coordinates
    });
}

} // namespace tool_click_at_coordinates
