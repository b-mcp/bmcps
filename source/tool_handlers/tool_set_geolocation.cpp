#include "tool_handlers/tool_handlers.hpp"
#include "mcp/mcp_tools.hpp"
#include "browser/cdp/cdp_driver.hpp"
#include "utils/debug_log.hpp"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

static json handle_set_geolocation(const json &arguments) {
    json result;

    if (!arguments.contains("latitude") || !arguments["latitude"].is_number()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "set_geolocation requires number latitude.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }
    if (!arguments.contains("longitude") || !arguments["longitude"].is_number()) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "set_geolocation requires number longitude.";

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    double latitude = arguments["latitude"].get<double>();
    double longitude = arguments["longitude"].get<double>();
    double accuracy = 0.0;
    if (arguments.contains("accuracy") && arguments["accuracy"].is_number()) {
        accuracy = arguments["accuracy"].get<double>();
    }

    debug_log::log("set_geolocation invoked");
    browser_driver::DriverResult geo_result = cdp_driver::set_geolocation(latitude, longitude, accuracy);

    if (!geo_result.success) {
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = "set_geolocation failed: " + geo_result.error_detail;

        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }

    json text_content;
    text_content["type"] = "text";
    text_content["text"] = geo_result.message;

    result["content"] = json::array({text_content});
    result["isError"] = false;
    return result;
}

namespace tool_set_geolocation {

void register_tool() {
    json input_schema;
    input_schema["type"] = "object";
    input_schema["properties"] = {
        {"latitude", {{"type", "number"}, {"description", "Latitude."}}},
        {"longitude", {{"type", "number"}, {"description", "Longitude."}}},
        {"accuracy", {{"type", "number"}, {"description", "Accuracy in meters."}}}
    };
    input_schema["required"] = json::array({"latitude", "longitude"});

    mcp_tools::register_tool({
        "set_geolocation",
        "Set geolocation override. Browser must be open.",
        input_schema,
        handle_set_geolocation
    });
}

} // namespace tool_set_geolocation
