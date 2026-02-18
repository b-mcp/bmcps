#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_dismiss_dialog(const json &arguments) {
    (void)arguments;
    json result;

    debug_log::log("dismiss_dialog invoked");
    browser_driver::DriverResult dismiss_result = cdp_driver::dismiss_dialog();

    if (!dismiss_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "dismiss_dialog failed: " + dismiss_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = dismiss_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_dismiss_dialog {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {};

    mcp_tools::register_tool({
        "dismiss_dialog",
        "Dismiss the current JavaScript dialog (cancel). Browser must be open and a tab attached.",
        input_schema,
        handle_dismiss_dialog
    });
}

} // namespace tool_dismiss_dialog
