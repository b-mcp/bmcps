cmake_minimum_required(VERSION 3.16)
project(bmcps LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Dependencies ---

# nlohmann/json (header-only, fetched automatically)
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# libwebsockets (installed on the system via apt: libwebsockets-dev)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBWEBSOCKETS REQUIRED libwebsockets)

# --- Main executable ---

set(BMCPS_SOURCES
    source/main.cpp
    source/utils/debug_log.cpp
    source/utils/utf8_sanitize.cpp
    source/mcp/mcp_stdio.cpp
    source/mcp/mcp_dispatch.cpp
    source/mcp/mcp_tools.cpp
    source/protocol/json_rpc.cpp
    source/browser/cdp/cdp_driver.cpp
    source/browser/cdp/cdp_chrome_launch.cpp
    source/platform/linux/platform_linux.cpp
    source/tool_handlers/tool_handlers.cpp
    source/tool_handlers/tool_open_browser.cpp
    source/tool_handlers/tool_close_browser.cpp
    source/tool_handlers/tool_list_tabs.cpp
    source/tool_handlers/tool_new_tab.cpp
    source/tool_handlers/tool_switch_tab.cpp
    source/tool_handlers/tool_close_tab.cpp
    source/tool_handlers/tool_navigate.cpp
    source/tool_handlers/tool_navigate_back.cpp
    source/tool_handlers/tool_navigate_forward.cpp
    source/tool_handlers/tool_refresh.cpp
    source/tool_handlers/tool_get_navigation_history.cpp
    source/tool_handlers/tool_capture_screenshot.cpp
    source/tool_handlers/tool_get_console_messages.cpp
    source/tool_handlers/tool_list_interactive_elements.cpp
    source/tool_handlers/tool_fill_field.cpp
    source/tool_handlers/tool_click_element.cpp
    source/tool_handlers/tool_click_at_coordinates.cpp
    source/tool_handlers/tool_scroll.cpp
    source/tool_handlers/tool_resize_browser.cpp
    source/tool_handlers/tool_evaluate_javascript.cpp
    source/tool_handlers/tool_hover_element.cpp
    source/tool_handlers/tool_double_click_element.cpp
    source/tool_handlers/tool_right_click_element.cpp
    source/tool_handlers/tool_drag_and_drop.cpp
    source/tool_handlers/tool_drag_from_to.cpp
    source/tool_handlers/tool_get_page_source.cpp
    source/tool_handlers/tool_get_outer_html.cpp
    source/tool_handlers/tool_send_keys.cpp
    source/tool_handlers/tool_key_press.cpp
    source/tool_handlers/tool_key_down.cpp
    source/tool_handlers/tool_key_up.cpp
    source/tool_handlers/tool_wait.cpp
    source/tool_handlers/tool_wait_for_selector.cpp
    source/tool_handlers/tool_wait_for_navigation.cpp
    source/tool_handlers/tool_get_cookies.cpp
    source/tool_handlers/tool_set_cookie.cpp
    source/tool_handlers/tool_clear_cookies.cpp
    source/tool_handlers/tool_get_dialog_message.cpp
    source/tool_handlers/tool_accept_dialog.cpp
    source/tool_handlers/tool_dismiss_dialog.cpp
    source/tool_handlers/tool_send_prompt_value.cpp
    source/tool_handlers/tool_upload_file.cpp
    source/tool_handlers/tool_list_frames.cpp
    source/tool_handlers/tool_switch_to_frame.cpp
    source/tool_handlers/tool_switch_to_main_frame.cpp
    source/tool_handlers/tool_get_storage.cpp
    source/tool_handlers/tool_set_storage.cpp
    source/tool_handlers/tool_get_clipboard.cpp
    source/tool_handlers/tool_set_clipboard.cpp
    source/tool_handlers/tool_get_network_requests.cpp
    source/tool_handlers/tool_set_geolocation.cpp
    source/tool_handlers/tool_set_user_agent.cpp
    source/tool_handlers/tool_is_visible.cpp
    source/tool_handlers/tool_get_element_bounding_box.cpp
)

add_executable(bmcps ${BMCPS_SOURCES})

target_include_directories(bmcps PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${LIBWEBSOCKETS_INCLUDE_DIRS}
)

target_link_libraries(bmcps PRIVATE
    nlohmann_json::nlohmann_json
    ${LIBWEBSOCKETS_LIBRARIES}
)

target_link_directories(bmcps PRIVATE
    ${LIBWEBSOCKETS_LIBRARY_DIRS}
)

# --- Tests ---

add_subdirectory(tests)
