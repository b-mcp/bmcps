#include "utils/debug_log.hpp"

#include <cstdlib>
#include <cctype>
#include <iostream>
#include <algorithm>
#include <string>

namespace debug_log {

static std::string to_lower(const std::string &input) {
    std::string result = input;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return result;
}

bool is_debug_enabled() {
    const char *value = std::getenv("BMCPS_DEBUG");
    if (value == nullptr || value[0] == '\0') {
        return false;
    }
    std::string normalized = to_lower(std::string(value));
    return (normalized == "1" || normalized == "true" || normalized == "yes");
}

void log(const std::string &message) {
    if (!is_debug_enabled()) {
        return;
    }
    std::cerr << "[bmcps] " << message << std::endl;
}

} // namespace debug_log
