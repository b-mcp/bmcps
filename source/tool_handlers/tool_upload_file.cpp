#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_upload_file(const json &arguments) {
    json result;

    if (!arguments.contains("selector") || !arguments["selector"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "upload_file requires a string selector (file input element).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }
    if (!arguments.contains("file_path") || !arguments["file_path"].is_string()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "upload_file requires a string file_path (path to file on host).";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    std::string selector = arguments["selector"].get<std::string>();
    std::string file_path = arguments["file_path"].get<std::string>();

    debug_log::log("upload_file invoked");
    browser_driver::DriverResult upload_result = cdp_driver::upload_file(selector, file_path);

    if (!upload_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "upload_file failed: " + upload_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = upload_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_upload_file {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"selector", {{"type", "string"}, {"description", "CSS selector of the file input."}}},
        {"file_path", {{"type", "string"}, {"description", "Absolute path to the file."}}}
    };
    input_schema["required"] = json::array({"selector", "file_path"});

    mcp_tools::register_tool({
        "upload_file",
        "Set file(s) on a file input by selector. file_path must be available to the browser. Browser must be open and a tab attached.",
        input_schema,
        handle_upload_file
    });
}

} // namespace tool_upload_file
