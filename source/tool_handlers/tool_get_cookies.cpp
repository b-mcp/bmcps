#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_get_cookies(const json &arguments) {
    json result;

    std::string url;
    if (arguments.contains("url") && arguments["url"].is_string()) {
        url = arguments["url"].get<std::string>();
    }

    debug_log::log("get_cookies invoked");
    browser_driver::GetCookiesResult cookies_result = cdp_driver::get_cookies(url);

    if (!cookies_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "get_cookies failed: " + cookies_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json cookie_list = json::array();
    for (const auto &entry : cookies_result.cookies) {
        json item;
        item["name"] = entry.name;
        item["value"] = entry.value;
        item["domain"] = entry.domain;
        item["path"] = entry.path;
        cookie_list.push_back(item);
    }
    json text_content;
    text_content["type"] = "text";
    text_content["text"] = cookie_list.dump();

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_get_cookies {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"url", {{"type", "string"}, {"description", "Optional URL to filter cookies."}}}
    };

    mcp_tools::register_tool({
        "get_cookies",
        "Get browser cookies. Optional url to filter. Browser must be open.",
        input_schema,
        handle_get_cookies
    });
}

} // namespace tool_get_cookies
