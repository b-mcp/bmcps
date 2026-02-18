#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_get_network_requests(const json &arguments) {
    (void)arguments;
    json result;

    debug_log::log("get_network_requests invoked");
    browser_driver::GetNetworkRequestsResult requests_result = cdp_driver::get_network_requests();

    if (!requests_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "get_network_requests failed: " + requests_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json request_list = json::array();
    for (const auto &entry : requests_result.requests) {
        json item;
        item["request_id"] = entry.request_id;
        item["url"] = entry.url;
        item["method"] = entry.method;
        item["status_code"] = entry.status_code;
        item["status_text"] = entry.status_text;
        request_list.push_back(item);
    }
    json text_content;
    text_content["type"] = "text";
    text_content["text"] = request_list.dump();

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_get_network_requests {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();

    mcp_tools::register_tool({
        "get_network_requests",
        "Get list of network requests",
        input_schema,
        handle_get_network_requests
    });
}

} // namespace tool_get_network_requests
