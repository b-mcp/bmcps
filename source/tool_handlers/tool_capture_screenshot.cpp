#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Tool handler for "capture_screenshot".
// Captures a screenshot of the currently displayed browser tab via CDP Page.captureScreenshot.
// Default: jpeg quality 70. Caller can set format (png | jpeg) and quality. If image exceeds max payload size, a clear error is returned.

static json handle_capture_screenshot(const json &arguments) {
    browser_driver::CaptureScreenshotOptions options;
    options.format = "jpeg";
    options.quality = 70;

    if (arguments.contains("format") && arguments["format"].is_string()) {
        std::string format_arg = arguments["format"].get<std::string>();
        if (format_arg == "png" || format_arg == "jpeg") {
            options.format = format_arg;
        }
    }
    if (arguments.contains("quality") && arguments["quality"].is_number_integer()) {
        int quality_arg = arguments["quality"].get<int>();
        if (quality_arg >= 1 && quality_arg <= 100) {
            options.quality = quality_arg;
        }
    }

    debug_log::log("capture_screenshot invoked format=" + options.format + " quality=" + std::to_string(options.quality));
    browser_driver::CaptureScreenshotResult screenshot_result = cdp_driver::capture_screenshot(options);

    json result;

    if (screenshot_result.success) {
        json text_content;
        text_content["type"] = "text";
        text_content["text"] = "Screenshot captured.";

        json image_content;
        image_content["type"] = "image";
        image_content["data"] = screenshot_result.image_base64;
        image_content["mimeType"] = screenshot_result.mime_type;

        result["content"] = json::array({text_content, image_content});
        result["isError"] = false;
    } else {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "Screenshot failed: " + screenshot_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
    }

    return result;
}

namespace tool_capture_screenshot {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = json::object();
    input_schema["properties"]["format"] = {
        {"type", "string"},
        {"enum", json::array({"png", "jpeg"})},
        {"description", "Image format: jpeg (default) or png. Caller chooses."}
    };
    input_schema["properties"]["quality"] = {
        {"type", "integer"},
        {"description", "JPEG quality 1–100 (default 70). Only used when format is jpeg. Lower = smaller file."}
    };
    input_schema["required"] = json::array();

    mcp_tools::register_tool({
        "capture_screenshot",
        "Capture a screenshot of the currently displayed browser tab. "
        "Default: jpeg quality 70. Optional: format (png | jpeg), quality (1–100 for jpeg). "
        "If the image exceeds the configured max size, a clear error is returned (reduce viewport or lower quality). "
        "Browser must be open and a tab attached (call open_browser first). "
        "Returns the screenshot as image content so the model can verify the visible UI.",
        input_schema,
        handle_capture_screenshot
    });
}

} // namespace tool_capture_screenshot
