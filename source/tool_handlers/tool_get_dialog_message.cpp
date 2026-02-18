#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_get_dialog_message(const json &arguments) {
    (void)arguments;
    json result;

    debug_log::log("get_dialog_message invoked");
    browser_driver::GetDialogMessageResult dialog_result = cdp_driver::get_dialog_message();

    if (!dialog_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "get_dialog_message failed: " + dialog_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json out;
    out["dialog_open"] = dialog_result.dialog_open;
    out["message"] = dialog_result.message;
    out["type"] = dialog_result.type;
    json text_content;
    text_content["type"] = "text";
    text_content["text"] = out.dump();

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_get_dialog_message {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();

    mcp_tools::register_tool({
        "get_dialog_message",
        "Get the current JavaScript dialog message and type (alert/confirm/prompt) if one is open. Browser must be open and a tab attached.",
        input_schema,
        handle_get_dialog_message
    });
}

} // namespace tool_get_dialog_message
