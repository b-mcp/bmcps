#ifndef BMCPS_CDP_CHROME_LAUNCH_HPP
#define BMCPS_CDP_CHROME_LAUNCH_HPP

// Chrome browser launch and port discovery via DevToolsActivePort file.

#include <string>
#include <vector>

#include "browser/browser_driver_abi.hpp"

namespace cdp_chrome_launch {

// Result of launching Chrome and discovering the debug port.
struct ChromeLaunchResult {
    bool success = false;
    int process_id = -1;
    int debug_port = -1;
    std::string websocket_debugger_url;
    std::string user_data_directory;
    std::string error_message;
};

// Fixed port and profile so we always use the same Chrome instance.
static constexpr int BMCPS_FIXED_DEBUG_PORT = 9222;
static const char BMCPS_FIXED_USER_DATA_DIR[] = "/tmp/bmcps_chrome_profile";

// If Chrome is already running (DevToolsActivePort exists in profile dir),
// returns its WebSocket URL. Otherwise returns empty string.
std::string try_get_existing_websocket_url(const std::string &user_data_directory);

// Launch Chrome with remote debugging on the fixed port and user-data-dir.
ChromeLaunchResult launch_chrome(const browser_driver::OpenBrowserOptions &options = {});

// Build the command-line arguments for launching Chrome.
struct ChromeCommandLine {
    std::string executable_path;
    std::vector<std::string> arguments;
};
ChromeCommandLine build_chrome_command_line(const std::string &user_data_directory, int port,
                                             const browser_driver::OpenBrowserOptions &options = {});

// Find the Chrome executable on the system (platform-specific search).
std::string find_chrome_executable();

// Parse the DevToolsActivePort file to extract the debug port.
// The file typically contains the port on the first line and a path/token on the second.
// Returns the port number, or -1 on failure.
int parse_devtools_active_port(const std::string &file_path);

// Build the WebSocket debugger URL from the port and browser id.
std::string build_websocket_url(int port, const std::string &browser_path);

} // namespace cdp_chrome_launch

#endif // BMCPS_CDP_CHROME_LAUNCH_HPP
