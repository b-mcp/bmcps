#include "browser/cdp/cdp_driver.hpp"
#include "browser/cdp/cdp_chrome_launch.hpp"
#include "platform/platform_abi.hpp"
#include "utils/debug_log.hpp"
#include "utils/utf8_sanitize.hpp"

#include <libwebsockets.h>
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <algorithm>

namespace cdp_driver {

// Module-level connection state (not a class instance; global singleton).
static ConnectionState global_state;

// Forward declaration of the WebSocket callback.
static int websocket_callback(struct lws *websocket_instance, enum lws_callback_reasons reason,
                               void *user_data, void *incoming_data, size_t incoming_length);

// WebSocket protocol definition for libwebsockets.
static const struct lws_protocols websocket_protocols[] = {
    {
        "cdp-protocol",
        websocket_callback,
        0,    // per-session data size
        65536 // rx buffer size
    },
    {nullptr, nullptr, 0, 0} // sentinel
};

// --- WebSocket callback ---

static int websocket_callback(struct lws *websocket_instance, enum lws_callback_reasons reason,
                               void *user_data, void *incoming_data, size_t incoming_length) {
    (void)user_data;

    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        global_state.connected = true;
        debug_log::log("CDP WebSocket connected.");
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE: {
        // Accumulate incoming data.
        const char *data_pointer = static_cast<const char *>(incoming_data);
        global_state.receive_buffer.append(data_pointer, incoming_length);

        // Check if the full message has been received.
        if (lws_is_final_fragment(websocket_instance)) {
            // Parse the complete JSON message.
            try {
                json message = json::parse(global_state.receive_buffer);
                global_state.receive_buffer.clear();

                // Check if this is a response (has "id") or an event (no "id").
                if (message.contains("id") && !message["id"].is_null()) {
                    int message_id = message["id"].get<int>();
                    std::lock_guard<std::mutex> lock(global_state.pending_mutex);
                    global_state.pending_responses[message_id] = message;
                    global_state.pending_condition.notify_all();
                } else {
                    // CDP event (method without id).
                    if (message.contains("method")) {
                        std::string method = message["method"].get<std::string>();
                        if (method == "Runtime.consoleAPICalled") {
                            std::string event_session_id;
                            if (message.contains("sessionId") && message["sessionId"].is_string()) {
                                event_session_id = message["sessionId"].get<std::string>();
                            }
                            if ((event_session_id.empty() || event_session_id == global_state.current_session_id) &&
                                message.contains("params")) {
                                const json &params = message["params"];
                                std::string level = "info";
                                if (params.contains("type") && params["type"].is_string()) {
                                    level = params["type"].get<std::string>();
                                }
                                std::string text_parts;
                                if (params.contains("args") && params["args"].is_array()) {
                                    for (const auto &arg : params["args"]) {
                                        std::string piece;
                                        if (arg.contains("value")) {
                                            const auto &value = arg["value"];
                                            if (value.is_string()) {
                                                piece = value.get<std::string>();
                                            } else if (!value.is_null()) {
                                                piece = value.dump();
                                            }
                                        } else if (arg.contains("description") && arg["description"].is_string()) {
                                            piece = arg["description"].get<std::string>();
                                        }
                                        if (!piece.empty()) {
                                            if (!text_parts.empty()) {
                                                text_parts += " ";
                                            }
                                            text_parts += piece;
                                        }
                                    }
                                }
                                utf8_sanitize::sanitize(text_parts);
                                int64_t timestamp_ms = static_cast<int64_t>(
                                    std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch()).count());
                                browser_driver::ConsoleEntry entry;
                                entry.timestamp_ms = timestamp_ms;
                                entry.level = level;
                                entry.text = std::move(text_parts);
                                {
                                    std::lock_guard<std::mutex> lock(global_state.console_mutex);
                                    global_state.console_entries.push_back(std::move(entry));
                                    while (global_state.console_entries.size() > ConnectionState::kConsoleEntriesMax) {
                                        global_state.console_entries.erase(global_state.console_entries.begin());
                                    }
                                }
                            }
                        } else if (method == "Page.javascriptDialogOpening") {
                            if (message.contains("params")) {
                                const json &params = message["params"];
                                std::lock_guard<std::mutex> lock(global_state.dialog_mutex);
                                global_state.last_dialog_message.clear();
                                global_state.last_dialog_type.clear();
                                if (params.contains("message") && params["message"].is_string()) {
                                    global_state.last_dialog_message = params["message"].get<std::string>();
                                }
                                if (params.contains("type") && params["type"].is_string()) {
                                    global_state.last_dialog_type = params["type"].get<std::string>();
                                }
                            }
                        } else if (method == "Runtime.executionContextCreated") {
                            if (message.contains("params") &&
                                message["params"].contains("context")) {
                                const json &ctx = message["params"]["context"];
                                int context_id = 0;
                                std::string frame_id;
                                if (ctx.contains("id") && ctx["id"].is_number()) {
                                    context_id = ctx["id"].get<int>();
                                }
                                if (ctx.contains("auxData") && ctx["auxData"].is_object() &&
                                    ctx["auxData"].contains("frameId") && ctx["auxData"]["frameId"].is_string()) {
                                    frame_id = ctx["auxData"]["frameId"].get<std::string>();
                                }
                                if (!frame_id.empty() && context_id != 0) {
                                    std::lock_guard<std::mutex> lock(global_state.frame_mutex);
                                    global_state.execution_context_id_by_frame_id[frame_id] = context_id;
                                }
                            }
                        } else if (method == "Network.requestWillBeSent") {
                            if (message.contains("params")) {
                                const json &params = message["params"];
                                std::string request_id;
                                std::string url;
                                std::string method_str = "GET";
                                if (params.contains("requestId") && params["requestId"].is_string()) {
                                    request_id = params["requestId"].get<std::string>();
                                }
                                if (params.contains("request")) {
                                    const json &req = params["request"];
                                    if (req.contains("url") && req["url"].is_string()) {
                                        url = req["url"].get<std::string>();
                                    }
                                    if (req.contains("method") && req["method"].is_string()) {
                                        method_str = req["method"].get<std::string>();
                                    }
                                }
                                browser_driver::NetworkRequestEntry entry;
                                entry.request_id = request_id;
                                entry.url = url;
                                entry.method = method_str;
                                entry.status_code = 0;
                                std::lock_guard<std::mutex> lock(global_state.network_mutex);
                                global_state.network_requests.push_back(entry);
                                while (global_state.network_requests.size() > ConnectionState::kNetworkRequestsMax) {
                                    global_state.network_requests.erase(global_state.network_requests.begin());
                                }
                            }
                        } else if (method == "Network.responseReceived") {
                            if (message.contains("params") &&
                                message["params"].contains("requestId") &&
                                message["params"].contains("response")) {
                                std::string request_id = message["params"]["requestId"].get<std::string>();
                                int status = 0;
                                std::string status_text;
                                if (message["params"]["response"].contains("status")) {
                                    status = message["params"]["response"]["status"].get<int>();
                                }
                                if (message["params"]["response"].contains("statusText") &&
                                    message["params"]["response"]["statusText"].is_string()) {
                                    status_text = message["params"]["response"]["statusText"].get<std::string>();
                                }
                                std::lock_guard<std::mutex> lock(global_state.network_mutex);
                                for (auto &entry : global_state.network_requests) {
                                    if (entry.request_id == request_id) {
                                        entry.status_code = status;
                                        entry.status_text = status_text;
                                        break;
                                    }
                                }
                            }
                        } else {
                            std::cerr << "[bmcps] CDP event: " << method << std::endl;
                        }
                    }
                }
            } catch (const json::parse_error &parse_error) {
                std::cerr << "[bmcps] Failed to parse CDP message: " << parse_error.what()
                          << ", buffer content: " << global_state.receive_buffer.substr(0, 200) << std::endl;
                global_state.receive_buffer.clear();
            }
        }
        break;
    }

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
        const char *error_message = incoming_data ? static_cast<const char *>(incoming_data) : "unknown";
        std::cerr << "[bmcps] CDP WebSocket connection error: " << error_message << std::endl;
        debug_log::log("CDP WebSocket connection error (LWS): " + std::string(error_message));
        global_state.connected = false;
        global_state.connection_failed = true;
        break;
    }

    case LWS_CALLBACK_CLIENT_CLOSED:
        std::cerr << "[bmcps] CDP WebSocket closed." << std::endl;
        global_state.connected = false;
        break;

    case LWS_CALLBACK_CLIENT_WRITEABLE: {
        // Nothing to write proactively from the callback; writes happen in send_command.
        break;
    }

    default:
        break;
    }

    return 0;
}

// --- Public functions ---

void initialize() {
    // Reset scalar fields individually; mutex and condition_variable are not assignable.
    global_state.connected = false;
    global_state.shutting_down = false;
    global_state.websocket_context = nullptr;
    global_state.websocket_connection = nullptr;
    global_state.chrome_process_id = -1;
    global_state.user_data_directory.clear();
    global_state.next_message_id = 1;
    global_state.current_target_id.clear();
    global_state.current_session_id.clear();
    global_state.last_dialog_message.clear();
    global_state.last_dialog_type.clear();
    global_state.execution_context_id_by_frame_id.clear();
    global_state.current_execution_context_id = 0;
    global_state.network_requests.clear();
    global_state.network_enabled = false;
    {
        std::lock_guard<std::mutex> lock(global_state.pending_mutex);
        global_state.pending_responses.clear();
    }
    global_state.receive_buffer.clear();
}

bool connect(const std::string &websocket_url) {
    std::cerr << "[bmcps] Connecting to CDP WebSocket: " << websocket_url << std::endl;
    debug_log::log("connect() URL=" + websocket_url);

    std::string url_without_scheme = websocket_url;
    if (url_without_scheme.substr(0, 5) == "ws://") {
        url_without_scheme = url_without_scheme.substr(5);
    }

    // Split host:port from path.
    std::string host_and_port;
    std::string path = "/";
    auto slash_position = url_without_scheme.find('/');
    if (slash_position != std::string::npos) {
        host_and_port = url_without_scheme.substr(0, slash_position);
        path = url_without_scheme.substr(slash_position);
    } else {
        host_and_port = url_without_scheme;
    }

    // Split host from port.
    std::string host = "127.0.0.1";
    int port = 9222;
    auto colon_position = host_and_port.find(':');
    if (colon_position != std::string::npos) {
        host = host_and_port.substr(0, colon_position);
        try {
            port = std::stoi(host_and_port.substr(colon_position + 1));
        } catch (...) {
            std::cerr << "[bmcps] Failed to parse port from WebSocket URL." << std::endl;
            return false;
        }
    }

    // Create libwebsockets context.
    struct lws_context_creation_info context_info;
    memset(&context_info, 0, sizeof(context_info));
    context_info.port = CONTEXT_PORT_NO_LISTEN; // Client mode, no listening.
    context_info.protocols = websocket_protocols;
    context_info.gid = -1;
    context_info.uid = -1;

    global_state.websocket_context = lws_create_context(&context_info);
    if (global_state.websocket_context == nullptr) {
        std::cerr << "[bmcps] Failed to create libwebsockets context." << std::endl;
        return false;
    }

    // Connect to the CDP WebSocket.
    struct lws_client_connect_info connect_info;
    memset(&connect_info, 0, sizeof(connect_info));
    connect_info.context = global_state.websocket_context;
    connect_info.address = host.c_str();
    connect_info.port = port;
    connect_info.path = path.c_str();
    connect_info.host = host.c_str();
    connect_info.origin = nullptr;
    connect_info.protocol = nullptr;

    debug_log::log("connect() host=" + host + " port=" + std::to_string(port) + " path=" + path + " (no subprotocol)");
    global_state.connection_failed = false;
    global_state.websocket_connection = lws_client_connect_via_info(&connect_info);
    if (global_state.websocket_connection == nullptr) {
        std::cerr << "[bmcps] Failed to initiate CDP WebSocket connection (lws_client_connect_via_info returned null)." << std::endl;
        debug_log::log("connect(): lws_client_connect_via_info returned null.");
        lws_context_destroy(global_state.websocket_context);
        global_state.websocket_context = nullptr;
        return false;
    }

    auto start_time = std::chrono::steady_clock::now();
    int connection_timeout_milliseconds = 20000;

    while (!global_state.connected) {
        lws_service(global_state.websocket_context, 50);

        if (global_state.connection_failed) {
            std::cerr << "[bmcps] CDP WebSocket connection failed (see error above)." << std::endl;
            debug_log::log("connect(): connection_failed was set by LWS callback.");
            lws_context_destroy(global_state.websocket_context);
            global_state.websocket_context = nullptr;
            return false;
        }

        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > connection_timeout_milliseconds) {
            std::cerr << "[bmcps] Timed out connecting to CDP WebSocket (after " << (connection_timeout_milliseconds / 1000) << " s)." << std::endl;
            debug_log::log("connect(): timed out after " + std::to_string(connection_timeout_milliseconds) + " ms.");
            lws_context_destroy(global_state.websocket_context);
            global_state.websocket_context = nullptr;
            return false;
        }
    }

    return true;
}

