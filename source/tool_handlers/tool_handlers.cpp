// Forward declarations of individual tool registration functions.
// Each tool_*.cpp defines its own namespace with a register_tool() function.

namespace tool_open_browser { void register_tool(); }
namespace tool_list_tabs { void register_tool(); }
namespace tool_navigate { void register_tool(); }

namespace tool_handlers {

void register_all_tools() {
    tool_open_browser::register_tool();
    tool_list_tabs::register_tool();
    tool_navigate::register_tool();
}

} // namespace tool_handlers
