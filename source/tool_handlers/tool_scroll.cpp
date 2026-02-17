#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "browser/browser_driver_abi.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static browser_driver::ScrollScope parse_scroll_scope(const json &arguments) {
    browser_driver::ScrollScope scope;

    if (!arguments.contains("scroll_scope") || !arguments["scroll_scope"].is_object()) {
        scope.type = browser_driver::ScrollScopeType::Page;
        scope.delta_x = 0;
        scope.delta_y = 0;
        return scope;
    }

    const json &scroll_scope = arguments["scroll_scope"];
    std::string type_str = scroll_scope.value("type", "page");

    if (type_str == "element") {
        scope.type = browser_driver::ScrollScopeType::Element;
        scope.selector = scroll_scope.value("selector", "");
        scope.delta_x = scroll_scope.value("delta_x", 0);
        scope.delta_y = scroll_scope.value("delta_y", 0);
    } else {
        scope.type = browser_driver::ScrollScopeType::Page;
        scope.delta_x = scroll_scope.value("delta_x", 0);
        scope.delta_y = scroll_scope.value("delta_y", 0);
    }

    return scope;
}

static json handle_scroll(const json &arguments) {
    browser_driver::ScrollScope scope = parse_scroll_scope(arguments);

    if (scope.type == browser_driver::ScrollScopeType::Element && scope.selector.empty()) {
        json result;
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "scroll with scroll_scope type 'element' requires 'selector'.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    debug_log::log("scroll invoked");
    browser_driver::DriverResult scroll_result = cdp_driver::scroll(scope);

    json result;

    if (!scroll_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "scroll failed: " + scroll_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = scroll_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_scroll {

void register_tool() {
    json type_enum = json::array();
    type_enum.push_back("page");
    type_enum.push_back("element");

    json scroll_scope_props;
    scroll_scope_props["type"] = json::object({{"type", "string"}, {"enum", type_enum}});
    scroll_scope_props["delta_x"] = json::object({{"type", "number"}, {"description", "Pixels to scroll horizontally. Default 0."}});
    scroll_scope_props["delta_y"] = json::object({{"type", "number"}, {"description", "Pixels to scroll vertically (positive = down). Default 0."}});
    scroll_scope_props["selector"] = json::object({{"type", "string"}, {"description", "Required when type=element: CSS selector of the scrollable container."}});

    json scroll_scope_schema;
    scroll_scope_schema["type"] = "object";
    scroll_scope_schema["description"] = "Scroll target: type=page (window) or type=element (selector + delta).";
    scroll_scope_schema["properties"] = scroll_scope_props;
    scroll_scope_schema["required"] = json::array({"type"});

    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"scroll_scope", scroll_scope_schema}
    };
    input_schema["required"] = json::array();

    mcp_tools::register_tool({
        "scroll",
        "Scroll the page (window) or a scrollable element (e.g. overflow container). scroll_scope: type 'page' with delta_x, delta_y; or type 'element' with selector and delta_x, delta_y. Browser must be open and a tab attached.",
        input_schema,
        handle_scroll
    });
}

} // namespace tool_scroll