void disconnect() {
    debug_log::log("disconnect() called. shutting_down=true, will destroy WebSocket and kill Chrome if we launched it.");
    global_state.shutting_down = true;

    if (global_state.websocket_context != nullptr) {
        lws_context_destroy(global_state.websocket_context);
        global_state.websocket_context = nullptr;
        debug_log::log("disconnect(): WebSocket context destroyed.");
    }

    if (global_state.chrome_process_id > 0) {
        debug_log::log("disconnect(): Killing Chrome process id=" + std::to_string(global_state.chrome_process_id));
        platform::kill_process(global_state.chrome_process_id);
    }
    global_state.chrome_process_id = -1;
    global_state.connected = false;
    debug_log::log("disconnect() finished.");
}

void service_websocket(int timeout_milliseconds) {
    if (global_state.websocket_context != nullptr) {
        lws_service(global_state.websocket_context, timeout_milliseconds);
    }
}

json send_command(const std::string &method, const json &params,
                  const std::string &session_id, int timeout_milliseconds) {
    if (!global_state.connected || global_state.websocket_connection == nullptr) {
        json error_response;
        error_response["error"] = "Not connected to CDP";
        return error_response;
    }

    // Build the CDP command message.
    int message_id = global_state.next_message_id++;
    json command;
    command["id"] = message_id;
    command["method"] = method;
    if (!params.is_null() && !params.empty()) {
        command["params"] = params;
    }
    // Session routing: if session_id is set, include it in the message.
    if (!session_id.empty()) {
        command["sessionId"] = session_id;
    }

    std::string serialized_command = command.dump();

    // libwebsockets requires LWS_PRE bytes of padding before the data.
    std::vector<unsigned char> send_buffer(LWS_PRE + serialized_command.size());
    memcpy(send_buffer.data() + LWS_PRE, serialized_command.c_str(), serialized_command.size());

    int bytes_written = lws_write(global_state.websocket_connection,
                                   send_buffer.data() + LWS_PRE,
                                   serialized_command.size(), LWS_WRITE_TEXT);
    if (bytes_written < 0) {
        json error_response;
        error_response["error"] = "Failed to send CDP command via WebSocket";
        return error_response;
    }

    // Wait for the response with the matching message ID.
    auto start_time = std::chrono::steady_clock::now();
    // Safety limit for iterations to avoid an infinite loop.
    int maximum_iterations = (timeout_milliseconds / 10) * 2 + 100;

    for (int iteration = 0; iteration < maximum_iterations; iteration++) {
        // Service the event loop to receive messages.
        lws_service(global_state.websocket_context, 10);

        {
            std::lock_guard<std::mutex> lock(global_state.pending_mutex);
            auto response_iterator = global_state.pending_responses.find(message_id);
            if (response_iterator != global_state.pending_responses.end()) {
                json response = response_iterator->second;
                global_state.pending_responses.erase(response_iterator);
                return response;
            }
        }

        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout_milliseconds) {
            json error_response;
            error_response["error"] = "Timed out waiting for CDP response to method: " + method;
            error_response["message_id"] = message_id;
            return error_response;
        }
    }

    json error_response;
    error_response["error"] = "Safety iteration limit reached while waiting for CDP response";
    return error_response;
}

// --- High-level browser operations ---

browser_driver::DriverResult open_browser(const browser_driver::OpenBrowserOptions &options) {
    browser_driver::DriverResult result;
    bool connected = false;

    if (!options.disable_translate) {
        std::string existing_url = cdp_chrome_launch::try_get_existing_websocket_url(cdp_chrome_launch::BMCPS_FIXED_USER_DATA_DIR);
        if (!existing_url.empty()) {
            debug_log::log("open_browser: Found existing Chrome, trying to connect to " + existing_url);
            connected = connect(existing_url);
            if (connected) {
                global_state.chrome_process_id = -1;
                global_state.user_data_directory = cdp_chrome_launch::BMCPS_FIXED_USER_DATA_DIR;
            } else {
                debug_log::log("open_browser: Connect to existing Chrome failed, will launch new one.");
            }
        }
    } else {
        debug_log::log("open_browser: disable_translate=true, launching new Chrome so translate bar is off.");
    }

    if (!connected) {
        cdp_chrome_launch::ChromeLaunchResult launch_result = cdp_chrome_launch::launch_chrome(options);
        if (!launch_result.success) {
            result.success = false;
            result.error_detail = launch_result.error_message;
            result.message = "Failed to launch Chrome.";
            return result;
        }
        global_state.chrome_process_id = launch_result.process_id;
        global_state.user_data_directory = launch_result.user_data_directory;

        debug_log::log("Connecting to CDP WebSocket…");
        connected = connect(launch_result.websocket_debugger_url);
        if (!connected) {
            debug_log::log("open_browser: WebSocket connect failed, killing Chrome pid=" + std::to_string(global_state.chrome_process_id));
            result.success = false;
            result.error_detail = "Could not establish WebSocket connection to: " + launch_result.websocket_debugger_url;
            result.message = "Failed to connect to Chrome CDP.";
            platform::kill_process(global_state.chrome_process_id);
            global_state.chrome_process_id = -1;
            return result;
        }
    }

    debug_log::log("open_browser: WebSocket connected successfully.");

    debug_log::log("Discovering targets…");
    // Enable target discovery.
    json discover_params;
    discover_params["discover"] = true;
    json discover_response = send_command("Target.setDiscoverTargets", discover_params);
    if (discover_response.contains("error") && discover_response["error"].is_string()) {
        std::cerr << "[bmcps] Warning: Target.setDiscoverTargets returned: "
                  << discover_response.dump() << std::endl;
    }

    json get_targets_response = send_command("Target.getTargets", json::object());
    int target_count = 0;
    if (get_targets_response.contains("result") && get_targets_response["result"].contains("targetInfos")) {
        target_count = static_cast<int>(get_targets_response["result"]["targetInfos"].size());
    }
    debug_log::log("open_browser: Target.getTargets returned, target count=" + std::to_string(target_count));

    std::string chosen_target_id;

    if (get_targets_response.contains("result") &&
        get_targets_response["result"].contains("targetInfos")) {
        const auto &target_infos = get_targets_response["result"]["targetInfos"];
        for (const auto &target_info : target_infos) {
            if (target_info.contains("type") && target_info["type"] == "page") {
                chosen_target_id = target_info["targetId"].get<std::string>();
                break;
            }
        }
    }

    if (chosen_target_id.empty()) {
        debug_log::log("open_browser: No page target found, creating new target (Target.createTarget).");
    }

    // If no page target exists, create a new one.
    if (chosen_target_id.empty()) {
        json create_params;
        create_params["url"] = "about:blank";
        json create_response = send_command("Target.createTarget", create_params);

        if (create_response.contains("result") &&
            create_response["result"].contains("targetId")) {
            chosen_target_id = create_response["result"]["targetId"].get<std::string>();
            debug_log::log("open_browser: Target.createTarget ok, targetId=" + chosen_target_id);
        } else {
            debug_log::log("open_browser: Target.createTarget failed: " + create_response.dump());
            result.success = false;
            result.error_detail = "Target.createTarget failed: " + create_response.dump();
            result.message = "Failed to create a new tab.";
            return result;
        }
    } else {
        debug_log::log("open_browser: Using existing page targetId=" + chosen_target_id);
    }

    debug_log::log("open_browser: Attaching to target (Target.attachToTarget) targetId=" + chosen_target_id);
    // Attach to the chosen target to get a session ID.
    json attach_params;
    attach_params["targetId"] = chosen_target_id;
    attach_params["flatten"] = true;
    json attach_response = send_command("Target.attachToTarget", attach_params);

    if (attach_response.contains("result") &&
        attach_response["result"].contains("sessionId")) {
        global_state.current_target_id = chosen_target_id;
        global_state.current_session_id = attach_response["result"]["sessionId"].get<std::string>();
        debug_log::log("open_browser: Target.attachToTarget ok, sessionId=" + global_state.current_session_id);
    } else {
        debug_log::log("open_browser: Target.attachToTarget failed: " + attach_response.dump());
        result.success = false;
        result.error_detail = "Target.attachToTarget failed: " + attach_response.dump();
        result.message = "Failed to attach to the browser tab.";
        return result;
    }

    enable_console_for_session();
    result.success = true;
    result.message = "Browser opened and connected to default tab.";
    debug_log::log("Attached to target id=" + global_state.current_target_id + " session=" + global_state.current_session_id);
    return result;
}

browser_driver::TabListResult list_tabs() {
    browser_driver::TabListResult result;

    if (!global_state.connected) {
        result.success = false;
        result.error_detail = "Not connected to a browser. Call open_browser first.";
        return result;
    }

    json get_targets_response = send_command("Target.getTargets", json::object());

    if (!get_targets_response.contains("result") ||
        !get_targets_response["result"].contains("targetInfos")) {
        result.success = false;
        result.error_detail = "Target.getTargets returned unexpected response: " + get_targets_response.dump();
        return result;
    }

    const auto &target_infos = get_targets_response["result"]["targetInfos"];
    std::vector<browser_driver::TabInfo> page_tabs;
    for (const auto &target_info : target_infos) {
        std::string type_str = target_info.contains("type") ? target_info["type"].get<std::string>() : "";
        if (type_str != "page") {
            continue;
        }
        browser_driver::TabInfo tab;
        if (target_info.contains("targetId")) {
            tab.target_id = target_info["targetId"].get<std::string>();
        }
        if (target_info.contains("title")) {
            tab.title = target_info["title"].get<std::string>();
        }
        if (target_info.contains("url")) {
            tab.url = target_info["url"].get<std::string>();
        }
        tab.type = type_str;
        page_tabs.push_back(tab);
    }
    std::sort(page_tabs.begin(), page_tabs.end(),
              [](const browser_driver::TabInfo &a, const browser_driver::TabInfo &b) {
                  return a.target_id < b.target_id;
              });
    result.tabs = page_tabs;

    result.success = true;
    return result;
}

browser_driver::NavigateResult navigate(const std::string &url) {
    browser_driver::NavigateResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_text = "No active browser session. Call open_browser first.";
        return result;
    }

    json navigate_params;
    navigate_params["url"] = url;

    // Send Page.navigate on the current session.
    json navigate_response = send_command("Page.navigate", navigate_params,
                                           global_state.current_session_id);

    if (navigate_response.contains("error") && navigate_response["error"].is_string()) {
        result.success = false;
        result.error_text = navigate_response["error"].get<std::string>();
        return result;
    }

    if (navigate_response.contains("result")) {
        const auto &nav_result = navigate_response["result"];
        if (nav_result.contains("frameId")) {
            result.frame_id = nav_result["frameId"].get<std::string>();
        }
        if (nav_result.contains("errorText")) {
            result.error_text = nav_result["errorText"].get<std::string>();
            result.success = false;
            return result;
        }
    }

    result.success = true;
    if (result.success) {
        std::lock_guard<std::mutex> lock(global_state.console_mutex);
        global_state.console_entries.clear();
    }
    return result;
}

