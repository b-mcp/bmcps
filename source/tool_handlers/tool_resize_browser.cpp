#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <map>

using json = nlohmann::json;

static bool resolve_preset(const std::string &preset_name, int &width, int &height) {
    static const std::map<std::string, std::pair<int, int>> presets = {
        {"vga", {640, 480}},
        {"xga", {1024, 768}},
        {"hd", {1280, 720}},
        {"fullhd", {1920, 1080}},
        {"2k", {2560, 1440}},
        {"4k", {3840, 2160}}
    };
    auto it = presets.find(preset_name);
    if (it == presets.end()) {
        return false;
    }
    width = it->second.first;
    height = it->second.second;
    return true;
}

static json handle_resize_browser(const json &arguments) {
    json result;

    int width = 0;
    int height = 0;
    bool has_preset = arguments.contains("preset") && arguments["preset"].is_string();
    bool has_dimensions = arguments.contains("width") && arguments["width"].is_number() &&
                         arguments.contains("height") && arguments["height"].is_number();

    if (has_preset && has_dimensions) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "resize_browser: use either 'preset' or 'width'+'height', not both.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    if (has_preset) {
        std::string preset = arguments["preset"].get<std::string>();
        if (!resolve_preset(preset, width, height)) {
            json error_content;
            error_content["type"] = "text";
            error_content["text"] = "resize_browser: unknown preset. Use one of: vga, xga, hd, fullhd, 2k, 4k";

            result["content"] = json::array({error_content});
            result["isError"] = true;
            return result;
        }
    } else if (has_dimensions) {
        width = arguments["width"].get<int>();
        height = arguments["height"].get<int>();
        if (width <= 0 || height <= 0) {
            json error_content;
            error_content["type"] = "text";
            error_content["text"] = "resize_browser: width and height must be positive.";

            result["content"] = json::array({error_content});
            result["isError"] = true;
            return result;
        }
    } else {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "resize_browser: provide either 'preset' (vga, xga, hd, fullhd, 2k, 4k) or 'width' and 'height' in pixels.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    debug_log::log("resize_browser invoked width=" + std::to_string(width) + " height=" + std::to_string(height));
    browser_driver::DriverResult resize_result = cdp_driver::set_window_bounds(width, height);

    if (!resize_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "resize_browser failed: " + resize_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = resize_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_resize_browser {

void register_tool() {
    json preset_enum = json::array();
    preset_enum.push_back("vga");
    preset_enum.push_back("xga");
    preset_enum.push_back("hd");
    preset_enum.push_back("fullhd");
    preset_enum.push_back("2k");
    preset_enum.push_back("4k");

    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"preset", json::object({
            {"type", "string"},
            {"enum", preset_enum},
            {"description", "Predefined size: vga (640x480), xga (1024x768), hd (1280x720), fullhd (1920x1080), 2k, 4k."}
        })},
        {"width", json::object({
            {"type", "integer"},
            {"description", "Window width in pixels. Use together with height (do not use with preset)."}
        })},
        {"height", json::object({
            {"type", "integer"},
            {"description", "Window height in pixels. Use together with width (do not use with preset)."}
        })}
    };
    input_schema["required"] = json::array();

    mcp_tools::register_tool({
        "resize_browser",
        "Resize the browser window. Use either preset (vga, xga, hd, fullhd, 2k, 4k) or width and height in pixels. Browser must be open.",
        input_schema,
        handle_resize_browser
    });
}

} // namespace tool_resize_browser
