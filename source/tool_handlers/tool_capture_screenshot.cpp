#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Tool handler for "capture_screenshot".
// Captures a screenshot of the currently displayed browser tab via CDP Page.captureScreenshot.

static json handle_capture_screenshot(const json &arguments) {
    (void)arguments;

    debug_log::log("capture_screenshot invoked");
    browser_driver::CaptureScreenshotResult screenshot_result = cdp_driver::capture_screenshot();

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
    input_schema["required"] = json::array();

    mcp_tools::register_tool({
        "capture_screenshot",
        "Capture a screenshot of the currently displayed browser tab. "
        "The browser must be open and a tab must be attached (call open_browser first). "
        "Returns the screenshot as image content so the model can verify the visible UI (e.g. buttons, layout).",
        input_schema,
        handle_capture_screenshot
    });
}

} // namespace tool_capture_screenshot
