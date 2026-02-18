#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_set_storage(const json &arguments) {
    json result;

    if (!arguments.contains("storage_type") || !arguments["storage_type"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "set_storage requires string storage_type.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }
    if (!arguments.contains("key") || !arguments["key"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "set_storage requires string key.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }
    if (!arguments.contains("value") || !arguments["value"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "set_storage requires string value.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string storage_type = arguments["storage_type"].get<std::string>();
    std::string key = arguments["key"].get<std::string>();
    std::string value = arguments["value"].get<std::string>();

    debug_log::log("set_storage invoked");
    browser_driver::DriverResult set_result = cdp_driver::set_storage(storage_type, key, value);

    if (!set_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "set_storage failed: " + set_result.error_detail;

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

namespace tool_set_storage {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"storage_type", {{"type", "string"}, {"description", "localStorage or sessionStorage"}}},
        {"key", {{"type", "string"}, {"description", "Key"}}},
        {"value", {{"type", "string"}, {"description", "Value"}}}
    };
    input_schema["required"] = json::array({"storage_type", "key", "value"});

    mcp_tools::register_tool({
        "set_storage",
        "Set localStorage or sessionStorage item",
        input_schema,
        handle_set_storage
    });
}

} // namespace tool_set_storage
