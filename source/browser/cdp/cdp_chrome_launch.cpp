#include "browser/cdp/cdp_chrome_launch.hpp"
#include "platform/platform_abi.hpp"
#include "utils/debug_log.hpp"

#include <filesystem>
#include <sstream>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <unistd.h>

namespace cdp_chrome_launch {

// Well-known Chrome executable paths on Linux.
static const std::vector<std::string> LINUX_CHROME_PATHS = {
    "google-chrome",
    "google-chrome-stable",
    "/usr/bin/google-chrome",
    "/usr/bin/google-chrome-stable",
    "/usr/bin/chromium-browser",
    "/usr/bin/chromium",
    "/snap/bin/chromium",
};

std::string find_chrome_executable() {
    // Try each known path; for bare names, check if they exist on PATH.
    for (const auto &candidate : LINUX_CHROME_PATHS) {
        if (candidate.find('/') != std::string::npos) {
            // Absolute path: check if the file exists and is executable.
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
        } else {
            // Bare name: use 'which' equivalent via PATH search.
            const char *path_environment = std::getenv("PATH");
            if (path_environment == nullptr) {
                continue;
            }
            std::istringstream path_stream(path_environment);
            std::string directory;
            while (std::getline(path_stream, directory, ':')) {
                std::string full_path = directory + "/" + candidate;
                if (std::filesystem::exists(full_path)) {
                    return full_path;
                }
            }
        }
    }
    return "";
}

ChromeCommandLine build_chrome_command_line(const std::string &user_data_directory, int port) {
    ChromeCommandLine command_line;
    command_line.executable_path = find_chrome_executable();
    command_line.arguments = {
        "--remote-debugging-port=" + std::to_string(port),
        "--remote-allow-origins=*",
        "--user-data-dir=" + user_data_directory,
    };
    if (getuid() == 0) {
        command_line.arguments.push_back("--no-sandbox");
    }
    std::vector<std::string> rest = {
        "--no-first-run",
        "--no-default-browser-check",
        "--disable-background-networking",
        "--disable-client-side-phishing-detection",
        "--disable-default-apps",
        "--disable-extensions",
        "--disable-hang-monitor",
        "--disable-popup-blocking",
        "--disable-prompt-on-repost",
        "--disable-sync",
        "--disable-translate",
        "--metrics-recording-only",
        "--safebrowsing-disable-auto-update",
        "about:blank",
    };
    command_line.arguments.insert(command_line.arguments.end(), rest.begin(), rest.end());
    return command_line;
}

int parse_devtools_active_port(const std::string &file_path) {
    std::string contents;
    if (!platform::read_file_contents(file_path, contents)) {
        return -1;
    }

    // The first line of DevToolsActivePort contains the port number.
    std::istringstream line_stream(contents);
    std::string first_line;
    if (!std::getline(line_stream, first_line) || first_line.empty()) {
        return -1;
    }

    try {
        int port = std::stoi(first_line);
        if (port > 0 && port <= 65535) {
            return port;
        }
    } catch (...) {
        // Parsing failed.
    }

    return -1;
}

std::string build_websocket_url(int port, const std::string &browser_path) {
    // The standard browser-level WebSocket endpoint.
    // If browser_path is empty, we use the /json/version endpoint format.
    if (browser_path.empty()) {
        return "ws://127.0.0.1:" + std::to_string(port) + "/devtools/browser";
    }
    // Ensure exactly one leading slash to avoid // in URL.
    std::string path = browser_path;
    while (!path.empty() && path[0] == '/') {
        path.erase(0, 1);
    }
    if (!path.empty()) {
        path = "/" + path;
    } else {
        path = "/devtools/browser";
    }
    return "ws://127.0.0.1:" + std::to_string(port) + path;
}

std::string try_get_existing_websocket_url(const std::string &user_data_directory) {
    std::string active_port_file = user_data_directory + "/DevToolsActivePort";
    if (!std::filesystem::exists(active_port_file)) {
        return "";
    }
    int port = parse_devtools_active_port(active_port_file);
    if (port <= 0) {
        return "";
    }
    std::string file_contents;
    if (!platform::read_file_contents(active_port_file, file_contents)) {
        return "";
    }
    std::istringstream line_stream(file_contents);
    std::string first_line, second_line;
    std::getline(line_stream, first_line);
    std::getline(line_stream, second_line);
    std::string browser_path;
    if (!second_line.empty()) {
        browser_path = second_line;
        while (!browser_path.empty() && browser_path[0] == '/') {
            browser_path.erase(0, 1);
        }
        if (!browser_path.empty()) {
            browser_path = "/" + browser_path;
        }
    }
    return build_websocket_url(port, browser_path);
}

ChromeLaunchResult launch_chrome() {
    ChromeLaunchResult result;

    debug_log::log("Chrome launch starting…");

    std::string profile_directory = "/tmp/bmcps_chrome_profile_" + std::to_string(getpid());
    std::filesystem::create_directories(profile_directory);
    result.user_data_directory = profile_directory;

    ChromeCommandLine command_line = build_chrome_command_line(profile_directory, 0);

    if (command_line.executable_path.empty()) {
        result.error_message = "Could not find Chrome executable on this system. "
                               "Install google-chrome or chromium and ensure it is on PATH.";
        return result;
    }

    // Spawn Chrome.
    platform::SpawnResult spawn_result = platform::spawn_process(
        command_line.executable_path, command_line.arguments);

    if (!spawn_result.success) {
        result.error_message = "Failed to spawn Chrome: " + spawn_result.error_message;
        return result;
    }

    result.process_id = spawn_result.process_id;

    std::string active_port_file = profile_directory + "/DevToolsActivePort";
    bool port_file_appeared = platform::wait_for_file(active_port_file, 15000);

    if (!port_file_appeared) {
        debug_log::log("launch_chrome: Timed out waiting for DevToolsActivePort, killing Chrome pid=" + std::to_string(result.process_id));
        result.error_message = "Timed out waiting for DevToolsActivePort file at: " + active_port_file;
        platform::kill_process(result.process_id);
        return result;
    }

    // Parse the port from the file.
    result.debug_port = parse_devtools_active_port(active_port_file);
    if (result.debug_port <= 0) {
        debug_log::log("launch_chrome: Failed to parse port from DevToolsActivePort, killing Chrome pid=" + std::to_string(result.process_id));
        result.error_message = "Failed to parse debug port from DevToolsActivePort file.";
        platform::kill_process(result.process_id);
        return result;
    }

    // Read the second line for the browser path (used in WebSocket URL).
    std::string file_contents;
    platform::read_file_contents(active_port_file, file_contents);
    std::istringstream line_stream(file_contents);
    std::string first_line, second_line;
    std::getline(line_stream, first_line);
    std::getline(line_stream, second_line);

    debug_log::log("DevToolsActivePort read, port=" + std::to_string(result.debug_port) + ", second_line=" + second_line);

    if (!second_line.empty()) {
        // Normalize path to exactly one leading slash (Chrome may write with or without).
        std::string browser_path = second_line;
        while (!browser_path.empty() && browser_path[0] == '/') {
            browser_path.erase(0, 1);
        }
        if (!browser_path.empty()) {
            browser_path = "/" + browser_path;
        }
        debug_log::log("WebSocket path normalized: " + browser_path);
        result.websocket_debugger_url = build_websocket_url(result.debug_port, browser_path);
    } else {
        result.websocket_debugger_url = build_websocket_url(result.debug_port, "");
    }

    debug_log::log("WebSocket URL: " + result.websocket_debugger_url);

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    debug_log::log("Chrome launch: waited 1500 ms for socket to be ready.");

    result.success = true;
    std::cerr << "[bmcps] Chrome launched (pid=" << result.process_id
              << ", port=" << result.debug_port << ")" << std::endl;
    return result;
}

} // namespace cdp_chrome_launch
