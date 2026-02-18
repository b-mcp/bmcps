#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_switch_to_main_frame(const json &arguments) {
    (void)arguments;
    json result;

    debug_log::log("switch_to_main_frame invoked");
    browser_driver::DriverResult switch_result = cdp_driver::switch_to_main_frame();

    if (!switch_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "switch_to_main_frame failed: " + switch_result.error_detail;

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

namespace tool_switch_to_main_frame {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {};

    mcp_tools::register_tool({
        "switch_to_main_frame",
        "Switch execution context back to the main frame. Browser must be open and a tab attached.",
        input_schema,
        handle_switch_to_main_frame
    });
}

} // namespace tool_switch_to_main_frame
