#ifndef BMCPS_DEBUG_LOG_HPP
#define BMCPS_DEBUG_LOG_HPP

#include <string>

namespace debug_log {

// Returns true if BMCPS_DEBUG env is set to a truthy value (1, true, yes).
bool is_debug_enabled();

// Writes message to stderr with [bmcps] prefix only when is_debug_enabled().
void log(const std::string &message);

} // namespace debug_log

#endif // BMCPS_DEBUG_LOG_HPP
