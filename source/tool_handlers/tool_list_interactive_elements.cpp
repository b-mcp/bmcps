#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

static json handle_list_interactive_elements(const json &arguments) {
    (void)arguments;

    debug_log::log("list_interactive_elements invoked");
    browser_driver::ListInteractiveElementsResult list_result = cdp_driver::list_interactive_elements();

    json result;

    if (!list_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "Failed to list interactive elements: " + list_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json elements_array = json::array();
    for (const auto &element : list_result.elements) {
        json object;
        object["selector"] = element.selector;
        object["role"] = element.role;
        object["label"] = element.label;
        object["placeholder"] = element.placeholder;
        object["type"] = element.type;
        object["text"] = element.text;
        elements_array.push_back(object);
    }

    std::ostringstream text_stream;
    text_stream << "Found " << list_result.elements.size() << " interactive element(s):\n";
    text_stream << elements_array.dump(2);

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = text_stream.str();

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_list_interactive_elements {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();
    input_schema["required"] = json::array();

    mcp_tools::register_tool({
        "list_interactive_elements",
        "List form fields and clickable elements on the current page (inputs, textareas, buttons, links). "
        "Returns selector, role, label, placeholder, type, and visible text for each. Use these selectors with fill_field and click_element. Browser must be open and a tab attached.",
        input_schema,
        handle_list_interactive_elements
    });
}

} // namespace tool_list_interactive_elements
