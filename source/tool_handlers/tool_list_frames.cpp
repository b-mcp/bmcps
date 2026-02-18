#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_list_frames(const json &arguments) {
    (void)arguments;
    json result;

    debug_log::log("list_frames invoked");
    browser_driver::ListFramesResult frames_result = cdp_driver::list_frames();

    if (!frames_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "list_frames failed: " + frames_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json frame_list = json::array();
    for (const auto &frame : frames_result.frames) {
        json item;
        item["frame_id"] = frame.frame_id;
        item["url"] = frame.url;
        item["parent_frame_id"] = frame.parent_frame_id;
        frame_list.push_back(item);
    }
    json text_content;
    text_content["type"] = "text";
    text_content["text"] = frame_list.dump();

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_list_frames {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();

    mcp_tools::register_tool({
        "list_frames",
        "List all frames in the current page (frame_id, url, parent_frame_id). Browser must be open and a tab attached.",
        input_schema,
        handle_list_frames
    });
}

} // namespace tool_list_frames
