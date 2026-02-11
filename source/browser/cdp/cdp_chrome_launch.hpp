#ifndef BMCPS_CDP_CHROME_LAUNCH_HPP
#define BMCPS_CDP_CHROME_LAUNCH_HPP

// Chrome browser launch and port discovery via DevToolsActivePort file.

#include <string>
#include <vector>

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

// Launch Chrome with remote debugging enabled.
// Creates a temporary user-data-dir, starts Chrome with --remote-debugging-port=0,
// waits for DevToolsActivePort file, and builds the WebSocket URL.
ChromeLaunchResult launch_chrome();

// Build the command-line arguments for launching Chrome.
// Exposed separately for testability (argv check without actually spawning).
struct ChromeCommandLine {
    std::string executable_path;
    std::vector<std::string> arguments;
};
ChromeCommandLine build_chrome_command_line(const std::string &user_data_directory);

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