browser_driver::DriverResult navigate_back() {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "Failed to navigate back.";
        return result;
    }

    json history_response = send_command("Page.getNavigationHistory", json::object(),
                                         global_state.current_session_id);
    if (history_response.contains("error") && history_response["error"].is_string()) {
        result.success = false;
        result.error_detail = history_response["error"].get<std::string>();
        result.message = "Failed to get navigation history.";
        return result;
    }
    if (!history_response.contains("result") || !history_response["result"].contains("currentIndex") ||
        !history_response["result"].contains("entries")) {
        result.success = false;
        result.error_detail = "Page.getNavigationHistory returned unexpected response.";
        result.message = "Failed to navigate back.";
        return result;
    }

    int current_index = history_response["result"]["currentIndex"].get<int>();
    const auto &entries = history_response["result"]["entries"];
    if (current_index <= 0 || entries.empty()) {
        result.success = false;
        result.error_detail = "No back history.";
        result.message = "Cannot navigate back.";
        return result;
    }

    int entry_id = entries[current_index - 1]["id"].get<int>();
    json nav_params;
    nav_params["entryId"] = entry_id;
    json nav_response = send_command("Page.navigateToHistoryEntry", nav_params,
                                     global_state.current_session_id);
    if (nav_response.contains("error") && nav_response["error"].is_string()) {
        result.success = false;
        result.error_detail = nav_response["error"].get<std::string>();
        result.message = "Failed to navigate back.";
        return result;
    }

    result.success = true;
    result.message = "Navigated back.";
    {
        std::lock_guard<std::mutex> lock(global_state.console_mutex);
        global_state.console_entries.clear();
    }
    return result;
}

browser_driver::DriverResult navigate_forward() {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "Failed to navigate forward.";
        return result;
    }

    json history_response = send_command("Page.getNavigationHistory", json::object(),
                                         global_state.current_session_id);
    if (history_response.contains("error") && history_response["error"].is_string()) {
        result.success = false;
        result.error_detail = history_response["error"].get<std::string>();
        result.message = "Failed to get navigation history.";
        return result;
    }
    if (!history_response.contains("result") || !history_response["result"].contains("currentIndex") ||
        !history_response["result"].contains("entries")) {
        result.success = false;
        result.error_detail = "Page.getNavigationHistory returned unexpected response.";
        result.message = "Failed to navigate forward.";
        return result;
    }

    int current_index = history_response["result"]["currentIndex"].get<int>();
    const auto &entries = history_response["result"]["entries"];
    int entries_count = static_cast<int>(entries.size());
    if (current_index >= entries_count - 1 || entries.empty()) {
        result.success = false;
        result.error_detail = "No forward history.";
        result.message = "Cannot navigate forward.";
        return result;
    }

    int entry_id = entries[current_index + 1]["id"].get<int>();
    json nav_params;
    nav_params["entryId"] = entry_id;
    json nav_response = send_command("Page.navigateToHistoryEntry", nav_params,
                                     global_state.current_session_id);
    if (nav_response.contains("error") && nav_response["error"].is_string()) {
        result.success = false;
        result.error_detail = nav_response["error"].get<std::string>();
        result.message = "Failed to navigate forward.";
        return result;
    }

    result.success = true;
    result.message = "Navigated forward.";
    {
        std::lock_guard<std::mutex> lock(global_state.console_mutex);
        global_state.console_entries.clear();
    }
    return result;
}

browser_driver::DriverResult refresh() {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "Failed to reload page.";
        return result;
    }

    json reload_response = send_command("Page.reload", json::object(),
                                        global_state.current_session_id);
    if (reload_response.contains("error") && reload_response["error"].is_string()) {
        result.success = false;
        result.error_detail = reload_response["error"].get<std::string>();
        result.message = "Failed to reload page.";
        return result;
    }

    result.success = true;
    result.message = "Page reloaded.";
    {
        std::lock_guard<std::mutex> lock(global_state.console_mutex);
        global_state.console_entries.clear();
    }
    return result;
}

browser_driver::NavigationHistoryResult get_navigation_history() {
    browser_driver::NavigationHistoryResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        return result;
    }

    json history_response = send_command("Page.getNavigationHistory", json::object(),
                                         global_state.current_session_id);
    if (history_response.contains("error") && history_response["error"].is_string()) {
        result.success = false;
        result.error_detail = history_response["error"].get<std::string>();
        return result;
    }
    if (!history_response.contains("result") || !history_response["result"].contains("currentIndex") ||
        !history_response["result"].contains("entries")) {
        result.success = false;
        result.error_detail = "Page.getNavigationHistory returned unexpected response.";
        return result;
    }

    result.current_index = history_response["result"]["currentIndex"].get<int>();
    const auto &entries_json = history_response["result"]["entries"];
    for (const auto &entry : entries_json) {
        browser_driver::NavigationHistoryEntry history_entry;
        if (entry.contains("id")) {
            history_entry.id = entry["id"].get<int>();
        }
        if (entry.contains("url") && entry["url"].is_string()) {
            history_entry.url = entry["url"].get<std::string>();
        }
        if (entry.contains("title") && entry["title"].is_string()) {
            history_entry.title = entry["title"].get<std::string>();
        }
        result.entries.push_back(history_entry);
    }
    result.success = true;
    return result;
}

browser_driver::DriverResult new_tab(const std::string &url) {
    browser_driver::DriverResult result;

    if (!global_state.connected) {
        result.success = false;
        result.error_detail = "Not connected to a browser. Call open_browser first.";
        result.message = "Failed to create new tab.";
        return result;
    }

    json create_params;
    create_params["url"] = url.empty() ? "about:blank" : url;
    json create_response = send_command("Target.createTarget", create_params);

    if (!create_response.contains("result") || !create_response["result"].contains("targetId")) {
        result.success = false;
        result.error_detail = "Target.createTarget failed: " + create_response.dump();
        result.message = "Failed to create new tab.";
        return result;
    }

    std::string target_id = create_response["result"]["targetId"].get<std::string>();
    debug_log::log("new_tab: created targetId=" + target_id);

    json attach_params;
    attach_params["targetId"] = target_id;
    attach_params["flatten"] = true;
    json attach_response = send_command("Target.attachToTarget", attach_params);

    if (!attach_response.contains("result") || !attach_response["result"].contains("sessionId")) {
        result.success = false;
        result.error_detail = "Target.attachToTarget failed: " + attach_response.dump();
        result.message = "Failed to attach to new tab.";
        return result;
    }

    global_state.current_target_id = target_id;
    global_state.current_session_id = attach_response["result"]["sessionId"].get<std::string>();
    enable_console_for_session();
    result.success = true;
    result.message = "New tab opened and attached.";
    debug_log::log("new_tab: attached sessionId=" + global_state.current_session_id);
    return result;
}

browser_driver::DriverResult switch_tab(int index) {
    browser_driver::DriverResult result;

    if (!global_state.connected) {
        result.success = false;
        result.error_detail = "Not connected to a browser. Call open_browser first.";
        result.message = "Failed to switch tab.";
        return result;
    }

    json get_targets_response = send_command("Target.getTargets", json::object());
    if (!get_targets_response.contains("result") || !get_targets_response["result"].contains("targetInfos")) {
        result.success = false;
        result.error_detail = "Target.getTargets failed: " + get_targets_response.dump();
        result.message = "Failed to switch tab.";
        return result;
    }

    std::vector<std::string> page_target_ids;
    for (const auto &target_info : get_targets_response["result"]["targetInfos"]) {
        if (target_info.contains("type") && target_info["type"] == "page") {
            page_target_ids.push_back(target_info["targetId"].get<std::string>());
        }
    }
    std::sort(page_target_ids.begin(), page_target_ids.end());

    if (index < 0 || index >= static_cast<int>(page_target_ids.size())) {
        result.success = false;
        result.error_detail = "Tab index " + std::to_string(index) + " out of range (0.." + std::to_string(page_target_ids.size() - 1) + ").";
        result.message = "Failed to switch tab.";
        return result;
    }

    std::string target_id = page_target_ids[static_cast<size_t>(index)];
    json attach_params;
    attach_params["targetId"] = target_id;
    attach_params["flatten"] = true;
    json attach_response = send_command("Target.attachToTarget", attach_params);

    if (!attach_response.contains("result") || !attach_response["result"].contains("sessionId")) {
        result.success = false;
        result.error_detail = "Target.attachToTarget failed: " + attach_response.dump();
        result.message = "Failed to switch tab.";
        return result;
    }

    global_state.current_target_id = target_id;
    global_state.current_session_id = attach_response["result"]["sessionId"].get<std::string>();
    enable_console_for_session();

    json activate_params;
    activate_params["targetId"] = target_id;
    json activate_response = send_command("Target.activateTarget", activate_params, "");
    if (activate_response.contains("error") && activate_response["error"].is_string()) {
        debug_log::log("switch_tab: Target.activateTarget failed: " + activate_response["error"].get<std::string>());
    }

    result.success = true;
    result.message = "Switched to tab " + std::to_string(index) + ".";
    debug_log::log("switch_tab: attached and activated targetId=" + target_id);
    return result;
}

browser_driver::DriverResult close_tab() {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_target_id.empty()) {
        result.success = false;
        result.error_detail = "No current tab. Call open_browser and ensure a tab is selected.";
        result.message = "Failed to close tab.";
        return result;
    }

    std::string tab_to_close = global_state.current_target_id;
    json close_params;
    close_params["targetId"] = tab_to_close;
    json close_response = send_command("Target.closeTarget", close_params);

    if (close_response.contains("error") && close_response["error"].is_string()) {
        result.success = false;
        result.error_detail = close_response["error"].get<std::string>();
        result.message = "Failed to close tab.";
        return result;
    }

    global_state.current_target_id.clear();
    global_state.current_session_id.clear();

    json get_targets_response = send_command("Target.getTargets", json::object());
    if (get_targets_response.contains("result") && get_targets_response["result"].contains("targetInfos")) {
        for (const auto &target_info : get_targets_response["result"]["targetInfos"]) {
            if (target_info.contains("type") && target_info["type"] == "page" &&
                target_info["targetId"] != tab_to_close) {
                std::string other_id = target_info["targetId"].get<std::string>();
                json attach_params;
                attach_params["targetId"] = other_id;
                attach_params["flatten"] = true;
                json attach_response = send_command("Target.attachToTarget", attach_params);
                if (attach_response.contains("result") && attach_response["result"].contains("sessionId")) {
                    global_state.current_target_id = other_id;
                    global_state.current_session_id = attach_response["result"]["sessionId"].get<std::string>();
                    enable_console_for_session();
                    debug_log::log("close_tab: attached to remaining tab targetId=" + other_id);
                }
                break;
            }
        }
    }

    result.success = true;
    result.message = "Tab closed.";
    return result;
}

browser_driver::CaptureScreenshotResult capture_screenshot() {
    browser_driver::CaptureScreenshotResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        return result;
    }

    json capture_params;
    capture_params["format"] = "png";
    json capture_response = send_command("Page.captureScreenshot", capture_params,
                                         global_state.current_session_id);

    if (capture_response.contains("error") && capture_response["error"].is_string()) {
        result.success = false;
        result.error_detail = capture_response["error"].get<std::string>();
        return result;
    }

    if (capture_response.contains("result") && capture_response["result"].contains("data") &&
        capture_response["result"]["data"].is_string()) {
        result.success = true;
        result.image_base64 = capture_response["result"]["data"].get<std::string>();
        result.mime_type = "image/png";
        debug_log::log("capture_screenshot: captured " + std::to_string(result.image_base64.size()) + " bytes base64");
    } else {
        result.success = false;
        result.error_detail = "Page.captureScreenshot did not return image data.";
    }

    return result;
}

