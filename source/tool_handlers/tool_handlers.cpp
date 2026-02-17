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
namespace tool_get_console_messages { void register_tool(); }
namespace tool_list_interactive_elements { void register_tool(); }
namespace tool_fill_field { void register_tool(); }
namespace tool_click_element { void register_tool(); }
namespace tool_click_at_coordinates { void register_tool(); }
namespace tool_scroll { void register_tool(); }
namespace tool_resize_browser { void register_tool(); }

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
    tool_get_console_messages::register_tool();
    tool_list_interactive_elements::register_tool();
    tool_fill_field::register_tool();
    tool_click_element::register_tool();
    tool_click_at_coordinates::register_tool();
    tool_scroll::register_tool();
    tool_resize_browser::register_tool();
}

} // namespace tool_handlers
