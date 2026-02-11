#include <iostream>
#include <string>

// MCP stdio transport: reading JSON messages from stdin and writing to stdout.
// Uses brace-counting with string/escape awareness for framing,
// so it works both with newline-delimited and streamed JSON.

namespace mcp_stdio {

// Read a single complete JSON object from stdin.
// Uses brace-counting approach: tracks { } depth, respecting strings and escapes.
// Returns the raw JSON string, or empty string on EOF / error.
std::string read_message() {
    std::string buffer;
    int brace_depth = 0;
    bool inside_string = false;
    bool escape_next = false;
    bool started = false;

    char character;
    while (std::cin.get(character)) {
        // Skip whitespace before the opening brace.
        if (!started) {
            if (character == '{') {
                started = true;
                brace_depth = 1;
                buffer += character;
            }
            // Ignore anything before the first '{' (whitespace, newlines, etc.)
            continue;
        }

        buffer += character;

        if (escape_next) {
            escape_next = false;
            continue;
        }

        if (character == '\\' && inside_string) {
            escape_next = true;
            continue;
        }

        if (character == '"') {
            inside_string = !inside_string;
            continue;
        }

        if (inside_string) {
            continue;
        }

        if (character == '{') {
            brace_depth++;
        } else if (character == '}') {
            brace_depth--;
            if (brace_depth == 0) {
                // Complete JSON object received.
                return buffer;
            }
        }
    }

    // EOF reached without a complete message.
    return "";
}

// Write a JSON message to stdout, followed by a newline (for compatibility).
void write_message(const std::string &json_string) {
    std::cout << json_string << "\n";
    std::cout.flush();
}

// Write a log message to stderr (MCP spec allows this for logging).
void log_message(const std::string &message) {
    std::cerr << "[bmcps] " << message << std::endl;
}

} // namespace mcp_stdio
