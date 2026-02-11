#include "protocol/json_rpc.hpp"

namespace json_rpc {

json build_response(const json &request_id, const json &result_payload) {
    json response;
    response["jsonrpc"] = "2.0";
    response["id"] = request_id;
    response["result"] = result_payload;
    return response;
}

json build_error_response(const json &request_id, int error_code, const std::string &error_message) {
    json response;
    response["jsonrpc"] = "2.0";
    response["id"] = request_id;
    response["error"]["code"] = error_code;
    response["error"]["message"] = error_message;
    return response;
}

json build_error_response(const json &request_id, int error_code, const std::string &error_message, const json &error_data) {
    json response;
    response["jsonrpc"] = "2.0";
    response["id"] = request_id;
    response["error"]["code"] = error_code;
    response["error"]["message"] = error_message;
    response["error"]["data"] = error_data;
    return response;
}

std::string get_method(const json &message) {
    if (message.contains("method") && message["method"].is_string()) {
        return message["method"].get<std::string>();
    }
    return "";
}

json get_id(const json &message) {
    if (message.contains("id")) {
        return message["id"];
    }
    return nullptr;
}

json get_params(const json &message) {
    if (message.contains("params") && message["params"].is_object()) {
        return message["params"];
    }
    return json::object();
}

bool is_notification(const json &message) {
    return !message.contains("id");
}

} // namespace json_rpc