void enable_console_for_session() {
    {
        std::lock_guard<std::mutex> lock(global_state.console_mutex);
        global_state.console_entries.clear();
    }
    if (global_state.connected && !global_state.current_session_id.empty()) {
        json enable_response = send_command("Runtime.enable", json::object(),
                                            global_state.current_session_id);
        if (enable_response.contains("error") && enable_response["error"].is_string()) {
            debug_log::log("enable_console_for_session: Runtime.enable failed: " +
                           enable_response["error"].get<std::string>());
        }
        json page_enable_response = send_command("Page.enable", json::object(),
                                                 global_state.current_session_id);
        if (page_enable_response.contains("error") && page_enable_response["error"].is_string()) {
            debug_log::log("enable_console_for_session: Page.enable failed: " +
                           page_enable_response["error"].get<std::string>());
        }
    }
}

namespace {

int level_weight(const std::string &level) {
    if (level == "debug") return 0;
    if (level == "log") return 1;
    if (level == "info") return 2;
    if (level == "warning") return 3;
    if (level == "error") return 4;
    return 2;
}

bool level_passes_min(const std::string &entry_level, const std::string &min_level) {
    return level_weight(entry_level) >= level_weight(min_level);
}

bool level_in_list(const std::string &entry_level, const std::vector<std::string> &levels) {
    for (const auto &allowed : levels) {
        if (entry_level == allowed) {
            return true;
        }
    }
    return false;
}

} // namespace

browser_driver::ConsoleMessagesResult get_console_messages(
    const browser_driver::GetConsoleMessagesOptions &options) {

    browser_driver::ConsoleMessagesResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        return result;
    }

    json eval_params;
    eval_params["expression"] = "Date.now()";
    auto time_before = std::chrono::steady_clock::now();
    json eval_response = send_command("Runtime.evaluate", eval_params,
                                      global_state.current_session_id, 5000);
    auto time_after = std::chrono::steady_clock::now();
    result.time_sync.server_now_ms = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    result.time_sync.round_trip_ms = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(time_after - time_before).count());
    if (eval_response.contains("result") && eval_response["result"].contains("result")) {
        const auto &res = eval_response["result"]["result"];
        if (res.contains("value") && res["value"].is_number()) {
            result.time_sync.browser_now_ms = res["value"].get<int64_t>();
            result.time_sync.offset_ms = result.time_sync.browser_now_ms - result.time_sync.server_now_ms;
        }
    }

    for (int drain_round = 0; drain_round < 20; ++drain_round) {
        service_websocket(50);
    }

    std::vector<browser_driver::ConsoleEntry> entries_copy;
    {
        std::lock_guard<std::mutex> lock(global_state.console_mutex);
        entries_copy = global_state.console_entries;
    }

    std::vector<browser_driver::ConsoleEntry> filtered;

    for (const auto &entry : entries_copy) {
        if (options.level_scope.type == browser_driver::LevelScopeType::MinLevel) {
            if (!level_passes_min(entry.level, options.level_scope.level)) {
                continue;
            }
        } else {
            if (options.level_scope.levels.empty() || !level_in_list(entry.level, options.level_scope.levels)) {
                continue;
            }
        }

        int64_t from_ms = 0;
        int64_t to_ms = result.time_sync.server_now_ms + 86400000;

        switch (options.time_scope.type) {
        case browser_driver::TimeScopeType::None:
            break;
        case browser_driver::TimeScopeType::LastDuration: {
            int64_t duration_ms = options.time_scope.last_duration_value;
            const std::string &unit = options.time_scope.last_duration_unit;
            if (unit == "seconds") {
                duration_ms *= 1000;
            } else if (unit == "minutes") {
                duration_ms *= 60 * 1000;
            }
            from_ms = result.time_sync.server_now_ms - duration_ms;
            to_ms = result.time_sync.server_now_ms;
            break;
        }
        case browser_driver::TimeScopeType::Range:
            from_ms = options.time_scope.from_ms;
            to_ms = options.time_scope.to_ms;
            break;
        case browser_driver::TimeScopeType::FromOnwards:
            from_ms = options.time_scope.from_ms;
            to_ms = result.time_sync.server_now_ms + 86400000;
            break;
        case browser_driver::TimeScopeType::Until:
            from_ms = 0;
            to_ms = options.time_scope.to_ms;
            break;
        }

        if (options.time_scope.type != browser_driver::TimeScopeType::None) {
            if (entry.timestamp_ms < from_ms || entry.timestamp_ms > to_ms) {
                continue;
            }
        }

        filtered.push_back(entry);
    }

    bool order_newest_first = (options.count_scope.order != "oldest_first");
    std::sort(filtered.begin(), filtered.end(),
              [order_newest_first](const browser_driver::ConsoleEntry &a,
                                  const browser_driver::ConsoleEntry &b) {
                  if (order_newest_first) {
                      return a.timestamp_ms > b.timestamp_ms;
                  }
                  return a.timestamp_ms < b.timestamp_ms;
              });

    result.total_matching = static_cast<int>(filtered.size());
    int max_entries = options.count_scope.max_entries;
    if (max_entries <= 0) {
        max_entries = 500;
    }
    result.truncated = (result.total_matching > max_entries);
    int take = std::min(result.total_matching, max_entries);

    for (int index = 0; index < take; ++index) {
        const auto &entry = filtered[static_cast<size_t>(index)];
        result.lines.push_back("[" + entry.level + "] " + entry.text);
    }
    result.returned_count = static_cast<int>(result.lines.size());
    result.success = true;
    return result;
}

static void ensure_dom_enabled() {
    if (!global_state.connected || global_state.current_session_id.empty()) {
        return;
    }
    json dom_enable_response = send_command("DOM.enable", json::object(),
                                            global_state.current_session_id);
    (void)dom_enable_response;
}

browser_driver::ListInteractiveElementsResult list_interactive_elements() {
    browser_driver::ListInteractiveElementsResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        return result;
    }

    const char *script =
        "(function(){"
        "var max=100,sel='input,textarea,button,[role=button],a,option,[role=option]';"
        "var nodes=document.querySelectorAll(sel);"
        "var out=[],idx=0;"
        "function esc(s){ if(!s)return''; return s.replace(/\\\\/g,'\\\\\\\\').replace(/\"/g,'\\\\\"').replace(/\\n/g,'\\\\n'); }"
        "for(var i=0;i<nodes.length&&idx<max;i++){"
        "var el=nodes[i];"
        "if(!el.offsetParent&&el.tagName!=='INPUT'&&el.tagName!=='TEXTAREA'&&el.tagName!=='OPTION'&&el.getAttribute('role')!=='option')continue;"
        "el.setAttribute('data-bmcps-id',String(idx));"
        "var label='';"
        "if(el.id){ var lbl=document.querySelector('label[for=\"'+el.id.replace(/\"/g,'\\\\\"')+'\"]'); if(lbl)label=(lbl.innerText||'').trim().substring(0,200); }"
        "if(!label&&el.placeholder)label=el.placeholder;"
        "if(!label&&el.getAttribute('aria-label'))label=el.getAttribute('aria-label')||'';"
        "var role=el.getAttribute('role')||(el.tagName==='A'?'link':el.tagName.toLowerCase());"
        "var text=(el.innerText||'').trim().substring(0,200);"
        "out.push({selector:'[data-bmcps-id=\"'+idx+'\"]',role:role,label:label,placeholder:(el.placeholder||''),type:(el.type||''),text:text});"
        "idx++;"
        "}"
        "return JSON.stringify(out);"
        "})()";

    json eval_params;
    eval_params["expression"] = script;
    eval_params["returnByValue"] = true;
    json eval_response = send_command("Runtime.evaluate", eval_params,
                                      global_state.current_session_id, 8000);

    if (eval_response.contains("error") && eval_response["error"].is_string()) {
        result.success = false;
        result.error_detail = eval_response["error"].get<std::string>();
        return result;
    }
    if (!eval_response.contains("result") || !eval_response["result"].contains("result")) {
        result.success = false;
        result.error_detail = "Runtime.evaluate did not return a result.";
        return result;
    }

    const auto &res = eval_response["result"]["result"];
    if (!res.contains("value") || !res["value"].is_string()) {
        result.success = false;
        result.error_detail = "list_interactive_elements script did not return JSON string.";
        return result;
    }

    std::string json_string = res["value"].get<std::string>();
    try {
        json array = json::parse(json_string);
        for (const auto &item : array) {
            browser_driver::InteractiveElement element;
            if (item.contains("selector") && item["selector"].is_string()) {
                element.selector = item["selector"].get<std::string>();
            }
            if (item.contains("role") && item["role"].is_string()) {
                element.role = item["role"].get<std::string>();
            }
            if (item.contains("label") && item["label"].is_string()) {
                element.label = item["label"].get<std::string>();
            }
            if (item.contains("placeholder") && item["placeholder"].is_string()) {
                element.placeholder = item["placeholder"].get<std::string>();
            }
            if (item.contains("type") && item["type"].is_string()) {
                element.type = item["type"].get<std::string>();
            }
            if (item.contains("text") && item["text"].is_string()) {
                element.text = item["text"].get<std::string>();
            }
            utf8_sanitize::sanitize(element.selector);
            utf8_sanitize::sanitize(element.role);
            utf8_sanitize::sanitize(element.label);
            utf8_sanitize::sanitize(element.placeholder);
            utf8_sanitize::sanitize(element.type);
            utf8_sanitize::sanitize(element.text);
            result.elements.push_back(element);
        }
    } catch (const json::parse_error &) {
        result.success = false;
        result.error_detail = "Failed to parse list_interactive_elements JSON.";
        return result;
    }

    result.success = true;
    return result;
}

browser_driver::DriverResult fill_field(const std::string &selector, const std::string &value,
                                        bool clear_first) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "fill_field failed.";
        return result;
    }

    std::string escaped_selector = json(selector).dump();
    std::string escaped_value = json(value).get<std::string>();

    std::string focus_script = "var el=document.querySelector(" + escaped_selector + ");"
        "if(!el){ throw new Error('Element not found: ' + " + escaped_selector + "); }"
        "el.focus();";
    if (clear_first) {
        focus_script += "el.value='';"
            "el.dispatchEvent(new Event('input',{bubbles:true}));"
            "el.dispatchEvent(new Event('change',{bubbles:true}));";
    }

    json eval_params;
    eval_params["expression"] = focus_script;
    json focus_response = send_command("Runtime.evaluate", eval_params,
                                       global_state.current_session_id, 5000);
    if (focus_response.contains("result") && focus_response["result"].contains("exceptionDetails")) {
        result.success = false;
        result.error_detail = "Element not found or focus failed: " + selector;
        result.message = "fill_field failed.";
        return result;
    }

    json insert_params;
    insert_params["text"] = value;
    json insert_response = send_command("Input.insertText", insert_params,
                                        global_state.current_session_id, 5000);
    if (insert_response.contains("error") && insert_response["error"].is_string()) {
        result.success = false;
        result.error_detail = insert_response["error"].get<std::string>();
        result.message = "fill_field failed.";
        return result;
    }

    result.success = true;
    result.message = "Field filled.";
    return result;
}

