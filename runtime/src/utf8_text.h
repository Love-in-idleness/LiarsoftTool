#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace liarsoft::runtime {

inline uint32_t nextUtf8(std::string_view text, size_t& offset) {
    const auto start = offset;
    const auto bad = [start]() -> uint32_t {
        throw std::runtime_error("Invalid UTF-8 at byte " + std::to_string(start));
    };
    if (offset >= text.size()) return bad();
    const unsigned char first = text[offset++];
    if (first < 0x80) return first;
    unsigned count;
    uint32_t value, minimum;
    if (first >= 0xc2 && first <= 0xdf) {
        count = 1; value = first & 0x1f; minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
        count = 2; value = first & 0x0f; minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
        count = 3; value = first & 7; minimum = 0x10000;
    } else return bad();
    if (count > text.size() - offset) return bad();
    while (count--) {
        const unsigned char byte = text[offset++];
        if ((byte & 0xc0) != 0x80) return bad();
        value = (value << 6) | (byte & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return bad();
    return value;
}

inline std::u16string decodeUtf8(std::string_view text) {
    std::u16string result;
    for (size_t offset = 0; offset < text.size();) {
        auto cp = nextUtf8(text, offset);
        if (cp <= 0xffff) result += char16_t(cp);
        else {
            cp -= 0x10000;
            result += char16_t(0xd800 + (cp >> 10));
            result += char16_t(0xdc00 + (cp & 1023));
        }
    }
    return result;
}
}
