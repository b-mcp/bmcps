#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

// Tool handler for "list_tabs".
// Returns the list of open browser tabs (targets) from CDP.

static json handle_list_tabs(const json &arguments) {
    (void)arguments;

    debug_log::log("list_tabs invoked");
    browser_driver::TabListResult tab_list_result = cdp_driver::list_tabs();

    json result;

    if (!tab_list_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "Failed to list tabs: " + tab_list_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    // Build a human-readable and machine-parseable tab list.
    json tabs_array = json::array();
    for (const auto &tab : tab_list_result.tabs) {
        json tab_entry;
        tab_entry["target_id"] = tab.target_id;
        tab_entry["title"] = tab.title;
        tab_entry["url"] = tab.url;
        tab_entry["type"] = tab.type;
        tabs_array.push_back(tab_entry);
    }

    // Also produce a human-readable summary.
    std::ostringstream summary_stream;
    summary_stream << "Found " << tab_list_result.tabs.size() << " tab(s):\n";
    for (size_t index = 0; index < tab_list_result.tabs.size(); index++) {
        const auto &tab = tab_list_result.tabs[index];
        summary_stream << "  [" << index << "] " << tab.title
                       << " (" << tab.url << ") type=" << tab.type
                       << " id=" << tab.target_id << "\n";
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = summary_stream.str();

    debug_log::log("Listing tabs… found " + std::to_string(tab_list_result.tabs.size()) + " tab(s).");
    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_list_tabs {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();
    // No parameters needed.

    mcp_tools::register_tool({
        "list_tabs",
        "List all open browser tabs. Returns target IDs, titles, URLs, and types. "
        "The browser must be open (call open_browser first).",
        input_schema,
        handle_list_tabs
    });
}

} // namespace tool_list_tabs