browser_driver::DriverResult click_element(const std::string &selector) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "click_element failed.";
        return result;
    }

    ensure_dom_enabled();

    json get_doc_response = send_command("DOM.getDocument", json::object(),
                                         global_state.current_session_id);
    if (!get_doc_response.contains("result") || !get_doc_response["result"].contains("root")) {
        result.success = false;
        result.error_detail = "DOM.getDocument failed.";
        result.message = "click_element failed.";
        return result;
    }
    int root_node_id = get_doc_response["result"]["root"]["nodeId"].get<int>();

    json query_params;
    query_params["nodeId"] = root_node_id;
    query_params["selector"] = selector;
    json query_response = send_command("DOM.querySelector", query_params,
                                       global_state.current_session_id);
    if (!query_response.contains("result") || query_response["result"]["nodeId"].get<int>() == 0) {
        std::string click_script = "var el=document.querySelector(" + json(selector).dump() + ");"
            "if(!el)throw new Error('Not found'); el.click();";
        json eval_params;
        eval_params["expression"] = click_script;
        json eval_response = send_command("Runtime.evaluate", eval_params,
                                          global_state.current_session_id, 5000);
        if (eval_response.contains("result") && eval_response["result"].contains("exceptionDetails")) {
            result.success = false;
            result.error_detail = "Element not found: " + selector;
            result.message = "click_element failed.";
            return result;
        }
        result.success = true;
        result.message = "Clicked (fallback).";
        return result;
    }

    int node_id = query_response["result"]["nodeId"].get<int>();
    json box_params;
    box_params["nodeId"] = node_id;
    json box_response = send_command("DOM.getBoxModel", box_params,
                                     global_state.current_session_id);
    if (!box_response.contains("result") || !box_response["result"].contains("model") ||
        !box_response["result"]["model"].contains("content")) {
        std::string click_script = "var el=document.querySelector(" + json(selector).dump() + ");"
            "if(!el)throw new Error('Not found'); el.click();";
        json eval_params;
        eval_params["expression"] = click_script;
        json eval_response = send_command("Runtime.evaluate", eval_params,
                                          global_state.current_session_id, 5000);
        if (eval_response.contains("result") && eval_response["result"].contains("exceptionDetails")) {
            result.success = false;
            result.error_detail = "Element not found or no box model: " + selector;
            result.message = "click_element failed.";
            return result;
        }
        result.success = true;
        result.message = "Clicked (fallback).";
        return result;
    }

    const auto &content = box_response["result"]["model"]["content"];
    double left = content[0].get<double>();
    double top = content[1].get<double>();
    double right = content[4].get<double>();
    double bottom = content[5].get<double>();
    int x = static_cast<int>((left + right) / 2);
    int y = static_cast<int>((top + bottom) / 2);

    json mouse_press;
    mouse_press["type"] = "mousePressed";
    mouse_press["x"] = x;
    mouse_press["y"] = y;
    mouse_press["button"] = "left";
    mouse_press["clickCount"] = 1;
    json mouse_release;
    mouse_release["type"] = "mouseReleased";
    mouse_release["x"] = x;
    mouse_release["y"] = y;
    mouse_release["button"] = "left";
    mouse_release["clickCount"] = 1;

    json press_response = send_command("Input.dispatchMouseEvent", mouse_press,
                                       global_state.current_session_id);
    json release_response = send_command("Input.dispatchMouseEvent", mouse_release,
                                        global_state.current_session_id);
    (void)press_response;
    (void)release_response;

    result.success = true;
    result.message = "Clicked.";
    return result;
}

browser_driver::DriverResult click_at_coordinates(int x, int y) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "click_at_coordinates failed.";
        return result;
    }

    json mouse_press;
    mouse_press["type"] = "mousePressed";
    mouse_press["x"] = x;
    mouse_press["y"] = y;
    mouse_press["button"] = "left";
    mouse_press["clickCount"] = 1;
    json mouse_release;
    mouse_release["type"] = "mouseReleased";
    mouse_release["x"] = x;
    mouse_release["y"] = y;
    mouse_release["button"] = "left";
    mouse_release["clickCount"] = 1;

    json press_response = send_command("Input.dispatchMouseEvent", mouse_press,
                                       global_state.current_session_id);
    json release_response = send_command("Input.dispatchMouseEvent", mouse_release,
                                        global_state.current_session_id);
    (void)press_response;
    (void)release_response;

    result.success = true;
    result.message = "Clicked at coordinates.";
    return result;
}

browser_driver::DriverResult scroll(const browser_driver::ScrollScope &scroll_scope) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "scroll failed.";
        return result;
    }

    int delta_x = scroll_scope.delta_x;
    int delta_y = scroll_scope.delta_y;

    if (scroll_scope.type == browser_driver::ScrollScopeType::Page) {
        std::string script = "window.scrollBy(" + std::to_string(delta_x) + "," + std::to_string(delta_y) + ");";
        json eval_params;
        eval_params["expression"] = script;
        json eval_response = send_command("Runtime.evaluate", eval_params,
                                          global_state.current_session_id, 5000);
        if (eval_response.contains("result") && eval_response["result"].contains("exceptionDetails")) {
            result.success = false;
            result.error_detail = "window.scrollBy failed.";
            result.message = "scroll failed.";
            return result;
        }
    } else {
        std::string escaped_selector = json(scroll_scope.selector).dump();
        std::string script = "var el=document.querySelector(" + escaped_selector + ");"
            "if(!el)throw new Error('Element not found');"
            "el.scrollBy(" + std::to_string(delta_x) + "," + std::to_string(delta_y) + ");";
        json eval_params;
        eval_params["expression"] = script;
        json eval_response = send_command("Runtime.evaluate", eval_params,
                                          global_state.current_session_id, 5000);
        if (eval_response.contains("result") && eval_response["result"].contains("exceptionDetails")) {
            result.success = false;
            result.error_detail = "Element not found or scroll failed: " + scroll_scope.selector;
            result.message = "scroll failed.";
            return result;
        }
    }

    result.success = true;
    result.message = "Scrolled.";
    return result;
}

browser_driver::DriverResult set_window_bounds(int width, int height) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_target_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser. Call open_browser first.";
        result.message = "set_window_bounds failed.";
        return result;
    }

    json get_window_params;
    get_window_params["targetId"] = global_state.current_target_id;
    json get_window_response = send_command("Browser.getWindowForTarget", get_window_params, "", 5000);

    if (!get_window_response.contains("result") || !get_window_response["result"].contains("windowId")) {
        result.success = false;
        result.error_detail = "Browser.getWindowForTarget failed or no windowId.";
        result.message = "set_window_bounds failed.";
        return result;
    }

    int window_id = get_window_response["result"]["windowId"].get<int>();

    json bounds;
    bounds["width"] = width;
    bounds["height"] = height;
    json set_params;
    set_params["windowId"] = window_id;
    set_params["bounds"] = bounds;
    json set_response = send_command("Browser.setWindowBounds", set_params, "", 5000);

    if (set_response.contains("error") && set_response["error"].is_string()) {
        result.success = false;
        result.error_detail = set_response["error"].get<std::string>();
        result.message = "set_window_bounds failed.";
        return result;
    }

    result.success = true;
    result.message = "Window resized to " + std::to_string(width) + "x" + std::to_string(height) + ".";
    return result;
}

// --- evaluate_javascript ---

browser_driver::EvaluateJavaScriptResult evaluate_javascript(const std::string &script,
                                                            int timeout_milliseconds) {
    browser_driver::EvaluateJavaScriptResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        return result;
    }

    json eval_params;
    eval_params["expression"] = script;
    eval_params["returnByValue"] = true;
    if (global_state.current_execution_context_id != 0) {
        eval_params["contextId"] = global_state.current_execution_context_id;
    }

    json eval_response = send_command("Runtime.evaluate", eval_params,
                                      global_state.current_session_id, timeout_milliseconds);

    if (eval_response.contains("result") && eval_response["result"].contains("exceptionDetails")) {
        result.success = false;
        std::string exception_text;
        const auto &ex = eval_response["result"]["exceptionDetails"];
        if (ex.contains("text") && ex["text"].is_string()) {
            exception_text = ex["text"].get<std::string>();
        }
        if (ex.contains("exception") && ex["exception"].contains("description") &&
            ex["exception"]["description"].is_string()) {
            if (!exception_text.empty()) {
                exception_text += "; ";
            }
            exception_text += ex["exception"]["description"].get<std::string>();
        }
        result.error_detail = exception_text.empty() ? "Script threw an exception." : exception_text;
        return result;
    }

    if (eval_response.contains("error") && eval_response["error"].is_string()) {
        result.success = false;
        result.error_detail = eval_response["error"].get<std::string>();
        return result;
    }

    if (!eval_response.contains("result")) {
        result.success = false;
        result.error_detail = "Runtime.evaluate did not return a result.";
        return result;
    }

    const json &res = eval_response["result"];
    if (res.contains("result")) {
        result.result_json_string = res["result"].dump();
    } else {
        result.result_json_string = "null";
    }
    result.success = true;
    return result;
}

// --- hover_element (mouse move to element center) ---

browser_driver::DriverResult hover_element(const std::string &selector) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "hover_element failed.";
        return result;
    }

    ensure_dom_enabled();

    json get_doc_response = send_command("DOM.getDocument", json::object(),
                                         global_state.current_session_id);
    if (!get_doc_response.contains("result") || !get_doc_response["result"].contains("root")) {
        result.success = false;
        result.error_detail = "DOM.getDocument failed.";
        result.message = "hover_element failed.";
        return result;
    }
    int root_node_id = get_doc_response["result"]["root"]["nodeId"].get<int>();

    json query_params;
    query_params["nodeId"] = root_node_id;
    query_params["selector"] = selector;
    json query_response = send_command("DOM.querySelector", query_params,
                                       global_state.current_session_id);
    if (!query_response.contains("result") || query_response["result"]["nodeId"].get<int>() == 0) {
        result.success = false;
        result.error_detail = "Element not found: " + selector;
        result.message = "hover_element failed.";
        return result;
    }

    int node_id = query_response["result"]["nodeId"].get<int>();
    json box_params;
    box_params["nodeId"] = node_id;
    json box_response = send_command("DOM.getBoxModel", box_params,
                                     global_state.current_session_id);
    if (!box_response.contains("result") || !box_response["result"].contains("model") ||
        !box_response["result"]["model"].contains("content")) {
        result.success = false;
        result.error_detail = "No box model for element: " + selector;
        result.message = "hover_element failed.";
        return result;
    }

    const auto &content = box_response["result"]["model"]["content"];
    int x = static_cast<int>((content[0].get<double>() + content[4].get<double>()) / 2);
    int y = static_cast<int>((content[1].get<double>() + content[5].get<double>()) / 2);

    json mouse_move;
    mouse_move["type"] = "mouseMoved";
    mouse_move["x"] = x;
    mouse_move["y"] = y;
    send_command("Input.dispatchMouseEvent", mouse_move, global_state.current_session_id);

    result.success = true;
    result.message = "Hovered.";
    return result;
}

// --- double_click_element ---

