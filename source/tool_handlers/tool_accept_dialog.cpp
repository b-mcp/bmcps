#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_accept_dialog(const json &arguments) {
    (void)arguments;
    json result;

    debug_log::log("accept_dialog invoked");
    browser_driver::DriverResult accept_result = cdp_driver::accept_dialog();

    if (!accept_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "accept_dialog failed: " + accept_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = accept_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_accept_dialog {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();

    mcp_tools::register_tool({
        "accept_dialog",
        "Accept the current JavaScript dialog. Browser must be open and a tab attached.",
        input_schema,
        handle_accept_dialog
    });
}

} // namespace tool_accept_dialog
