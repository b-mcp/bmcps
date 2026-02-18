#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_get_page_source(const json &arguments) {
    (void)arguments;
    json result;

    debug_log::log("get_page_source invoked");
    browser_driver::GetPageSourceResult source_result = cdp_driver::get_page_source();

    if (!source_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "get_page_source failed: " + source_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = source_result.html;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_get_page_source {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();

    mcp_tools::register_tool({
        "get_page_source",
        "Get the full HTML source of the current page (document.documentElement.outerHTML). Browser must be open and a tab attached.",
        input_schema,
        handle_get_page_source
    });
}

} // namespace tool_get_page_source
