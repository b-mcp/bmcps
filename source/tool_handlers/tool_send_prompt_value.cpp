#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_send_prompt_value(const json &arguments) {
    json result;

    if (!arguments.contains("text") || !arguments["text"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "send_prompt_value requires a string text (value for prompt dialog).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string text = arguments["text"].get<std::string>();

    debug_log::log("send_prompt_value invoked");
    browser_driver::DriverResult send_result = cdp_driver::send_prompt_value(text);

    if (!send_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "send_prompt_value failed: " + send_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = send_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_send_prompt_value {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"text", {{"type", "string"}, {"description", "Text to send to the prompt dialog."}}}
    };
    input_schema["required"] = json::array({"text"});

    mcp_tools::register_tool({
        "send_prompt_value",
        "Send text to the current prompt() dialog and accept it. Browser must be open and a tab attached.",
        input_schema,
        handle_send_prompt_value
    });
}

} // namespace tool_send_prompt_value
