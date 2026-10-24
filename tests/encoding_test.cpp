#include "encoding.h"
#include "encoding_samples.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <stdexcept>

static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static void requireInvalid(const std::string& input, const char* target, const char* source = "UTF-8") {
    try {
        liarsoft::convertEncoding(input, source, target, true);
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
    const auto& supported = liarsoft::supportedEncodings();
    require(supported.size() == encodingSamples().size(), "Encoding samples do not cover every choice");
    for (const auto& sample : encodingSamples()) {
        require(std::count(supported.begin(), supported.end(), sample.first) == 1,
                "Encoding sample is missing or duplicated in the shared choices");
        const auto bytes = liarsoft::convertEncoding(sample.second, "UTF-8", sample.first, true);
        require(liarsoft::convertEncoding(bytes, sample.first, "UTF-8", true) == sample.second,
                "Legacy encoding round-trip failed for " + sample.first);
        if (sample.first != "GB18030" && sample.first != "UTF-8")
            requireInvalid(u8"😀", sample.first.c_str());
    }
    for (const auto& alias : std::vector<std::pair<std::string, std::string>>{
            {"cp936", "GBK"}, {"Windows-936", "GBK"}, {"GB_2312", "GBK"},
            {"GB-18030", "GB18030"}, {"54936", "GB18030"}, {"CP54936", "GB18030"},
            {"Big-5", "CP950"}, {"Windows-950", "CP950"}, {"950", "CP950"},
            {"UHC", "CP949"}, {"Windows-949", "CP949"}, {"949", "CP949"},
            {"UTF_8", "UTF-8"}, {"65001", "UTF-8"}})
        require(liarsoft::normalizeEncodingName(alias.first) == alias.second, "Encoding alias changed");
    for (int codePage = 1250; codePage <= 1258; ++codePage) {
        const auto number = std::to_string(codePage);
        for (const auto& alias : {number, "cp" + number, "Windows-" + number, "MS" + number})
            require(liarsoft::normalizeEncodingName(alias) == "CP" + number, "Windows code page alias changed");
    }
    require(liarsoft::convertEncoding(std::string("\x81\x30\x81\x30", 4), "GB18030", "UTF-8", true) == u8"\u0080",
            "GB18030 four-byte character was decoded as GBK");
    require(liarsoft::convertEncoding(u8"\u0080", "UTF-8", "GB18030", true) == std::string("\x81\x30\x81\x30", 4),
            "GB18030 four-byte character was encoded as GBK");
    requireInvalid(std::string("\xC3", 1), "UTF-8");
    requireInvalid(std::string("\xFF", 1), "UTF-8");
    requireInvalid(u8"😀", "CP932");
    requireInvalid(std::string("\x81", 1), "UTF-8", "GBK");
    requireInvalid(std::string("\xA4", 1), "UTF-8", "CP950");
    requireInvalid(std::string("\x81", 1), "UTF-8", "CP949");
    requireInvalid(std::string("\x81\x30\x81", 3), "UTF-8", "GB18030");
    requireInvalid(std::string("\x81\x30\xFF\x30", 4), "UTF-8", "GB18030");
    requireInvalid("text", "not-an-encoding");
    requireInvalid("text", "UTF-8", "not-an-encoding");
    return 0;
}
