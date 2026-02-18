#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_drag_from_to(const json &arguments) {
    json result;

    if (!arguments.contains("x1") || !arguments["x1"].is_number_integer()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "drag_from_to requires integer x1 (viewport x).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }
    if (!arguments.contains("y1") || !arguments["y1"].is_number_integer()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "drag_from_to requires integer y1 (viewport y).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }
    if (!arguments.contains("x2") || !arguments["x2"].is_number_integer()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "drag_from_to requires integer x2 (viewport x).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }
    if (!arguments.contains("y2") || !arguments["y2"].is_number_integer()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "drag_from_to requires integer y2 (viewport y).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    int x1 = arguments["x1"].get<int>();
    int y1 = arguments["y1"].get<int>();
    int x2 = arguments["x2"].get<int>();
    int y2 = arguments["y2"].get<int>();

    debug_log::log("drag_from_to invoked");
    browser_driver::DriverResult drag_result = cdp_driver::drag_from_to_coordinates(x1, y1, x2, y2);

    if (!drag_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "drag_from_to failed: " + drag_result.error_detail;

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

namespace tool_drag_from_to {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"x1", {{"type", "integer"}, {"description", "Start x viewport pixels."}}},
        {"y1", {{"type", "integer"}, {"description", "Start y viewport pixels."}}},
        {"x2", {{"type", "integer"}, {"description", "End x viewport pixels."}}},
        {"y2", {{"type", "integer"}, {"description", "End y viewport pixels."}}}
    };
    input_schema["required"] = json::array({"x1", "y1", "x2", "y2"});

    mcp_tools::register_tool({
        "drag_from_to",
        "Drag from (x1,y1) to (x2,y2) in viewport coordinates. Useful for canvas. Browser must be open and a tab attached.",
        input_schema,
        handle_drag_from_to
    });
}

} // namespace tool_drag_from_to
