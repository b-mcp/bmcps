#ifndef BMCPS_JSON_RPC_HPP
#define BMCPS_JSON_RPC_HPP

// JSON-RPC 2.0 helpers for MCP protocol communication.
// Uses nlohmann/json for parsing and serialization.

#include <nlohmann/json.hpp>
#include <string>
#include <optional>

namespace json_rpc {

using json = nlohmann::json;

// Build a JSON-RPC 2.0 success response.
json build_response(const json &request_id, const json &result_payload);

// Build a JSON-RPC 2.0 error response.
json build_error_response(const json &request_id, int error_code, const std::string &error_message);

// Build a JSON-RPC 2.0 error response with additional data.
json build_error_response(const json &request_id, int error_code, const std::string &error_message, const json &error_data);

// Standard JSON-RPC error codes.
constexpr int PARSE_ERROR = -32700;
constexpr int INVALID_REQUEST = -32600;
constexpr int METHOD_NOT_FOUND = -32601;
constexpr int INVALID_PARAMS = -32602;
constexpr int INTERNAL_ERROR = -32603;

// Extract method name from a JSON-RPC request/notification. Returns empty if missing.
std::string get_method(const json &message);

// Extract the id from a JSON-RPC message. Returns nullptr json if missing (notification).
json get_id(const json &message);

// Extract params from a JSON-RPC message. Returns empty object if missing.
json get_params(const json &message);

// Check if a message is a notification (no id field).
bool is_notification(const json &message);

} // namespace json_rpc

#endif // BMCPS_JSON_RPC_HPP
