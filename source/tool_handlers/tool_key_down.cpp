#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_key_down(const json &arguments) {
    json result;

    if (!arguments.contains("key") || !arguments["key"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "key_down requires a string 'key' (e.g. Control, Shift).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string key = arguments["key"].get<std::string>();

    debug_log::log("key_down invoked key=" + key);
    browser_driver::DriverResult key_result = cdp_driver::key_down(key);

    if (!key_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "key_down failed: " + key_result.error_detail;

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

namespace tool_key_down {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"key", {{"type", "string"}, {"description", "Key name (e.g. Control, Shift)."}}}
    };
    input_schema["required"] = json::array({"key"});

    mcp_tools::register_tool({
        "key_down",
        "Send keyDown for a key (e.g. modifier). Browser must be open and a tab attached.",
        input_schema,
        handle_key_down
    });
}

} // namespace tool_key_down
