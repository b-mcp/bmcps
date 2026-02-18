#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_get_element_bounding_box(const json &arguments) {
    json result;

    if (!arguments.contains("selector") || !arguments["selector"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "get_element_bounding_box requires a string selector.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string selector = arguments["selector"].get<std::string>();

    debug_log::log("get_element_bounding_box invoked selector=" + selector);
    browser_driver::BoundingBoxResult box_result = cdp_driver::get_element_bounding_box(selector);

    if (!box_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "get_element_bounding_box failed: " + box_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json box_json;
    box_json["x"] = box_result.x;
    box_json["y"] = box_result.y;
    box_json["width"] = box_result.width;
    box_json["height"] = box_result.height;
    json text_content;
    text_content["type"] = "text";
    text_content["text"] = box_json.dump();

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_get_element_bounding_box {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"selector", {{"type", "string"}, {"description", "CSS selector of the element."}}}
    };
    input_schema["required"] = json::array({"selector"});

    mcp_tools::register_tool({
        "get_element_bounding_box",
        "Get getBoundingClientRect (x, y, width, height) for an element. Browser must be open and a tab attached.",
        input_schema,
        handle_get_element_bounding_box
    });
}

} // namespace tool_get_element_bounding_box
