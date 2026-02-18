#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_set_cookie(const json &arguments) {
    json result;

    if (!arguments.contains("name") || !arguments["name"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "set_cookie requires string name.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }
    if (!arguments.contains("value") || !arguments["value"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "set_cookie requires string value.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string name = arguments["name"].get<std::string>();
    std::string value = arguments["value"].get<std::string>();
    std::string url, domain, path;
    if (arguments.contains("url") && arguments["url"].is_string()) {
        url = arguments["url"].get<std::string>();
    }
    if (arguments.contains("domain") && arguments["domain"].is_string()) {
        domain = arguments["domain"].get<std::string>();
    }
    if (arguments.contains("path") && arguments["path"].is_string()) {
        path = arguments["path"].get<std::string>();
    }

    debug_log::log("set_cookie invoked");
    browser_driver::DriverResult set_result = cdp_driver::set_cookie(name, value, url, domain, path);

    if (!set_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "set_cookie failed: " + set_result.error_detail;

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

namespace tool_set_cookie {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"name", {{"type", "string"}, {"description", "Cookie name."}}},
        {"value", {{"type", "string"}, {"description", "Cookie value."}}},
        {"url", {{"type", "string"}, {"description", "Optional URL."}}},
        {"domain", {{"type", "string"}, {"description", "Optional domain."}}},
        {"path", {{"type", "string"}, {"description", "Optional path."}}}
    };
    input_schema["required"] = json::array({"name", "value"});

    mcp_tools::register_tool({
        "set_cookie",
        "Set a cookie. Browser must be open.",
        input_schema,
        handle_set_cookie
    });
}

} // namespace tool_set_cookie