static browser_driver::DriverResult click_element_with_options(const std::string &selector,
                                                               const std::string &button,
                                                               int click_count) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "click failed.";
        return result;
    }

    ensure_dom_enabled();

    json get_doc_response = send_command("DOM.getDocument", json::object(),
                                         global_state.current_session_id);
    if (!get_doc_response.contains("result") || !get_doc_response["result"].contains("root")) {
        result.success = false;
        result.error_detail = "DOM.getDocument failed.";
        return result;
    }
    int root_node_id = get_doc_response["result"]["root"]["nodeId"].get<int>();

    json query_params;
    query_params["nodeId"] = root_node_id;
    query_params["selector"] = selector;
    json query_response = send_command("DOM.querySelector", query_params,
                                       global_state.current_session_id);
    if (!query_response.contains("result") || query_response["result"]["nodeId"].get<int>() == 0) {
        std::string click_script = "var el=document.querySelector(" + json(selector).dump() + ");"
            "if(!el)throw new Error('Not found'); el.click();";
        json eval_params;
        eval_params["expression"] = click_script;
        json eval_response = send_command("Runtime.evaluate", eval_params,
                                          global_state.current_session_id, 5000);
        if (eval_response.contains("result") && eval_response["result"].contains("exceptionDetails")) {
            result.success = false;
            result.error_detail = "Element not found: " + selector;
            return result;
        }
        result.success = true;
        result.message = "Clicked (fallback).";
        return result;
    }

    int node_id = query_response["result"]["nodeId"].get<int>();
    json box_params;
    box_params["nodeId"] = node_id;
    json box_response = send_command("DOM.getBoxModel", box_params,
                                     global_state.current_session_id);
    if (!box_response.contains("result") || !box_response["result"].contains("model") ||
        !box_response["result"]["model"].contains("content")) {
        std::string click_script = "var el=document.querySelector(" + json(selector).dump() + ");"
            "if(!el)throw new Error('Not found'); el.click();";
        json eval_params;
        eval_params["expression"] = click_script;
        json eval_response = send_command("Runtime.evaluate", eval_params,
                                          global_state.current_session_id, 5000);
        if (eval_response.contains("result") && eval_response["result"].contains("exceptionDetails")) {
            result.success = false;
            result.error_detail = "Element not found or no box model: " + selector;
            return result;
        }
        result.success = true;
        result.message = "Clicked (fallback).";
        return result;
    }

    const auto &content = box_response["result"]["model"]["content"];
    double left = content[0].get<double>();
    double top = content[1].get<double>();
    double right = content[4].get<double>();
    double bottom = content[5].get<double>();
    int x = static_cast<int>((left + right) / 2);
    int y = static_cast<int>((top + bottom) / 2);

    json mouse_press;
    mouse_press["type"] = "mousePressed";
    mouse_press["x"] = x;
    mouse_press["y"] = y;
    mouse_press["button"] = button;
    mouse_press["clickCount"] = click_count;
    json mouse_release;
    mouse_release["type"] = "mouseReleased";
    mouse_release["x"] = x;
    mouse_release["y"] = y;
    mouse_release["button"] = button;
    mouse_release["clickCount"] = click_count;

    send_command("Input.dispatchMouseEvent", mouse_press, global_state.current_session_id);
    send_command("Input.dispatchMouseEvent", mouse_release, global_state.current_session_id);

    result.success = true;
    result.message = "Clicked.";
    return result;
}

browser_driver::DriverResult double_click_element(const std::string &selector) {
    browser_driver::DriverResult r = click_element_with_options(selector, "left", 2);
    if (r.success) {
        r.message = "Double-clicked.";
    }
    return r;
}

browser_driver::DriverResult right_click_element(const std::string &selector) {
    return click_element_with_options(selector, "right", 1);
}

// --- drag_and_drop_selectors, drag_from_to_coordinates ---

static bool get_element_center(const std::string &selector, int &out_x, int &out_y) {
    json get_doc = send_command("DOM.getDocument", json::object(),
                                global_state.current_session_id);
    if (!get_doc.contains("result") || !get_doc["result"].contains("root")) {
        return false;
    }
    int root_id = get_doc["result"]["root"]["nodeId"].get<int>();
    json qp;
    qp["nodeId"] = root_id;
    qp["selector"] = selector;
    json qr = send_command("DOM.querySelector", qp, global_state.current_session_id);
    if (!qr.contains("result") || qr["result"]["nodeId"].get<int>() == 0) {
        return false;
    }
    json bp;
    bp["nodeId"] = qr["result"]["nodeId"].get<int>();
    json br = send_command("DOM.getBoxModel", bp, global_state.current_session_id);
    if (!br.contains("result") || !br["result"].contains("model") ||
        !br["result"]["model"].contains("content")) {
        return false;
    }
    const auto &content = br["result"]["model"]["content"];
    out_x = static_cast<int>((content[0].get<double>() + content[4].get<double>()) / 2);
    out_y = static_cast<int>((content[1].get<double>() + content[5].get<double>()) / 2);
    return true;
}

browser_driver::DriverResult drag_and_drop_selectors(const std::string &source_selector,
                                                      const std::string &target_selector) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "drag_and_drop failed.";
        return result;
    }

    ensure_dom_enabled();

    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    if (!get_element_center(source_selector, x1, y1)) {
        result.success = false;
        result.error_detail = "Source element not found: " + source_selector;
        result.message = "drag_and_drop failed.";
        return result;
    }
    if (!get_element_center(target_selector, x2, y2)) {
        result.success = false;
        result.error_detail = "Target element not found: " + target_selector;
        result.message = "drag_and_drop failed.";
        return result;
    }

    json press;
    press["type"] = "mousePressed";
    press["x"] = x1;
    press["y"] = y1;
    press["button"] = "left";
    press["clickCount"] = 1;
    json move;
    move["type"] = "mouseMoved";
    move["x"] = x2;
    move["y"] = y2;
    json release;
    release["type"] = "mouseReleased";
    release["x"] = x2;
    release["y"] = y2;
    release["button"] = "left";
    release["clickCount"] = 1;

    send_command("Input.dispatchMouseEvent", press, global_state.current_session_id);
    send_command("Input.dispatchMouseEvent", move, global_state.current_session_id);
    send_command("Input.dispatchMouseEvent", release, global_state.current_session_id);

    result.success = true;
    result.message = "Drag and drop done.";
    return result;
}

browser_driver::DriverResult drag_from_to_coordinates(int x1, int y1, int x2, int y2) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "drag_from_to failed.";
        return result;
    }

    json press;
    press["type"] = "mousePressed";
    press["x"] = x1;
    press["y"] = y1;
    press["button"] = "left";
    press["clickCount"] = 1;
    json move;
    move["type"] = "mouseMoved";
    move["x"] = x2;
    move["y"] = y2;
    json release;
    release["type"] = "mouseReleased";
    release["x"] = x2;
    release["y"] = y2;
    release["button"] = "left";
    release["clickCount"] = 1;

    send_command("Input.dispatchMouseEvent", press, global_state.current_session_id);
    send_command("Input.dispatchMouseEvent", move, global_state.current_session_id);
    send_command("Input.dispatchMouseEvent", release, global_state.current_session_id);

    result.success = true;
    result.message = "Drag from to done.";
    return result;
}

// --- get_page_source, get_outer_html ---

browser_driver::GetPageSourceResult get_page_source() {
    browser_driver::GetPageSourceResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        return result;
    }

    json eval_params;
    eval_params["expression"] = "document.documentElement.outerHTML";
    eval_params["returnByValue"] = true;
    if (global_state.current_execution_context_id != 0) {
        eval_params["contextId"] = global_state.current_execution_context_id;
    }
    json eval_response = send_command("Runtime.evaluate", eval_params,
                                      global_state.current_session_id, 5000);

    if (eval_response.contains("result") && eval_response["result"].contains("exceptionDetails")) {
        result.success = false;
        result.error_detail = "Failed to get page source.";
        return result;
    }
    if (!eval_response.contains("result") || !eval_response["result"].contains("result")) {
        result.success = false;
        result.error_detail = "Runtime.evaluate did not return result.";
        return result;
    }
    const json &res = eval_response["result"]["result"];
    if (res.contains("value") && res["value"].is_string()) {
        result.html = res["value"].get<std::string>();
    }
    result.success = true;
    return result;
}

browser_driver::GetPageSourceResult get_outer_html(const std::string &selector) {
    browser_driver::GetPageSourceResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        return result;
    }

    std::string escaped_selector = json(selector).dump();
    std::string script = "var el=document.querySelector(" + escaped_selector + ");"
        "el ? el.outerHTML : '';";
    json eval_params;
    eval_params["expression"] = script;
    eval_params["returnByValue"] = true;
    if (global_state.current_execution_context_id != 0) {
        eval_params["contextId"] = global_state.current_execution_context_id;
    }
    json eval_response = send_command("Runtime.evaluate", eval_params,
                                      global_state.current_session_id, 5000);

    if (eval_response.contains("result") && eval_response["result"].contains("exceptionDetails")) {
        result.success = false;
        result.error_detail = "Element not found or failed: " + selector;
        return result;
    }
    if (!eval_response.contains("result") || !eval_response["result"].contains("result")) {
        result.success = false;
        result.error_detail = "Runtime.evaluate did not return result.";
        return result;
    }
    const json &res = eval_response["result"]["result"];
    if (res.contains("value") && res["value"].is_string()) {
        result.html = res["value"].get<std::string>();
    }
    result.success = true;
    return result;
}

// --- send_keys, key_press, key_down, key_up ---

browser_driver::DriverResult send_keys(const std::string &keys, const std::string &selector) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "send_keys failed.";
        return result;
    }

    if (!selector.empty()) {
        std::string escaped_selector = json(selector).dump();
        std::string focus_script = "var el=document.querySelector(" + escaped_selector + ");"
            "if(!el){ throw new Error('Element not found'); } el.focus();";
        json eval_params;
        eval_params["expression"] = focus_script;
        json focus_response = send_command("Runtime.evaluate", eval_params,
                                           global_state.current_session_id, 5000);
        if (focus_response.contains("result") && focus_response["result"].contains("exceptionDetails")) {
            result.success = false;
            result.error_detail = "Element not found: " + selector;
            result.message = "send_keys failed.";
            return result;
        }
    }

    std::string literal_text;
    std::vector<std::string> special_keys;
    for (size_t i = 0; i < keys.size(); ) {
        if (keys[i] == '{' && i + 1 < keys.size()) {
            size_t close = keys.find('}', i + 1);
            if (close != std::string::npos) {
                std::string key_name = keys.substr(i + 1, close - i - 1);
                if (!literal_text.empty()) {
                    json insert_params;
                    insert_params["text"] = literal_text;
                    send_command("Input.insertText", insert_params, global_state.current_session_id);
                    literal_text.clear();
                }
                json key_params;
                key_params["key"] = key_name;
                key_params["type"] = "keyDown";
                send_command("Input.dispatchKeyEvent", key_params, global_state.current_session_id);
                key_params["type"] = "keyUp";
                send_command("Input.dispatchKeyEvent", key_params, global_state.current_session_id);
                i = close + 1;
                continue;
            }
        }
        literal_text += keys[i];
        i++;
    }
    if (!literal_text.empty()) {
        json insert_params;
        insert_params["text"] = literal_text;
        json insert_response = send_command("Input.insertText", insert_params,
                                            global_state.current_session_id, 5000);
        if (insert_response.contains("error") && insert_response["error"].is_string()) {
            result.success = false;
            result.error_detail = insert_response["error"].get<std::string>();
            result.message = "send_keys failed.";
            return result;
        }
    }

    result.success = true;
    result.message = "Keys sent.";
    return result;
}

browser_driver::DriverResult key_press(const std::string &key) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "key_press failed.";
        return result;
    }

    json key_down_params;
    key_down_params["key"] = key;
    key_down_params["type"] = "keyDown";
    send_command("Input.dispatchKeyEvent", key_down_params, global_state.current_session_id);
    json key_up_params;
    key_up_params["key"] = key;
    key_up_params["type"] = "keyUp";
    send_command("Input.dispatchKeyEvent", key_up_params, global_state.current_session_id);

    result.success = true;
    result.message = "Key pressed.";
    return result;
}

browser_driver::DriverResult key_down(const std::string &key) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "key_down failed.";
        return result;
    }

    json key_params;
    key_params["key"] = key;
    key_params["type"] = "keyDown";
    send_command("Input.dispatchKeyEvent", key_params, global_state.current_session_id);

    result.success = true;
    result.message = "Key down.";
    return result;
}

browser_driver::DriverResult key_up(const std::string &key) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "key_up failed.";
        return result;
    }

    json key_params;
    key_params["key"] = key;
    key_params["type"] = "keyUp";
    send_command("Input.dispatchKeyEvent", key_params, global_state.current_session_id);

    result.success = true;
    result.message = "Key up.";
    return result;
}

// --- wait_seconds, wait_for_selector, wait_for_navigation ---

