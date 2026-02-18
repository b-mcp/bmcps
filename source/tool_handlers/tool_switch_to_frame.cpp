#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_switch_to_frame(const json &arguments) {
    json result;

    if (!arguments.contains("frame_id_or_index") || !arguments["frame_id_or_index"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "switch_to_frame requires a string frame_id_or_index (from list_frames, or 0 for main).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string frame_id_or_index = arguments["frame_id_or_index"].get<std::string>();

    debug_log::log("switch_to_frame invoked");
    browser_driver::DriverResult switch_result = cdp_driver::switch_to_frame(frame_id_or_index);

    if (!switch_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "switch_to_frame failed: " + switch_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = switch_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_switch_to_frame {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"frame_id_or_index", {{"type", "string"}, {"description", "Frame id from list_frames or index (0 = main)."}}}
    };
    input_schema["required"] = json::array({"frame_id_or_index"});

    mcp_tools::register_tool({
        "switch_to_frame",
        "Switch execution context to a frame. Use list_frames to get frame_id. Browser must be open and a tab attached.",
        input_schema,
        handle_switch_to_frame
    });
}

} // namespace tool_switch_to_frame
