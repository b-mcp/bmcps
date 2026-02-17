#ifndef BMCPS_UTF8_SANITIZE_HPP
#define BMCPS_UTF8_SANITIZE_HPP

#include <string>

namespace utf8_sanitize {

// Replaces invalid UTF-8 sequences (broken multibyte, invalid bytes) with U+FFFD.
// In-place version.
void sanitize(std::string &text);

// Replaces invalid UTF-8 sequences with U+FFFD. Returns a new string.
std::string sanitize(const std::string &text);

} // namespace utf8_sanitize

#endif // BMCPS_UTF8_SANITIZE_HPP
