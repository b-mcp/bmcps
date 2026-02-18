#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_get_storage(const json &arguments) {
    json result;

    if (!arguments.contains("storage_type") || !arguments["storage_type"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "get_storage requires string storage_type (localStorage or sessionStorage).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string storage_type = arguments["storage_type"].get<std::string>();
    std::string key;
    if (arguments.contains("key") && arguments["key"].is_string()) {
        key = arguments["key"].get<std::string>();
    }

    debug_log::log("get_storage invoked");
    browser_driver::GetPageSourceResult storage_result = cdp_driver::get_storage(storage_type, key);

    if (!storage_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "get_storage failed: " + storage_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = storage_result.html;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_get_storage {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"storage_type", {{"type", "string"}, {"description", "localStorage or sessionStorage."}}},
        {"key", {{"type", "string"}, {"description", "Optional key; if omitted return all."}}}
    };
    input_schema["required"] = json::array({"storage_type"});

    mcp_tools::register_tool({
        "get_storage",
        "Get localStorage or sessionStorage. Optional key. Browser must be open and a tab attached.",
        input_schema,
        handle_get_storage
    });
}

} // namespace tool_get_storage
