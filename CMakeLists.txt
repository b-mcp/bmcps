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
    source/mcp/mcp_stdio.cpp
    source/mcp/mcp_dispatch.cpp
    source/mcp/mcp_tools.cpp
    source/protocol/json_rpc.cpp
    source/browser/cdp/cdp_driver.cpp
    source/browser/cdp/cdp_chrome_launch.cpp
    source/platform/linux/platform_linux.cpp
    source/tool_handlers/tool_handlers.cpp
    source/tool_handlers/tool_open_browser.cpp
    source/tool_handlers/tool_list_tabs.cpp
    source/tool_handlers/tool_navigate.cpp
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
