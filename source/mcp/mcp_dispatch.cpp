#include <nlohmann/json.hpp>
#include <string>

#include "protocol/json_rpc.hpp"
#include "mcp/mcp_tools.hpp"

// MCP JSON-RPC method dispatch.
// Routes incoming MCP messages to the appropriate handler.

namespace mcp_dispatch {

using json = nlohmann::json;

// Protocol version we support.
static const std::string PROTOCOL_VERSION = "2024-11-05";

// Server info.
static const std::string SERVER_NAME = "bmcps";
static const std::string SERVER_VERSION = "0.1.0";
// Description so that MCP clients (e.g. Cursor) can discover this server is for
// browser automation and suggest it when the user needs browser-related actions.
static const std::string SERVER_DESCRIPTION =
    "Browser MCP server: controls a real browser (Chrome). Use this server when "
    "you need to open URLs, navigate, fill forms, click elements, capture "
    "screenshots, manage tabs, read console logs, or perform any browser-based "
    "automation. Tools include open_browser, navigate, list_tabs, fill_field, "
    "click_element, capture_screenshot, get_console_messages, and more.";

// Handle the "initialize" request.
static json handle_initialize(const json &request_id, const json &params) {
    (void)params; // We accept any client capabilities for now.

    json capabilities;
    capabilities["tools"] = json::object(); // We expose tools.

    json server_info;
    server_info["name"] = SERVER_NAME;
    server_info["version"] = SERVER_VERSION;
    server_info["description"] = SERVER_DESCRIPTION;

    json result;
    result["protocolVersion"] = PROTOCOL_VERSION;
    result["capabilities"] = capabilities;
    result["serverInfo"] = server_info;

    return json_rpc::build_response(request_id, result);
}

// Handle the "tools/list" request.
static json handle_tools_list(const json &request_id, const json &params) {
    (void)params;
    json result = mcp_tools::build_tools_list_response();
    return json_rpc::build_response(request_id, result);
}

// Handle the "tools/call" request.
static json handle_tools_call(const json &request_id, const json &params) {
    std::string tool_name;
    if (params.contains("name") && params["name"].is_string()) {
        tool_name = params["name"].get<std::string>();
    } else {
        return json_rpc::build_error_response(request_id, json_rpc::INVALID_PARAMS,
                                               "Missing or invalid 'name' in tools/call");
    }

    json arguments = json::object();
    if (params.contains("arguments") && params["arguments"].is_object()) {
        arguments = params["arguments"];
    }

    json tool_result = mcp_tools::dispatch_tool_call(tool_name, arguments);
    return json_rpc::build_response(request_id, tool_result);
}

// Dispatch a single JSON-RPC message. Returns the response JSON, or a null
// json value for notifications (which require no response).
json dispatch_message(const json &message) {
    std::string method = json_rpc::get_method(message);
    json request_id = json_rpc::get_id(message);
    json params = json_rpc::get_params(message);

    // Handle notifications (no response expected).
    if (json_rpc::is_notification(message)) {
        // "notifications/initialized" is the only notification we expect; acknowledge silently.
        return nullptr;
    }

    // Route to the appropriate handler.
    if (method == "initialize") {
        return handle_initialize(request_id, params);
    }
    if (method == "tools/list") {
        return handle_tools_list(request_id, params);
    }
    if (method == "tools/call") {
        return handle_tools_call(request_id, params);
    }

    // Unknown method.
    return json_rpc::build_error_response(request_id, json_rpc::METHOD_NOT_FOUND,
                                           "Unknown method: " + method);
}

} // namespace mcp_dispatch
