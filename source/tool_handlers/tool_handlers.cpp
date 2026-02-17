// Forward declarations of individual tool registration functions.
// Each tool_*.cpp defines its own namespace with a register_tool() function.

namespace tool_open_browser { void register_tool(); }
namespace tool_close_browser { void register_tool(); }
namespace tool_list_tabs { void register_tool(); }
namespace tool_new_tab { void register_tool(); }
namespace tool_switch_tab { void register_tool(); }
namespace tool_close_tab { void register_tool(); }
namespace tool_navigate { void register_tool(); }
namespace tool_navigate_back { void register_tool(); }
namespace tool_navigate_forward { void register_tool(); }
namespace tool_refresh { void register_tool(); }
namespace tool_get_navigation_history { void register_tool(); }
namespace tool_capture_screenshot { void register_tool(); }

namespace tool_handlers {

void register_all_tools() {
    tool_open_browser::register_tool();
    tool_close_browser::register_tool();
    tool_list_tabs::register_tool();
    tool_new_tab::register_tool();
    tool_switch_tab::register_tool();
    tool_close_tab::register_tool();
    tool_navigate::register_tool();
    tool_navigate_back::register_tool();
    tool_navigate_forward::register_tool();
    tool_refresh::register_tool();
    tool_get_navigation_history::register_tool();
    tool_capture_screenshot::register_tool();
}

} // namespace tool_handlers
