#ifndef BMCPS_MCP_TOOLS_HPP
#define BMCPS_MCP_TOOLS_HPP

// MCP tool registry: registration, listing, and dispatch of tool calls.

#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include <vector>

namespace mcp_tools {

using json = nlohmann::json;

// A tool handler function: receives the arguments JSON, returns the result JSON
// (content array + isError flag, as per MCP spec).
using ToolHandler = std::function<json(const json &arguments)>;

// Description of a registered tool, matching the MCP tool schema.
struct ToolDefinition {
    std::string name;
    std::string description;
    json input_schema; // JSON Schema object
    ToolHandler handler;
};

// Register a tool. Call this during initialization for each tool.
void register_tool(const ToolDefinition &definition);

// Build the response payload for tools/list.
json build_tools_list_response();

// Dispatch a tools/call request. Returns the result payload (content + isError).
json dispatch_tool_call(const std::string &tool_name, const json &arguments);

// Get all registered tool definitions (for testing or introspection).
const std::vector<ToolDefinition> &get_registered_tools();

} // namespace mcp_tools

#endif // BMCPS_MCP_TOOLS_HPP
