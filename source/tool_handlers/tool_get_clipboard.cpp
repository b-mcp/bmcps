#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_get_clipboard(const json &arguments) {
    (void)arguments;
    json result;

    debug_log::log("get_clipboard invoked");
    browser_driver::GetPageSourceResult clipboard_result = cdp_driver::get_clipboard();

    if (!clipboard_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "get_clipboard failed: " + clipboard_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = clipboard_result.html;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_get_clipboard {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {};

    mcp_tools::register_tool({
        "get_clipboard",
        "Read clipboard text from the page. May require user gesture in some contexts. Browser must be open and a tab attached.",
        input_schema,
        handle_get_clipboard
    });
}

} // namespace tool_get_clipboard
