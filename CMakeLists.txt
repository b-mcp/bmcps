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
