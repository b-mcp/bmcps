#ifndef BMCPS_CDP_DRIVER_HPP
#define BMCPS_CDP_DRIVER_HPP

// CDP (Chrome DevTools Protocol) driver.
// Manages the WebSocket connection to Chrome, Target/session routing,
// and provides high-level functions for browser automation.

#include <nlohmann/json.hpp>
#include <string>
#include <map>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <vector>

#include "browser/browser_driver_abi.hpp"

struct lws_context;
struct lws;

namespace cdp_driver {

using json = nlohmann::json;

// State of the CDP connection.
struct ConnectionState {
    bool connected = false;
    bool shutting_down = false;
    struct lws_context *websocket_context = nullptr;
    struct lws *websocket_connection = nullptr;

    // Chrome process info
    int chrome_process_id = -1;
    std::string user_data_directory;

    // CDP message ID counter (incremented for each request).
    int next_message_id = 1;

    // Current active target and session.
    std::string current_target_id;
    std::string current_session_id;

    // Pending request map: message id -> response JSON (filled when response arrives).
    std::map<int, json> pending_responses;
    std::mutex pending_mutex;
    std::condition_variable pending_condition;

    // Buffer for incoming WebSocket data.
    std::string receive_buffer;
};

// Initialize the CDP driver (set up global state). Call once at startup.
void initialize();

// Connect to Chrome via WebSocket at the given URL.
// Returns true on success.
bool connect(const std::string &websocket_url);

// Disconnect and clean up.
void disconnect();

// Send a CDP command and wait for the response (blocking, with timeout).
// If session_id is non-empty, the command is routed to that session.
// Returns the response JSON, or an error object if timed out / failed.
json send_command(const std::string &method, const json &params,
                  const std::string &session_id = "", int timeout_milliseconds = 10000);

// Run the WebSocket event loop for a given duration (milliseconds).
// This must be called periodically to process incoming messages.
void service_websocket(int timeout_milliseconds);

// --- High-level browser operations (using browser_driver_abi types) ---

// Open the browser: launch Chrome, connect via CDP, discover targets,
// attach to a default tab. Stores current_target_id and current_session_id.
browser_driver::DriverResult open_browser();

// List all page-type targets (tabs).
browser_driver::TabListResult list_tabs();

// Navigate the current tab to the given URL.
browser_driver::NavigateResult navigate(const std::string &url);

// Get the connection state (for introspection / testing).
ConnectionState &get_state();

} // namespace cdp_driver

#endif // BMCPS_CDP_DRIVER_HPP
