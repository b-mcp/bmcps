#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

static json handle_get_navigation_history(const json &arguments) {
    (void)arguments;

    debug_log::log("get_navigation_history invoked");
    browser_driver::NavigationHistoryResult history_result = cdp_driver::get_navigation_history();

    json result;

    if (!history_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "Failed to get navigation history: " + history_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::ostringstream text_stream;
    text_stream << "Current index: " << history_result.current_index
                << ". History has " << history_result.entries.size() << " entr"
                << (history_result.entries.size() == 1 ? "y" : "ies") << ":\n";
    for (size_t index = 0; index < history_result.entries.size(); ++index) {
        const auto &entry = history_result.entries[index];
        text_stream << "  [" << index << "] " << entry.url;
        if (!entry.title.empty()) {
            text_stream << " - " << entry.title;
        }
        if (static_cast<int>(index) == history_result.current_index) {
            text_stream << " (current)";
        }
        text_stream << "\n";
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = text_stream.str();

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_get_navigation_history {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();
    input_schema["required"] = json::array();

    mcp_tools::register_tool({
        "get_navigation_history",
        "Get the current tab's navigation history (list of URLs and the current index). "
        "The browser must be open and a tab must be attached (call open_browser first). "
        "Unlike in-page JavaScript, this returns the full history via CDP.",
        input_schema,
        handle_get_navigation_history
    });
}

} // namespace tool_get_navigation_history
