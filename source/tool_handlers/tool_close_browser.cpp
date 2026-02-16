#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_close_browser(const json &arguments) {
    (void)arguments;

    debug_log::log("close_browser invoked");
    cdp_driver::disconnect();

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = "Browser closed.";

    json result;
    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_close_browser {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();

    mcp_tools::register_tool({
        "close_browser",
        "Close the browser and disconnect from CDP. The browser process is terminated. "
        "Call open_browser again to start a fresh browser.",
        input_schema,
        handle_close_browser
    });
}

} // namespace tool_close_browser