browser_driver::DriverResult wait_seconds(double seconds) {
    browser_driver::DriverResult result;
    if (seconds <= 0 || seconds > 3600) {
        result.success = false;
        result.error_detail = "seconds must be in (0, 3600].";
        result.message = "wait failed.";
        return result;
    }
    int milliseconds = static_cast<int>(seconds * 1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    result.success = true;
    result.message = "Waited " + std::to_string(seconds) + " s.";
    return result;
}

browser_driver::DriverResult wait_for_selector(const std::string &selector, int timeout_milliseconds) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "wait_for_selector failed.";
        return result;
    }

    std::string escaped_selector = json(selector).dump();
    std::string script = "document.querySelector(" + escaped_selector + ") ? true : false;";
    json eval_params;
    eval_params["expression"] = script;
    eval_params["returnByValue"] = true;
    if (global_state.current_execution_context_id != 0) {
        eval_params["contextId"] = global_state.current_execution_context_id;
    }

    auto start = std::chrono::steady_clock::now();
    int elapsed = 0;
    while (elapsed < timeout_milliseconds) {
        json eval_response = send_command("Runtime.evaluate", eval_params,
                                          global_state.current_session_id, 2000);
        if (eval_response.contains("result") && eval_response["result"].contains("result")) {
            const json &res = eval_response["result"]["result"];
            if (res.contains("value") && res["value"].is_boolean() && res["value"].get<bool>()) {
                result.success = true;
                result.message = "Selector found.";
                return result;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count());
    }

    result.success = false;
    result.error_detail = "Timeout waiting for selector: " + selector;
    result.message = "wait_for_selector failed.";
    return result;
}

browser_driver::DriverResult wait_for_navigation(int timeout_milliseconds) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "wait_for_navigation failed.";
        return result;
    }

    std::string script = "document.readyState";
    json eval_params;
    eval_params["expression"] = script;
    eval_params["returnByValue"] = true;
    if (global_state.current_execution_context_id != 0) {
        eval_params["contextId"] = global_state.current_execution_context_id;
    }

    auto start = std::chrono::steady_clock::now();
    int elapsed = 0;
    std::string last_ready_state;
    while (elapsed < timeout_milliseconds) {
        json eval_response = send_command("Runtime.evaluate", eval_params,
                                          global_state.current_session_id, 2000);
        if (eval_response.contains("result") && eval_response["result"].contains("result")) {
            const json &res = eval_response["result"]["result"];
            if (res.contains("value") && res["value"].is_string()) {
                std::string ready_state = res["value"].get<std::string>();
                if (ready_state == "complete") {
                    result.success = true;
                    result.message = "Navigation complete.";
                    return result;
                }
                last_ready_state = ready_state;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count());
    }

    result.success = false;
    result.error_detail = "Timeout waiting for navigation (last readyState: " + last_ready_state + ").";
    result.message = "wait_for_navigation failed.";
    return result;
}

// --- get_cookies, set_cookie, clear_cookies ---

browser_driver::GetCookiesResult get_cookies(const std::string &url) {
    browser_driver::GetCookiesResult result;

    if (!global_state.connected) {
        result.success = false;
        result.error_detail = "No active browser. Call open_browser first.";
        return result;
    }

    json params;
    if (!url.empty()) {
        params["urls"] = json::array({url});
    }
    json response = send_command("Network.getCookies", params, "", 5000);

    if (response.contains("error") && response["error"].is_string()) {
        result.success = false;
        result.error_detail = response["error"].get<std::string>();
        return result;
    }
    if (!response.contains("result") || !response["result"].contains("cookies")) {
        result.success = false;
        result.error_detail = "Network.getCookies did not return cookies.";
        return result;
    }

    for (const auto &cookie : response["result"]["cookies"]) {
        browser_driver::CookieEntry entry;
        if (cookie.contains("name") && cookie["name"].is_string()) {
            entry.name = cookie["name"].get<std::string>();
        }
        if (cookie.contains("value") && cookie["value"].is_string()) {
            entry.value = cookie["value"].get<std::string>();
        }
        if (cookie.contains("domain") && cookie["domain"].is_string()) {
            entry.domain = cookie["domain"].get<std::string>();
        }
        if (cookie.contains("path") && cookie["path"].is_string()) {
            entry.path = cookie["path"].get<std::string>();
        }
        result.cookies.push_back(entry);
    }
    result.success = true;
    return result;
}

browser_driver::DriverResult set_cookie(const std::string &name, const std::string &value,
                                         const std::string &url,
                                         const std::string &domain,
                                         const std::string &path) {
    browser_driver::DriverResult result;

    if (!global_state.connected) {
        result.success = false;
        result.error_detail = "No active browser. Call open_browser first.";
        result.message = "set_cookie failed.";
        return result;
    }

    json params;
    params["name"] = name;
    params["value"] = value;
    if (!url.empty()) {
        params["url"] = url;
    }
    if (!domain.empty()) {
        params["domain"] = domain;
    }
    if (!path.empty()) {
        params["path"] = path;
    }
    json response = send_command("Network.setCookie", params, "", 5000);

    if (response.contains("result") && response["result"].is_boolean() && !response["result"].get<bool>()) {
        result.success = false;
        result.error_detail = "Network.setCookie returned false.";
        result.message = "set_cookie failed.";
        return result;
    }
    if (response.contains("error") && response["error"].is_string()) {
        result.success = false;
        result.error_detail = response["error"].get<std::string>();
        result.message = "set_cookie failed.";
        return result;
    }
    result.success = true;
    result.message = "Cookie set.";
    return result;
}

browser_driver::DriverResult clear_cookies() {
    browser_driver::DriverResult result;

    if (!global_state.connected) {
        result.success = false;
        result.error_detail = "No active browser. Call open_browser first.";
        result.message = "clear_cookies failed.";
        return result;
    }

    json response = send_command("Network.clearBrowserCookies", json::object(), "", 5000);
    if (response.contains("error") && response["error"].is_string()) {
        result.success = false;
        result.error_detail = response["error"].get<std::string>();
        result.message = "clear_cookies failed.";
        return result;
    }
    result.success = true;
    result.message = "Cookies cleared.";
    return result;
}

// --- JavaScript dialog: state from Page.javascriptDialogOpening; handle via Page.handleJavaScriptDialog ---

browser_driver::GetDialogMessageResult get_dialog_message() {
    browser_driver::GetDialogMessageResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session.";
        return result;
    }

    std::lock_guard<std::mutex> lock(global_state.dialog_mutex);
    result.dialog_open = !global_state.last_dialog_message.empty() || !global_state.last_dialog_type.empty();
    result.message = global_state.last_dialog_message;
    result.type = global_state.last_dialog_type;
    result.success = true;
    return result;
}

browser_driver::DriverResult accept_dialog() {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "accept_dialog failed.";
        return result;
    }

    json params;
    params["accept"] = true;
    json response = send_command("Page.handleJavaScriptDialog", params,
                                 global_state.current_session_id, 5000);
    if (response.contains("error") && response["error"].is_string()) {
        result.success = false;
        result.error_detail = response["error"].get<std::string>();
        result.message = "accept_dialog failed.";
        return result;
    }
    {
        std::lock_guard<std::mutex> lock(global_state.dialog_mutex);
        global_state.last_dialog_message.clear();
        global_state.last_dialog_type.clear();
    }
    result.success = true;
    result.message = "Dialog accepted.";
    return result;
}

browser_driver::DriverResult dismiss_dialog() {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "dismiss_dialog failed.";
        return result;
    }

    json params;
    params["accept"] = false;
    json response = send_command("Page.handleJavaScriptDialog", params,
                                 global_state.current_session_id, 5000);
    if (response.contains("error") && response["error"].is_string()) {
        result.success = false;
        result.error_detail = response["error"].get<std::string>();
        result.message = "dismiss_dialog failed.";
        return result;
    }
    {
        std::lock_guard<std::mutex> lock(global_state.dialog_mutex);
        global_state.last_dialog_message.clear();
        global_state.last_dialog_type.clear();
    }
    result.success = true;
    result.message = "Dialog dismissed.";
    return result;
}

browser_driver::DriverResult send_prompt_value(const std::string &text) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "send_prompt_value failed.";
        return result;
    }

    json params;
    params["accept"] = true;
    params["promptText"] = text;
    json response = send_command("Page.handleJavaScriptDialog", params,
                                 global_state.current_session_id, 5000);
    if (response.contains("error") && response["error"].is_string()) {
        result.success = false;
        result.error_detail = response["error"].get<std::string>();
        result.message = "send_prompt_value failed.";
        return result;
    }
    {
        std::lock_guard<std::mutex> lock(global_state.dialog_mutex);
        global_state.last_dialog_message.clear();
        global_state.last_dialog_type.clear();
    }
    result.success = true;
    result.message = "Prompt value sent.";
    return result;
}

// --- upload_file ---

browser_driver::DriverResult upload_file(const std::string &selector, const std::string &file_path) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "upload_file failed.";
        return result;
    }

    ensure_dom_enabled();

    json get_doc_response = send_command("DOM.getDocument", json::object(),
                                         global_state.current_session_id);
    if (!get_doc_response.contains("result") || !get_doc_response["result"].contains("root")) {
        result.success = false;
        result.error_detail = "DOM.getDocument failed.";
        result.message = "upload_file failed.";
        return result;
    }
    int root_node_id = get_doc_response["result"]["root"]["nodeId"].get<int>();

    json query_params;
    query_params["nodeId"] = root_node_id;
    query_params["selector"] = selector;
    json query_response = send_command("DOM.querySelector", query_params,
                                       global_state.current_session_id);
    if (!query_response.contains("result") || query_response["result"]["nodeId"].get<int>() == 0) {
        result.success = false;
        result.error_detail = "File input element not found: " + selector;
        result.message = "upload_file failed.";
        return result;
    }

    int node_id = query_response["result"]["nodeId"].get<int>();
    json params;
    params["nodeId"] = node_id;
    params["files"] = json::array({file_path});
    json set_response = send_command("DOM.setFileInputFiles", params,
                                     global_state.current_session_id, 5000);

    if (set_response.contains("error") && set_response["error"].is_string()) {
        result.success = false;
        result.error_detail = set_response["error"].get<std::string>();
        result.message = "upload_file failed.";
        return result;
    }
    result.success = true;
    result.message = "File set.";
    return result;
}

// --- list_frames, switch_to_frame, switch_to_main_frame ---

browser_driver::ListFramesResult list_frames() {
    browser_driver::ListFramesResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        return result;
    }

    json response = send_command("Page.getFrameTree", json::object(),
                                 global_state.current_session_id, 5000);
    if (!response.contains("result") || !response["result"].contains("frameTree")) {
        result.success = false;
        result.error_detail = "Page.getFrameTree failed.";
        return result;
    }

    std::function<void(const json &, const std::string &)> collect_frames;
    collect_frames = [&](const json &frame_tree, const std::string &parent_id) {
        if (!frame_tree.contains("frame")) {
            return;
        }
        const json &frame = frame_tree["frame"];
        browser_driver::FrameInfo info;
        if (frame.contains("id") && frame["id"].is_string()) {
            info.frame_id = frame["id"].get<std::string>();
        }
        if (frame.contains("url") && frame["url"].is_string()) {
            info.url = frame["url"].get<std::string>();
        }
        info.parent_frame_id = parent_id;
        result.frames.push_back(info);
        if (frame_tree.contains("childFrames") && frame_tree["childFrames"].is_array()) {
            for (const auto &child : frame_tree["childFrames"]) {
                collect_frames(child, info.frame_id);
            }
        }
    };
    collect_frames(response["result"]["frameTree"], "");

    result.success = true;
    return result;
}

