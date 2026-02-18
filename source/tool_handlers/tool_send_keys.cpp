#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_send_keys(const json &arguments) {
    json result;

    if (!arguments.contains("keys") || !arguments["keys"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "send_keys requires a string keys. Use {Enter}, {Tab}, {Escape} for special keys.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string keys = arguments["keys"].get<std::string>();
    std::string selector;
    if (arguments.contains("selector") && arguments["selector"].is_string()) {
        selector = arguments["selector"].get<std::string>();
    }

    debug_log::log("send_keys invoked");
    browser_driver::DriverResult key_result = cdp_driver::send_keys(keys, selector);

    if (!key_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "send_keys failed: " + key_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = key_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_send_keys {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"keys", {{"type", "string"}, {"description", "Keys to send. Literal or {Enter}, {Tab}, {Escape}."}}},
        {"selector", {{"type", "string"}, {"description", "Optional. Focus this element first."}}}
    };
    input_schema["required"] = json::array({"keys"});

    mcp_tools::register_tool({
        "send_keys",
        "Send keyboard input. Optional selector to focus first. Special keys: {Enter}, {Tab}, {Escape}. Browser must be open and a tab attached.",
        input_schema,
        handle_send_keys
    });
}

} // namespace tool_send_keys
