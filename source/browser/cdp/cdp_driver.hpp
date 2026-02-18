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
    bool connection_failed = false;
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

    // Console messages buffer (Runtime.consoleAPICalled for current tab).
    std::vector<browser_driver::ConsoleEntry> console_entries;
    std::mutex console_mutex;
    static constexpr size_t kConsoleEntriesMax = 20000;

    // Last JavaScript dialog (Page.javascriptDialogOpening): message and type.
    std::string last_dialog_message;
    std::string last_dialog_type;
    std::mutex dialog_mutex;

    // Frame execution context: map frame id to context id; empty = main.
    std::map<std::string, int> execution_context_id_by_frame_id;
    int current_execution_context_id = 0;  // 0 = use default (main frame)
    std::mutex frame_mutex;

    // Network requests buffer (Network.requestWillBeSent / responseReceived).
    std::vector<browser_driver::NetworkRequestEntry> network_requests;
    std::mutex network_mutex;
    static constexpr size_t kNetworkRequestsMax = 500;
    bool network_enabled = false;
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
browser_driver::DriverResult open_browser(const browser_driver::OpenBrowserOptions &options = {});

// List all page-type targets (tabs).
browser_driver::TabListResult list_tabs();

// Navigate the current tab to the given URL.
browser_driver::NavigateResult navigate(const std::string &url);

// Go back in the current tab's history.
browser_driver::DriverResult navigate_back();

// Go forward in the current tab's history.
browser_driver::DriverResult navigate_forward();

// Reload the current tab.
browser_driver::DriverResult refresh();

// Get the current tab's navigation history (entries and current index).
browser_driver::NavigationHistoryResult get_navigation_history();

// Create a new tab (optionally with URL) and attach to it as the current target.
browser_driver::DriverResult new_tab(const std::string &url = "about:blank");

// Switch to tab by 0-based index (page targets only). Returns success and attaches to that tab.
browser_driver::DriverResult switch_tab(int index);

// Close the current tab. If other page targets exist, attaches to the first one.
browser_driver::DriverResult close_tab();

// Capture a screenshot of the current tab. Returns base64 image data and mime type.
browser_driver::CaptureScreenshotResult capture_screenshot();

// Enable Runtime for the current session and clear console buffer. Call after attach.
void enable_console_for_session();

// Get console messages with time/level/count scope. Includes time_sync when possible.
browser_driver::ConsoleMessagesResult get_console_messages(
    const browser_driver::GetConsoleMessagesOptions &options);

// List form fields and clickable elements (label, placeholder, text, selector). Max ~100.
browser_driver::ListInteractiveElementsResult list_interactive_elements();

// Fill an input/textarea by selector. Optionally clear before typing.
browser_driver::DriverResult fill_field(const std::string &selector, const std::string &value,
                                        bool clear_first = true);

// Click an element by selector (uses box model + Input.dispatchMouseEvent, fallback element.click()).
browser_driver::DriverResult click_element(const std::string &selector);

// Click at viewport coordinates (e.g. canvas). x, y in CSS pixels.
browser_driver::DriverResult click_at_coordinates(int x, int y);

// Scroll: page (window) or element (selector). delta_x, delta_y in pixels.
browser_driver::DriverResult scroll(const browser_driver::ScrollScope &scroll_scope);

// Resize browser window (Browser domain, no session). width/height in pixels.
browser_driver::DriverResult set_window_bounds(int width, int height);

// Evaluate JavaScript in the page. Returns result serialized as JSON string.
browser_driver::EvaluateJavaScriptResult evaluate_javascript(const std::string &script,
                                                            int timeout_milliseconds = 10000);

// Hover over element by selector (mouse move to element center).
browser_driver::DriverResult hover_element(const std::string &selector);

// Double-click element by selector.
browser_driver::DriverResult double_click_element(const std::string &selector);

// Right-click element by selector.
browser_driver::DriverResult right_click_element(const std::string &selector);

// Drag from source to target by selectors; or by coordinates (for canvas etc.).
browser_driver::DriverResult drag_and_drop_selectors(const std::string &source_selector,
                                                     const std::string &target_selector);
browser_driver::DriverResult drag_from_to_coordinates(int x1, int y1, int x2, int y2);

// Page source and outer HTML.
browser_driver::GetPageSourceResult get_page_source();
browser_driver::GetPageSourceResult get_outer_html(const std::string &selector);

// Keyboard: send_keys (optional selector for focus), key_press, key_down, key_up.
browser_driver::DriverResult send_keys(const std::string &keys,
                                       const std::string &selector = "");
browser_driver::DriverResult key_press(const std::string &key);
browser_driver::DriverResult key_down(const std::string &key);
browser_driver::DriverResult key_up(const std::string &key);

// Wait: sleep seconds; wait for selector or navigation with timeout.
browser_driver::DriverResult wait_seconds(double seconds);
browser_driver::DriverResult wait_for_selector(const std::string &selector,
                                               int timeout_milliseconds);
browser_driver::DriverResult wait_for_navigation(int timeout_milliseconds);

// Cookies.
browser_driver::GetCookiesResult get_cookies(const std::string &url = "");
browser_driver::DriverResult set_cookie(const std::string &name, const std::string &value,
                                        const std::string &url = "",
                                        const std::string &domain = "",
                                        const std::string &path = "");
browser_driver::DriverResult clear_cookies();

// JavaScript dialog (alert/confirm/prompt). State is stored on Page.javascriptDialogOpening.
browser_driver::GetDialogMessageResult get_dialog_message();
browser_driver::DriverResult accept_dialog();
browser_driver::DriverResult dismiss_dialog();
browser_driver::DriverResult send_prompt_value(const std::string &text);

// File upload (file input by selector).
browser_driver::DriverResult upload_file(const std::string &selector,
                                         const std::string &file_path);

// Frames: list and switch execution context.
browser_driver::ListFramesResult list_frames();
browser_driver::DriverResult switch_to_frame(const std::string &frame_id_or_index);
browser_driver::DriverResult switch_to_main_frame();

// Storage (localStorage/sessionStorage).
browser_driver::GetPageSourceResult get_storage(const std::string &storage_type,
                                                const std::string &key = "");
browser_driver::DriverResult set_storage(const std::string &storage_type,
                                         const std::string &key,
                                         const std::string &value);

// Clipboard (via page script; may require user gesture in some contexts).
browser_driver::GetPageSourceResult get_clipboard();
browser_driver::DriverResult set_clipboard(const std::string &text);

// Network: enable and buffer requests; return list.
browser_driver::GetNetworkRequestsResult get_network_requests();

// Geolocation and user agent.
browser_driver::DriverResult set_geolocation(double latitude, double longitude,
                                             double accuracy = 0.0);
browser_driver::DriverResult set_user_agent(const std::string &user_agent_string);

// Element visibility and bounding box.
browser_driver::DriverResult is_visible(const std::string &selector, bool &out_visible);
browser_driver::BoundingBoxResult get_element_bounding_box(const std::string &selector);

// Get the connection state (for introspection / testing).
ConnectionState &get_state();

} // namespace cdp_driver

#endif // BMCPS_CDP_DRIVER_HPP
