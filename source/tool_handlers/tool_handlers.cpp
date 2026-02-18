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
namespace tool_evaluate_javascript { void register_tool(); }
namespace tool_hover_element { void register_tool(); }
namespace tool_double_click_element { void register_tool(); }
namespace tool_right_click_element { void register_tool(); }
namespace tool_drag_and_drop { void register_tool(); }
namespace tool_drag_from_to { void register_tool(); }
namespace tool_get_page_source { void register_tool(); }
namespace tool_get_outer_html { void register_tool(); }
namespace tool_send_keys { void register_tool(); }
namespace tool_key_press { void register_tool(); }
namespace tool_key_down { void register_tool(); }
namespace tool_key_up { void register_tool(); }
namespace tool_wait { void register_tool(); }
namespace tool_wait_for_selector { void register_tool(); }
namespace tool_wait_for_navigation { void register_tool(); }
namespace tool_get_cookies { void register_tool(); }
namespace tool_set_cookie { void register_tool(); }
namespace tool_clear_cookies { void register_tool(); }
namespace tool_get_dialog_message { void register_tool(); }
namespace tool_accept_dialog { void register_tool(); }
namespace tool_dismiss_dialog { void register_tool(); }
namespace tool_send_prompt_value { void register_tool(); }
namespace tool_upload_file { void register_tool(); }
namespace tool_list_frames { void register_tool(); }
namespace tool_switch_to_frame { void register_tool(); }
namespace tool_switch_to_main_frame { void register_tool(); }
namespace tool_get_storage { void register_tool(); }
namespace tool_set_storage { void register_tool(); }
namespace tool_get_clipboard { void register_tool(); }
namespace tool_set_clipboard { void register_tool(); }
namespace tool_get_network_requests { void register_tool(); }
namespace tool_set_geolocation { void register_tool(); }
namespace tool_set_user_agent { void register_tool(); }
namespace tool_is_visible { void register_tool(); }
namespace tool_get_element_bounding_box { void register_tool(); }

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
    tool_evaluate_javascript::register_tool();
    tool_hover_element::register_tool();
    tool_double_click_element::register_tool();
    tool_right_click_element::register_tool();
    tool_drag_and_drop::register_tool();
    tool_drag_from_to::register_tool();
    tool_get_page_source::register_tool();
    tool_get_outer_html::register_tool();
    tool_send_keys::register_tool();
    tool_key_press::register_tool();
    tool_key_down::register_tool();
    tool_key_up::register_tool();
    tool_wait::register_tool();
    tool_wait_for_selector::register_tool();
    tool_wait_for_navigation::register_tool();
    tool_get_cookies::register_tool();
    tool_set_cookie::register_tool();
    tool_clear_cookies::register_tool();
    tool_get_dialog_message::register_tool();
    tool_accept_dialog::register_tool();
    tool_dismiss_dialog::register_tool();
    tool_send_prompt_value::register_tool();
    tool_upload_file::register_tool();
    tool_list_frames::register_tool();
    tool_switch_to_frame::register_tool();
    tool_switch_to_main_frame::register_tool();
    tool_get_storage::register_tool();
    tool_set_storage::register_tool();
    tool_get_clipboard::register_tool();
    tool_set_clipboard::register_tool();
    tool_get_network_requests::register_tool();
    tool_set_geolocation::register_tool();
    tool_set_user_agent::register_tool();
    tool_is_visible::register_tool();
    tool_get_element_bounding_box::register_tool();
}

} // namespace tool_handlers
