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

ConnectionState &get_state() {
    return global_state;
}

} // namespace cdp_driver
