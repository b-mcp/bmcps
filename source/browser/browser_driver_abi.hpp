#ifndef BMCPS_BROWSER_DRIVER_ABI_HPP
#define BMCPS_BROWSER_DRIVER_ABI_HPP

// Browser driver abstraction interface.
// Each browser driver (CDP for Chrome, potentially others in the future)
// implements these functions. This keeps the tool_handlers layer decoupled
// from any particular browser protocol.

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace browser_driver {

// Optional settings when opening the browser (launch arguments).
// Defaults: disable_translate true, so the translate bar is hidden unless opted in.
struct OpenBrowserOptions {
    bool disable_translate = true;
};

// Information about a single browser tab / target.
struct TabInfo {
    std::string target_id;
    std::string title;
    std::string url;
    std::string type; // e.g. "page", "background_page", "service_worker"
};

// Result of a browser driver operation.
struct DriverResult {
    bool success = false;
    std::string message;
    std::string error_detail;
};

// Result of listing tabs.
struct TabListResult {
    bool success = false;
    std::vector<TabInfo> tabs;
    std::string error_detail;
};

// Result of navigation.
struct NavigateResult {
    bool success = false;
    std::string frame_id;
    std::string error_text; // CDP errorText if navigation failed
};

// Result of capturing a screenshot of the current tab.
struct CaptureScreenshotResult {
    bool success = false;
    std::string image_base64;
    std::string mime_type;   // e.g. "image/png"
    std::string error_detail;
};

// One entry in the current tab's navigation history (from CDP Page.getNavigationHistory).
struct NavigationHistoryEntry {
    int id = 0;
    std::string url;
    std::string title;
};

// Result of getting the current tab's navigation history.
struct NavigationHistoryResult {
    bool success = false;
    int current_index = 0;
    std::vector<NavigationHistoryEntry> entries;
    std::string error_detail;
};

// --- Console messages (get_console_messages) ---

// One console log entry (text is always sanitized UTF-8).
struct ConsoleEntry {
    int64_t timestamp_ms = 0;
    std::string level;
    std::string text;
};

// Time scope: discriminated union. Exactly one variant is active.
enum class TimeScopeType {
    None,
    LastDuration,
    Range,
    FromOnwards,
    Until
};

struct TimeScope {
    TimeScopeType type = TimeScopeType::None;
    // LastDuration: value (e.g. 5), unit ("milliseconds"|"seconds"|"minutes")
    int64_t last_duration_value = 0;
    std::string last_duration_unit;
    // Range / FromOnwards / Until: from_ms and/or to_ms (epoch ms)
    int64_t from_ms = 0;
    int64_t to_ms = 0;
};

// Count scope: always same shape. Applied after time and level filter.
struct CountScope {
    int max_entries = 500;
    std::string order; // "newest_first" | "oldest_first"
};

// Level scope: discriminated union.
enum class LevelScopeType {
    MinLevel,
    Only
};

struct LevelScope {
    LevelScopeType type = LevelScopeType::MinLevel;
    std::string level; // for MinLevel: "debug"|"log"|"info"|"warning"|"error"
    std::vector<std::string> levels; // for Only: non-empty list
};

struct GetConsoleMessagesOptions {
    TimeScope time_scope;
    CountScope count_scope;
    LevelScope level_scope;
};

// Time sync info: browser vs server time, for the caller.
struct TimeSyncInfo {
    int64_t browser_now_ms = 0;
    int64_t server_now_ms = 0;
    int64_t offset_ms = 0;
    int64_t round_trip_ms = 0;
};

// Result of get_console_messages.
struct ConsoleMessagesResult {
    bool success = false;
    std::vector<std::string> lines;
    std::string error_detail;
    bool truncated = false;
    int returned_count = 0;
    int total_matching = 0;
    TimeSyncInfo time_sync;
};

} // namespace browser_driver

#endif // BMCPS_BROWSER_DRIVER_ABI_HPP
