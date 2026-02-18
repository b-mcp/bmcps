#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_set_clipboard(const json &arguments) {
    json result;

    if (!arguments.contains("text") || !arguments["text"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "set_clipboard requires a string text.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string text = arguments["text"].get<std::string>();

    debug_log::log("set_clipboard invoked");
    browser_driver::DriverResult set_result = cdp_driver::set_clipboard(text);

    if (!set_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "set_clipboard failed: " + set_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = set_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_set_clipboard {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"text", {{"type", "string"}, {"description", "Text to write."}}}
    };
    input_schema["required"] = json::array({"text"});

    mcp_tools::register_tool({
        "set_clipboard",
        "Write text to page clipboard. Browser must be open.",
        input_schema,
        handle_set_clipboard
    });
}

} // namespace tool_set_clipboard
