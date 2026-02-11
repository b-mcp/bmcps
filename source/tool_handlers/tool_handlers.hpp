#ifndef BMCPS_TOOL_HANDLERS_HPP
#define BMCPS_TOOL_HANDLERS_HPP

// Tool handler registration.
// Each tool_*.cpp file provides a register function that is called during startup.

namespace tool_handlers {

// Register all available tool handlers with the MCP tool registry.
void register_all_tools();

} // namespace tool_handlers

#endif // BMCPS_TOOL_HANDLERS_HPP