browser_driver::DriverResult switch_to_frame(const std::string &frame_id_or_index) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "switch_to_frame failed.";
        return result;
    }

    browser_driver::ListFramesResult list_result = list_frames();
    if (!list_result.success || list_result.frames.empty()) {
        result.success = false;
        result.error_detail = "Could not list frames.";
        result.message = "switch_to_frame failed.";
        return result;
    }

    if (frame_id_or_index.empty()) {
        global_state.current_execution_context_id = 0;
        result.success = true;
        result.message = "Switched to main frame.";
        return result;
    }

    bool is_index = (frame_id_or_index.size() == 1 && frame_id_or_index[0] >= '0' && frame_id_or_index[0] <= '9') ||
                    (frame_id_or_index.size() > 1 && frame_id_or_index.find_first_not_of("0123456789") == std::string::npos);
    int index = -1;
    if (is_index) {
        try {
            index = std::stoi(frame_id_or_index);
        } catch (...) {
        }
    }

    if (index >= 0 && index < static_cast<int>(list_result.frames.size())) {
        std::string frame_id = list_result.frames[index].frame_id;
        json eval_params;
        eval_params["expression"] = "undefined";
        eval_params["contextId"] = 0;
        for (int attempt = 0; attempt < 50; attempt++) {
            service_websocket(100);
            std::lock_guard<std::mutex> lock(global_state.frame_mutex);
            auto it = global_state.execution_context_id_by_frame_id.find(frame_id);
            if (it != global_state.execution_context_id_by_frame_id.end()) {
                global_state.current_execution_context_id = it->second;
                result.success = true;
                result.message = "Switched to frame.";
                return result;
            }
        }
        result.success = false;
        result.error_detail = "Execution context for frame not found (enable Runtime and wait for executionContextCreated).";
        result.message = "switch_to_frame failed.";
        return result;
    }

    std::lock_guard<std::mutex> lock(global_state.frame_mutex);
    auto it = global_state.execution_context_id_by_frame_id.find(frame_id_or_index);
    if (it != global_state.execution_context_id_by_frame_id.end()) {
        global_state.current_execution_context_id = it->second;
        result.success = true;
        result.message = "Switched to frame.";
        return result;
    }
    result.success = false;
    result.error_detail = "Frame id or index not found: " + frame_id_or_index;
    result.message = "switch_to_frame failed.";
    return result;
}

browser_driver::DriverResult switch_to_main_frame() {
    browser_driver::DriverResult result;
    global_state.current_execution_context_id = 0;
    result.success = true;
    result.message = "Switched to main frame.";
    return result;
}

// --- get_storage, set_storage ---

browser_driver::GetPageSourceResult get_storage(const std::string &storage_type,
                                                const std::string &key) {
    browser_driver::GetPageSourceResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        return result;
    }

    std::string store = (storage_type == "sessionStorage") ? "sessionStorage" : "localStorage";
    std::string script;
    if (key.empty()) {
        script = "JSON.stringify(Object.fromEntries(Object.entries(" + store + ")));";
    } else {
        script = "(() => { var s = " + store + "; var v = s.getItem(" + json(key).dump() + "); return v !== null ? v : ''; })();";
    }
    json eval_params;
    eval_params["expression"] = script;
    eval_params["returnByValue"] = true;
    if (global_state.current_execution_context_id != 0) {
        eval_params["contextId"] = global_state.current_execution_context_id;
    }
    json eval_response = send_command("Runtime.evaluate", eval_params,
                                      global_state.current_session_id, 5000);

    if (eval_response.contains("result") && eval_response["result"].contains("exceptionDetails")) {
        result.success = false;
        result.error_detail = "get_storage failed.";
        return result;
    }
    if (!eval_response.contains("result") || !eval_response["result"].contains("result")) {
        result.success = false;
        result.error_detail = "Runtime.evaluate did not return result.";
        return result;
    }
    const json &res = eval_response["result"]["result"];
    if (res.contains("value") && res["value"].is_string()) {
        result.html = res["value"].get<std::string>();
    }
    result.success = true;
    return result;
}

browser_driver::DriverResult set_storage(const std::string &storage_type,
                                         const std::string &key,
                                         const std::string &value) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "set_storage failed.";
        return result;
    }

    std::string store = (storage_type == "sessionStorage") ? "sessionStorage" : "localStorage";
    std::string script = store + ".setItem(" + json(key).dump() + "," + json(value).dump() + ");";
    json eval_params;
    eval_params["expression"] = script;
    if (global_state.current_execution_context_id != 0) {
        eval_params["contextId"] = global_state.current_execution_context_id;
    }
    json eval_response = send_command("Runtime.evaluate", eval_params,
                                      global_state.current_session_id, 5000);

    if (eval_response.contains("result") && eval_response["result"].contains("exceptionDetails")) {
        result.success = false;
        result.error_detail = "set_storage failed.";
        result.message = "set_storage failed.";
        return result;
    }
    result.success = true;
    result.message = "Storage set.";
    return result;
}

// --- get_clipboard, set_clipboard (async Promise via Runtime.awaitPromise) ---

browser_driver::GetPageSourceResult get_clipboard() {
    browser_driver::GetPageSourceResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        return result;
    }

    json eval_params;
    eval_params["expression"] = "navigator.clipboard.readText()";
    eval_params["awaitPromise"] = true;
    eval_params["returnByValue"] = true;
    if (global_state.current_execution_context_id != 0) {
        eval_params["contextId"] = global_state.current_execution_context_id;
    }
    json eval_response = send_command("Runtime.evaluate", eval_params,
                                      global_state.current_session_id, 5000);

    if (eval_response.contains("result") && eval_response["result"].contains("exceptionDetails")) {
        result.success = false;
        result.error_detail = "get_clipboard failed (clipboard may require user gesture).";
        return result;
    }
    if (!eval_response.contains("result") || !eval_response["result"].contains("result")) {
        result.success = false;
        result.error_detail = "Runtime.evaluate did not return result.";
        return result;
    }
    const json &res = eval_response["result"]["result"];
    if (res.contains("value") && res["value"].is_string()) {
        result.html = res["value"].get<std::string>();
    }
    result.success = true;
    return result;
}

browser_driver::DriverResult set_clipboard(const std::string &text) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "set_clipboard failed.";
        return result;
    }

    std::string escaped = json(text).dump();
    json eval_params;
    eval_params["expression"] = "navigator.clipboard.writeText(" + escaped + ")";
    eval_params["awaitPromise"] = true;
    if (global_state.current_execution_context_id != 0) {
        eval_params["contextId"] = global_state.current_execution_context_id;
    }
    json eval_response = send_command("Runtime.evaluate", eval_params,
                                      global_state.current_session_id, 5000);

    if (eval_response.contains("result") && eval_response["result"].contains("exceptionDetails")) {
        result.success = false;
        result.error_detail = "set_clipboard failed (clipboard may require user gesture).";
        result.message = "set_clipboard failed.";
        return result;
    }
    result.success = true;
    result.message = "Clipboard set.";
    return result;
}

// --- get_network_requests ---

browser_driver::GetNetworkRequestsResult get_network_requests() {
    browser_driver::GetNetworkRequestsResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        return result;
    }

    if (!global_state.network_enabled) {
        send_command("Network.enable", json::object(), global_state.current_session_id, 5000);
        global_state.network_enabled = true;
    }

    for (int drain = 0; drain < 5; drain++) {
        service_websocket(20);
    }

    std::lock_guard<std::mutex> lock(global_state.network_mutex);
    result.requests = global_state.network_requests;
    result.success = true;
    return result;
}

// --- set_geolocation, set_user_agent ---

browser_driver::DriverResult set_geolocation(double latitude, double longitude, double accuracy) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "set_geolocation failed.";
        return result;
    }

    json params;
    params["latitude"] = latitude;
    params["longitude"] = longitude;
    if (accuracy > 0) {
        params["accuracy"] = accuracy;
    }
    json response = send_command("Emulation.setGeolocationOverride", params,
                                 global_state.current_session_id, 5000);
    if (response.contains("error") && response["error"].is_string()) {
        result.success = false;
        result.error_detail = response["error"].get<std::string>();
        result.message = "set_geolocation failed.";
        return result;
    }
    result.success = true;
    result.message = "Geolocation set.";
    return result;
}

browser_driver::DriverResult set_user_agent(const std::string &user_agent_string) {
    browser_driver::DriverResult result;

    if (!global_state.connected) {
        result.success = false;
        result.error_detail = "No active browser. Call open_browser first.";
        result.message = "set_user_agent failed.";
        return result;
    }

    json params;
    params["userAgent"] = user_agent_string;
    json response = send_command("Network.setUserAgentOverride", params, "", 5000);
    if (response.contains("error") && response["error"].is_string()) {
        result.success = false;
        result.error_detail = response["error"].get<std::string>();
        result.message = "set_user_agent failed.";
        return result;
    }
    result.success = true;
    result.message = "User agent set.";
    return result;
}

// --- is_visible, get_element_bounding_box ---

browser_driver::DriverResult is_visible(const std::string &selector, bool &out_visible) {
    browser_driver::DriverResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        result.message = "is_visible failed.";
        return result;
    }

    std::string escaped_selector = json(selector).dump();
    std::string script = "(function(){ var el=document.querySelector(" + escaped_selector + ");"
        "if(!el) return false; var r=el.getBoundingClientRect();"
        "return r.width>0 && r.height>0 && window.getComputedStyle(el).visibility!='hidden' && window.getComputedStyle(el).display!='none'; })();";
    json eval_params;
    eval_params["expression"] = script;
    eval_params["returnByValue"] = true;
    if (global_state.current_execution_context_id != 0) {
        eval_params["contextId"] = global_state.current_execution_context_id;
    }
    json eval_response = send_command("Runtime.evaluate", eval_params,
                                      global_state.current_session_id, 5000);

    if (eval_response.contains("result") && eval_response["result"].contains("exceptionDetails")) {
        result.success = false;
        result.error_detail = "Element not found or error: " + selector;
        result.message = "is_visible failed.";
        return result;
    }
    if (!eval_response.contains("result") || !eval_response["result"].contains("result")) {
        result.success = false;
        result.error_detail = "Runtime.evaluate did not return result.";
        result.message = "is_visible failed.";
        return result;
    }
    const json &res = eval_response["result"]["result"];
    out_visible = (res.contains("value") && res["value"].is_boolean() && res["value"].get<bool>());
    result.success = true;
    result.message = out_visible ? "Element is visible." : "Element is not visible.";
    return result;
}

browser_driver::BoundingBoxResult get_element_bounding_box(const std::string &selector) {
    browser_driver::BoundingBoxResult result;

    if (!global_state.connected || global_state.current_session_id.empty()) {
        result.success = false;
        result.error_detail = "No active browser session. Call open_browser first.";
        return result;
    }

    std::string escaped_selector = json(selector).dump();
    std::string script = "(function(){ var el=document.querySelector(" + escaped_selector + ");"
        "if(!el) return null; var r=el.getBoundingClientRect();"
        "return {x:r.x,y:r.y,width:r.width,height:r.height}; })();";
    json eval_params;
    eval_params["expression"] = script;
    eval_params["returnByValue"] = true;
    if (global_state.current_execution_context_id != 0) {
        eval_params["contextId"] = global_state.current_execution_context_id;
    }
    json eval_response = send_command("Runtime.evaluate", eval_params,
                                      global_state.current_session_id, 5000);

    if (eval_response.contains("result") && eval_response["result"].contains("exceptionDetails")) {
        result.success = false;
        result.error_detail = "Element not found: " + selector;
        return result;
    }
    if (!eval_response.contains("result") || !eval_response["result"].contains("result")) {
        result.success = false;
        result.error_detail = "Runtime.evaluate did not return result.";
        return result;
    }
    const json &res = eval_response["result"]["result"];
    if (!res.contains("value") || !res["value"].is_object()) {
        result.success = false;
        result.error_detail = "No bounding rect.";
        return result;
    }
    const json &val = res["value"];
    if (val.contains("x") && val["x"].is_number()) {
        result.x = val["x"].get<double>();
    }
    if (val.contains("y") && val["y"].is_number()) {
        result.y = val["y"].get<double>();
    }
    if (val.contains("width") && val["width"].is_number()) {
        result.width = val["width"].get<double>();
    }
    if (val.contains("height") && val["height"].is_number()) {
        result.height = val["height"].get<double>();
    }
    result.success = true;
    return result;
}

ConnectionState &get_state() {
    return global_state;
}

} // namespace cdp_driver
