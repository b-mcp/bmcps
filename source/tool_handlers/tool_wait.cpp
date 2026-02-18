#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_wait(const json &arguments) {
    json result;

    if (!arguments.contains("seconds") || !arguments["seconds"].is_number()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "wait requires a number 'seconds' (e.g. 1.5).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    double seconds = arguments["seconds"].get<double>();

    debug_log::log("wait invoked seconds=" + std::to_string(seconds));
    browser_driver::DriverResult wait_result = cdp_driver::wait_seconds(seconds);

    if (!wait_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "wait failed: " + wait_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = wait_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_wait {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"seconds", {{"type", "number"}, {"description", "Seconds to sleep (e.g. 1 or 1.5)."}}}
    };
    input_schema["required"] = json::array({"seconds"});

    mcp_tools::register_tool({
        "wait",
        "Sleep for a given number of seconds. No browser required.",
        input_schema,
        handle_wait
    });
}

} // namespace tool_wait
