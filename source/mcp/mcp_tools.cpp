#include "mcp/mcp_tools.hpp"

#include <algorithm>

namespace mcp_tools {

// Global tool registry (module-level, not class-based).
static std::vector<ToolDefinition> registered_tools;

void register_tool(const ToolDefinition &definition) {
    registered_tools.push_back(definition);
}

json build_tools_list_response() {
    json tools_array = json::array();
    for (const auto &tool : registered_tools) {
        json tool_entry;
        tool_entry["name"] = tool.name;
        tool_entry["description"] = tool.description;
        tool_entry["inputSchema"] = tool.input_schema;
        tools_array.push_back(tool_entry);
    }

    json result;
    result["tools"] = tools_array;
    return result;
}

json dispatch_tool_call(const std::string &tool_name, const json &arguments) {
    for (const auto &tool : registered_tools) {
        if (tool.name == tool_name) {
            return tool.handler(arguments);
        }
    }

    // Tool not found: return an error result.
    json error_content;
    error_content["type"] = "text";
    error_content["text"] = "Unknown tool: " + tool_name;

    json result;
    result["content"] = json::array({error_content});
    result["isError"] = true;
    return result;
}

const std::vector<ToolDefinition> &get_registered_tools() {
    return registered_tools;
}

} // namespace mcp_tools
