#ifndef BMCPS_BROWSER_DRIVER_ABI_HPP
#define BMCPS_BROWSER_DRIVER_ABI_HPP

// Browser driver abstraction interface.
// Each browser driver (CDP for Chrome, potentially others in the future)
// implements these functions. This keeps the tool_handlers layer decoupled
// from any particular browser protocol.

#include <string>
#include <vector>
#include <functional>

namespace browser_driver {

// Information about a single browser tab / target.
struct TabInfo {
    std::string target_id;
    std::string title;
    std::string url;
    std::string type; // e.g. "page", "background_page", "service_worker"
};

// Result of a browser driver operation.
struct DriverResult {
    bool success = false;
    std::string message;
    std::string error_detail;
};

// Result of listing tabs.
struct TabListResult {
    bool success = false;
    std::vector<TabInfo> tabs;
    std::string error_detail;
};

// Result of navigation.
struct NavigateResult {
    bool success = false;
    std::string frame_id;
    std::string error_text; // CDP errorText if navigation failed
};

// Result of capturing a screenshot of the current tab.
struct CaptureScreenshotResult {
    bool success = false;
    std::string image_base64;
    std::string mime_type;   // e.g. "image/png"
    std::string error_detail;
};

} // namespace browser_driver

#endif // BMCPS_BROWSER_DRIVER_ABI_HPP
