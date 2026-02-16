// BMCP Server – Browser Model Context Protocol Server
// Entry point: stdio MCP server loop.
//
// Reads JSON-RPC 2.0 messages from stdin, dispatches them, writes responses to stdout.
// Logs go to stderr (permitted by MCP spec).

#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <csignal>

#include "tool_handlers/tool_handlers.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

using json = nlohmann::json;

// Forward declarations from mcp_stdio and mcp_dispatch namespaces.
namespace mcp_stdio {
    std::string read_message();
    void write_message(const std::string &json_string);
    void log_message(const std::string &message);
}

namespace mcp_dispatch {
    json dispatch_message(const json &message);
}

// Global flag for graceful shutdown.
static volatile bool shutdown_requested = false;

static void signal_handler(int signal_number) {
    (void)signal_number;
    shutdown_requested = true;
}

int main() {
    std::cerr << "[bmcps] bmcps – Browser MCP Server, build " << __DATE__ << " " << __TIME__ << std::endl;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    cdp_driver::initialize();
    tool_handlers::register_all_tools();

    mcp_stdio::log_message("BMCP Server started. Waiting for MCP messages on stdin.");

    // Main message loop: read from stdin, dispatch, write to stdout.
    while (!shutdown_requested) {
        std::string raw_message = mcp_stdio::read_message();

        if (raw_message.empty()) {
            // EOF on stdin means the client disconnected.
            debug_log::log("EOF on stdin. Shutting down, will disconnect and kill browser.");
            mcp_stdio::log_message("EOF on stdin. Shutting down.");
            break;
        }

        // Parse the JSON message.
        json parsed_message;
        try {
            parsed_message = json::parse(raw_message);
        } catch (const json::parse_error &error) {
            mcp_stdio::log_message("Failed to parse incoming JSON: " + std::string(error.what()));
            // Send a parse error response (no request id available).
            json error_response;
            error_response["jsonrpc"] = "2.0";
            error_response["id"] = nullptr;
            error_response["error"]["code"] = -32700;
            error_response["error"]["message"] = "Parse error";
            mcp_stdio::write_message(error_response.dump());
            continue;
        }

        // Dispatch the message.
        json response = mcp_dispatch::dispatch_message(parsed_message);

        // Notifications return null (no response needed).
        if (response.is_null()) {
            continue;
        }

        // Write the response to stdout.
        mcp_stdio::write_message(response.dump());
    }

    debug_log::log("Calling disconnect() (cleanup), browser process will be killed if connected.");
    cdp_driver::disconnect();
    mcp_stdio::log_message("BMCP Server shut down.");

    return 0;
}
