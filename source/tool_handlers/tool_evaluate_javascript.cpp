#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_evaluate_javascript(const json &arguments) {
    json result;

    if (!arguments.contains("script") || !arguments["script"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "evaluate_javascript requires a string 'script' (JavaScript to run in the page).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string script = arguments["script"].get<std::string>();
    int timeout_milliseconds = 10000;
    if (arguments.contains("timeout_milliseconds") && arguments["timeout_milliseconds"].is_number_integer()) {
        timeout_milliseconds = arguments["timeout_milliseconds"].get<int>();
    }

    debug_log::log("evaluate_javascript invoked");
    browser_driver::EvaluateJavaScriptResult eval_result = cdp_driver::evaluate_javascript(script, timeout_milliseconds);

    if (!eval_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "evaluate_javascript failed: " + eval_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = eval_result.result_json_string;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_evaluate_javascript {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"script", {{"type", "string"}, {"description", "JavaScript code to execute in the page. Return value is serialized and returned."}}},
        {"timeout_milliseconds", {{"type", "integer"}, {"description", "Optional timeout in milliseconds (default 10000)."}}}
    };
    input_schema["required"] = json::array({"script"});

    mcp_tools::register_tool({
        "evaluate_javascript",
        "Execute JavaScript in the current page and return the result as JSON. Use for custom DOM queries, canvas, or any in-page logic. Browser must be open and a tab attached.",
        input_schema,
        handle_evaluate_javascript
    });
}

} // namespace tool_evaluate_javascript
