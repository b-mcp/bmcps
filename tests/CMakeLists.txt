# Test executables for bmcps.

# --- Unit / parameter tests ---

set(TEST_SOURCES
    test_runner.cpp
    test_open_browser.cpp
    test_navigate.cpp
)

add_executable(bmcps_test ${TEST_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/browser/cdp/cdp_chrome_launch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/platform/linux/platform_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/protocol/json_rpc.cpp
)

target_include_directories(bmcps_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../source
    ${LIBWEBSOCKETS_INCLUDE_DIRS}
)

target_link_libraries(bmcps_test PRIVATE
    nlohmann_json::nlohmann_json
    ${LIBWEBSOCKETS_LIBRARIES}
)

target_link_directories(bmcps_test PRIVATE
    ${LIBWEBSOCKETS_LIBRARY_DIRS}
)

# --- Smoke E2E test (optional, requires a running display / Chrome) ---

add_executable(bmcps_smoke_test
    test_smoke_e2e.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/browser/cdp/cdp_chrome_launch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/browser/cdp/cdp_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/platform/linux/platform_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/protocol/json_rpc.cpp
)

target_include_directories(bmcps_smoke_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../source
    ${LIBWEBSOCKETS_INCLUDE_DIRS}
)

target_link_libraries(bmcps_smoke_test PRIVATE
    nlohmann_json::nlohmann_json
    ${LIBWEBSOCKETS_LIBRARIES}
)

target_link_directories(bmcps_smoke_test PRIVATE
    ${LIBWEBSOCKETS_LIBRARY_DIRS}
)

# Don't build smoke test by default (needs Chrome + display).
set_target_properties(bmcps_smoke_test PROPERTIES EXCLUDE_FROM_ALL TRUE)
