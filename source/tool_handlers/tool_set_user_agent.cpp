#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_set_user_agent(const json &arguments) {
    json result;

    if (!arguments.contains("user_agent_string") || !arguments["user_agent_string"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "set_user_agent requires a string user_agent_string.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string user_agent_string = arguments["user_agent_string"].get<std::string>();

    debug_log::log("set_user_agent invoked");
    browser_driver::DriverResult ua_result = cdp_driver::set_user_agent(user_agent_string);

    if (!ua_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "set_user_agent failed: " + ua_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = ua_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_set_user_agent {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"user_agent_string", {{"type", "string"}, {"description", "User-Agent string."}}}
    };
    input_schema["required"] = json::array({"user_agent_string"});

    mcp_tools::register_tool({
        "set_user_agent",
        "Set User-Agent override. Browser must be open.",
        input_schema,
        handle_set_user_agent
    });
}

} // namespace tool_set_user_agent
