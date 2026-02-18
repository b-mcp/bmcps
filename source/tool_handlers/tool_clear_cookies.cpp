#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_clear_cookies(const json &arguments) {
    (void)arguments;
    json result;

    debug_log::log("clear_cookies invoked");
    browser_driver::DriverResult clear_result = cdp_driver::clear_cookies();

    if (!clear_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "clear_cookies failed: " + clear_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = clear_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_clear_cookies {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {};

    mcp_tools::register_tool({
        "clear_cookies",
        "Clear all browser cookies. Browser must be open.",
        input_schema,
        handle_clear_cookies
    });
}

} // namespace tool_clear_cookies
