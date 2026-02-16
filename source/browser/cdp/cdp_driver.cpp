#include "browser/cdp/cdp_driver.hpp"
#include "browser/cdp/cdp_chrome_launch.hpp"
#include "platform/platform_abi.hpp"
#include "utils/debug_log.hpp"

#include <libwebsockets.h>
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>

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
                    // CDP event (method without id). Log for now; can be extended later
                    // with an event handler registry.
                    if (message.contains("method")) {
                        std::cerr << "[bmcps] CDP event: " << message["method"].get<std::string>() << std::endl;
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

browser_driver::DriverResult open_browser() {
    browser_driver::DriverResult result;
    bool connected = false;

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

    if (!connected) {
        cdp_chrome_launch::ChromeLaunchResult launch_result = cdp_chrome_launch::launch_chrome();
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
    for (const auto &target_info : target_infos) {
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
        if (target_info.contains("type")) {
            tab.type = target_info["type"].get<std::string>();
        }
        result.tabs.push_back(tab);
    }

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

ConnectionState &get_state() {
    return global_state;
}

} // namespace cdp_driver
