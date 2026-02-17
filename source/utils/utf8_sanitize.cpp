#include "utils/utf8_sanitize.hpp"

#include <cstdint>

namespace utf8_sanitize {

namespace {

const unsigned char kReplacementUtf8[] = { 0xEF, 0xBF, 0xBD }; // U+FFFD in UTF-8
constexpr size_t kReplacementLength = sizeof(kReplacementUtf8);

// Returns number of bytes that form a valid UTF-8 lead byte (1-4), or 0 if invalid.
unsigned char utf8_lead_length(unsigned char byte) {
    if (byte < 0x80u) {
        return 1;
    }
    if (byte >= 0xC2u && byte <= 0xDFu) {
        return 2;
    }
    if (byte >= 0xE0u && byte <= 0xEFu) {
        return 3;
    }
    if (byte >= 0xF0u && byte <= 0xF4u) {
        return 4;
    }
    return 0;
}

// Returns true if byte is a valid UTF-8 continuation (0x80..0xBF).
bool is_continuation(unsigned char byte) {
    return (byte & 0xC0u) == 0x80u;
}

} // namespace

void sanitize(std::string &text) {
    std::string result;
    result.reserve(text.size());

    const unsigned char *pointer = reinterpret_cast<const unsigned char *>(text.data());
    const unsigned char *end = pointer + text.size();

    while (pointer < end) {
        unsigned char lead = *pointer;
        unsigned char length = utf8_lead_length(lead);

        if (length == 0) {
            result.append(reinterpret_cast<const char *>(kReplacementUtf8), kReplacementLength);
            ++pointer;
            continue;
        }

        if (pointer + length > end) {
            result.append(reinterpret_cast<const char *>(kReplacementUtf8), kReplacementLength);
            ++pointer;
            continue;
        }

        bool valid = true;
        for (unsigned char index = 1; index < length; ++index) {
            if (!is_continuation(pointer[index])) {
                valid = false;
                break;
            }
        }

        if (!valid) {
            result.append(reinterpret_cast<const char *>(kReplacementUtf8), kReplacementLength);
            ++pointer;
            continue;
        }

        result.append(reinterpret_cast<const char *>(pointer), static_cast<size_t>(length));
        pointer += length;
    }

    text = std::move(result);
}

std::string sanitize(const std::string &text) {
    std::string copy = text;
    sanitize(copy);
    return copy;
}

} // namespace utf8_sanitize
