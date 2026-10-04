#include "encoding.h"

#include <iostream>
#include <string>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static void requireInvalid(const std::string& input, const char* target) {
    try {
        liarsoft::convertEncoding(input, "UTF-8", target, true);
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error("Strict conversion accepted invalid or unrepresentable text");
}

int main() {
    const std::string extension("\xFB\x40", 2); // Windows CP932 extension.
    for (const char* alias : {"CP932", "WINDOWS-31J", "SHIFT_JIS", "sjis"}) {
        const std::string utf8 = liarsoft::convertEncoding(extension, alias, "UTF-8");
        if (utf8.empty() || liarsoft::convertEncoding(utf8, "UTF-8", alias) != extension) {
            std::cerr << "CP932 round-trip failed for " << alias << std::endl;
            return 1;
        }
        if (liarsoft::normalizeEncodingName(alias) != "CP932") {
            std::cerr << "CP932 alias was not normalized: " << alias << std::endl;
            return 1;
        }
    }
    const std::string unicode = u8"日本語 中文 Русский 😀";
    require(liarsoft::convertEncoding(unicode, "UTF-8", "UTF-8", true) == unicode,
            "UTF-8 identity conversion failed");
    for (const auto& sample : {std::make_pair("GBK", std::string(u8"中文标点。")),
                               std::make_pair("CP1251", std::string(u8"Русский текст"))}) {
        const auto bytes = liarsoft::convertEncoding(sample.second, "UTF-8", sample.first, true);
        require(liarsoft::convertEncoding(bytes, sample.first, "UTF-8", true) == sample.second,
                "Legacy encoding round-trip failed");
    }
    requireInvalid(std::string("\xC3", 1), "UTF-8");
    requireInvalid(std::string("\xFF", 1), "UTF-8");
    requireInvalid(u8"😀", "CP932");
    return 0;
}
