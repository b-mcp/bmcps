#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "browser/browser_driver_abi.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

static browser_driver::GetConsoleMessagesOptions parse_options(const json &arguments) {
    browser_driver::GetConsoleMessagesOptions options;

    if (arguments.contains("time_scope") && arguments["time_scope"].is_object()) {
        const json &time_scope = arguments["time_scope"];
        std::string type = time_scope.value("type", "none");
        if (type == "none") {
            options.time_scope.type = browser_driver::TimeScopeType::None;
        } else if (type == "last_duration") {
            options.time_scope.type = browser_driver::TimeScopeType::LastDuration;
            options.time_scope.last_duration_value = time_scope.value("value", 0);
            options.time_scope.last_duration_unit = time_scope.value("unit", "seconds");
        } else if (type == "range") {
            options.time_scope.type = browser_driver::TimeScopeType::Range;
            options.time_scope.from_ms = time_scope.value("from_ms", static_cast<int64_t>(0));
            options.time_scope.to_ms = time_scope.value("to_ms", static_cast<int64_t>(0));
        } else if (type == "from_onwards") {
            options.time_scope.type = browser_driver::TimeScopeType::FromOnwards;
            options.time_scope.from_ms = time_scope.value("from_ms", static_cast<int64_t>(0));
        } else if (type == "until") {
            options.time_scope.type = browser_driver::TimeScopeType::Until;
            options.time_scope.to_ms = time_scope.value("to_ms", static_cast<int64_t>(0));
        }
    }

    if (arguments.contains("count_scope") && arguments["count_scope"].is_object()) {
        const json &count_scope = arguments["count_scope"];
        options.count_scope.max_entries = count_scope.value("max_entries", 500);
        options.count_scope.order = count_scope.value("order", "newest_first");
    }

    if (arguments.contains("level_scope") && arguments["level_scope"].is_object()) {
        const json &level_scope = arguments["level_scope"];
        std::string type = level_scope.value("type", "min_level");
        if (type == "only") {
            options.level_scope.type = browser_driver::LevelScopeType::Only;
            if (level_scope.contains("levels") && level_scope["levels"].is_array()) {
                for (const auto &level_item : level_scope["levels"]) {
                    if (level_item.is_string()) {
                        options.level_scope.levels.push_back(level_item.get<std::string>());
                    }
                }
            }
        } else {
            options.level_scope.type = browser_driver::LevelScopeType::MinLevel;
            options.level_scope.level = level_scope.value("level", "info");
        }
    }

    return options;
}

static json handle_get_console_messages(const json &arguments) {
    debug_log::log("get_console_messages invoked");

    browser_driver::GetConsoleMessagesOptions options = parse_options(arguments);
    browser_driver::ConsoleMessagesResult messages_result = cdp_driver::get_console_messages(options);

    json result;

    if (!messages_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "Failed to get console messages: " + messages_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::ostringstream text_stream;
    text_stream << "[bmcps-console] returned=" << messages_result.returned_count
                << " total_matching=" << messages_result.total_matching
                << " truncated=" << (messages_result.truncated ? "true" : "false");
    text_stream << "\n";
    text_stream << "time_sync browser_now_ms=" << messages_result.time_sync.browser_now_ms
                << " server_now_ms=" << messages_result.time_sync.server_now_ms
                << " offset_ms=" << messages_result.time_sync.offset_ms
                << " round_trip_ms=" << messages_result.time_sync.round_trip_ms;
    text_stream << "\n\n";

    for (const std::string &line : messages_result.lines) {
        text_stream << line << "\n";
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = text_stream.str();

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_get_console_messages {

void register_tool() {
    json time_type_enum = json::array();
    time_type_enum.push_back("none");
    time_type_enum.push_back("last_duration");
    time_type_enum.push_back("range");
    time_type_enum.push_back("from_onwards");
    time_type_enum.push_back("until");

    json unit_enum = json::array();
    unit_enum.push_back("milliseconds");
    unit_enum.push_back("seconds");
    unit_enum.push_back("minutes");

    json order_enum = json::array();
    order_enum.push_back("newest_first");
    order_enum.push_back("oldest_first");

    json level_type_enum = json::array();
    level_type_enum.push_back("min_level");
    level_type_enum.push_back("only");

    json level_names_enum = json::array();
    level_names_enum.push_back("debug");
    level_names_enum.push_back("log");
    level_names_enum.push_back("info");
    level_names_enum.push_back("warning");
    level_names_enum.push_back("error");

    json time_scope_props;
    time_scope_props["type"] = json::object({{"type", "string"}, {"enum", time_type_enum}});
    time_scope_props["value"] = json::object({{"type", "number"}, {"description", "For last_duration: duration value."}});
    time_scope_props["unit"] = json::object({{"type", "string"}, {"enum", unit_enum}, {"description", "For last_duration."}});
    time_scope_props["from_ms"] = json::object({{"type", "integer"}, {"description", "For range or from_onwards: start timestamp (ms epoch)."}});
    time_scope_props["to_ms"] = json::object({{"type", "integer"}, {"description", "For range or until: end timestamp (ms epoch)."}});

    json count_scope_props;
    count_scope_props["max_entries"] = json::object({{"type", "integer"}, {"default", 500}, {"description", "Max number of lines to return."}});
    count_scope_props["order"] = json::object({{"type", "string"}, {"enum", order_enum}, {"default", "newest_first"}});

    json level_scope_props;
    level_scope_props["type"] = json::object({{"type", "string"}, {"enum", level_type_enum}});
    level_scope_props["level"] = json::object({{"type", "string"}, {"enum", level_names_enum}, {"description", "For min_level."}});
    level_scope_props["levels"] = json::object({{"type", "array"}, {"items", json::object({{"type", "string"}})}, {"description", "For only: list of levels to include."}});

    json time_scope_schema;
    time_scope_schema["type"] = "object";
    time_scope_schema["description"] = "Time filter. One variant: type=none (default), type=last_duration (value+unit), type=range (from_ms+to_ms), type=from_onwards (from_ms), type=until (to_ms).";
    time_scope_schema["properties"] = time_scope_props;
    time_scope_schema["required"] = json::array({"type"});

    json count_scope_schema;
    count_scope_schema["type"] = "object";
    count_scope_schema["description"] = "Max entries to return and order. Applied after time and level filter.";
    count_scope_schema["properties"] = count_scope_props;

    json level_scope_schema;
    level_scope_schema["type"] = "object";
    level_scope_schema["description"] = "Level filter: type=min_level with level (default info), or type=only with levels array.";
    level_scope_schema["properties"] = level_scope_props;
    level_scope_schema["required"] = json::array({"type"});

    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {{"time_scope", time_scope_schema}, {"count_scope", count_scope_schema}, {"level_scope", level_scope_schema}};
    input_schema["required"] = json::array();

    mcp_tools::register_tool({
        "get_console_messages",
        "Get console messages (console.log, console.error, etc.) from the current browser tab. "
        "The browser must be open and a tab attached (call open_browser first). "
        "Parameters: time_scope (none | last_duration | range | from_onwards | until), count_scope (max_entries, order), level_scope (min_level or only). "
        "Response first line: [bmcps-console] returned=N total_matching=M truncated=true|false; then time_sync; then log lines.",
        input_schema,
        handle_get_console_messages
    });
}

} // namespace tool_get_console_messages
