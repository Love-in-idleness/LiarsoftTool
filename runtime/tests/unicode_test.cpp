#include "unicode.h"
#include "encoding.h"
#include "gscfile.h"
#include "gsc_decompiler.h"

#include <iostream>
#include <stdexcept>
#include <vector>

using liarsoft::runtime::GlyphTokens;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Function> void fails(Function function) {
    try { function(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Expected a runtime error");
}
void put32(std::vector<uint8_t>& bytes, size_t pos, uint32_t value) {
    for (unsigned byte = 0; byte < 4; ++byte) bytes[pos + byte] = uint8_t(value >> (byte * 8));
}
std::vector<uint8_t> fixture(const std::string& text) {
    std::vector<uint8_t> bytes(28 + 2 + 8 + 1 + text.size() + 1 + 2);
    put32(bytes, 0, uint32_t(bytes.size()));
    put32(bytes, 4, 28);
    put32(bytes, 8, 2);
    put32(bytes, 12, 8);
    put32(bytes, 16, uint32_t(text.size() + 2));
    put32(bytes, 20, 24);
    put32(bytes, 24, 31);
    bytes[28] = 0x18; bytes[29] = 0x01;
    put32(bytes, 34, 1);
    std::copy(text.begin(), text.end(), bytes.begin() + 39);
    bytes[bytes.size() - 2] = 0x31;
    bytes.back() = 0x32;
    return bytes;
}
std::string utf8(unsigned cp) {
    return {char(0xe0 | (cp >> 12)), char(0x80 | ((cp >> 6) & 63)), char(0x80 | (cp & 63))};
}
}

int main() {
    try {
        GlyphTokens table;
        const std::string japanese = u8"^g005日本語^n|漢[かん]^cy：ABC";
        require(table.encode(japanese) == liarsoft::convertEncoding(japanese, "UTF-8", "CP932", true),
                "Existing CP932 text or control syntax changed");
        require(table.size() == 0, "Native characters consumed glyph slots");
        const std::string mixed = u8"^g005简体中文 Русский^n^cw";
        const auto encoded = table.encode(mixed);
        require(table.containsTokens(encoded), "Simplified text was not assigned Unicode tokens");
        require(table.decode(encoded) == u"^g005简体中文 Русский^n^cw", "Unicode transport lost text");
        require(table.encode(mixed) == encoded, "Glyph IDs changed on repeated encoding");

        GlyphTokens restored;
        restored.restore(table.serialize());
        restored.encode(u8"别的字先加载");
        require(restored.encode(mixed) == encoded, "Reload order invalidated saved history glyphs");
        auto corrupt = table.serialize();
        corrupt[4] = 2;
        fails([&] { restored.restore(corrupt); });
        require(restored.encode(mixed) == encoded, "Failed cache restore altered glyph IDs");
        corrupt = table.serialize();
        corrupt[10] = 3;
        fails([&] { restored.restore(corrupt); });

        for (const std::string& bad : {std::string("\xc0\xaf", 2), std::string("\xe3\x81", 2),
                std::string("\xed\xa0\x80", 3), std::string("\xf4\x90\x80\x80", 4),
                std::string("\x80", 1), std::string("A\0B", 3), std::string(u8"😀")})
            fails([&] { table.encode(bad); });
        fails([&] { table.decode(std::string("\x81", 1)); });

        GlyphTokens nativeFirst;
        nativeFirst.reserveNative(std::string("\xf0\x40", 2));
        require(!nativeFirst.glyph(0xf040), "Original EUDC was remapped");
        const auto other = nativeFirst.encode(u8"简");
        require(other != std::string("\xf0\x40", 2), "Unicode allocation collided with native EUDC");
        GlyphTokens unicodeFirst;
        const auto allocated = unicodeFirst.encode(u8"简");
        fails([&] { unicodeFirst.reserveNative(allocated); });
        require(unicodeFirst.decode(allocated) == u"简", "Collision handling damaged existing history");

        GlyphTokens full;
        std::string large;
        for (unsigned cp = 0x3400; cp < 0x3400 + GlyphTokens::capacity; ++cp) large += utf8(cp);
        full.encode(large);
        require(full.size() == GlyphTokens::capacity, "Capacity fixture did not fill the table");
        const auto snapshot = full.serialize();
        fails([&] { full.encode(utf8(0x3400 + GlyphTokens::capacity)); });
        require(snapshot == full.serialize(), "Capacity failure altered the dictionary");

        GlyphTokens scriptTokens;
        const auto source = fixture(mixed);
        const auto output = liarsoft::runtime::transformUtf8Gsc(source, scriptTokens);
        fails([&] { liarsoft::runtime::transformUtf8Gsc(fixture(std::string("\xc0\xaf", 2)), scriptTokens); });
        const auto before = liarsoft::GscFile::fromBytes(source, "UTF-8");
        const auto after = liarsoft::GscFile::fromBytes(output, "CP932");
        require(before.commandSection == after.commandSection && before.endSequence == after.endSequence &&
                before.unknown1 == after.unknown1 && before.unknown2 == after.unknown2,
                "Unicode rewrite changed GSC command/header/trailing sections");
        const auto raw = liarsoft::convertEncoding(after.strings.at(0), "UTF-8", "CP932", true);
        require(scriptTokens.decode(raw) == u"^g005简体中文 Русский^n^cw", "GSC transport changed dialogue");
        const auto modernSource = liarsoft::restoreGscFromTsc(
            ";@gsc-byte-format modern-36\n;@gsc-schema rscript18\n;@gsc-trailer 00000000e066a70000\n"
            "*font 1 0 0 0 0 \"" + mixed + "\"\n*end\n", "UTF-8");
        const auto modernOutput = liarsoft::runtime::transformUtf8Gsc(modernSource, scriptTokens);
        const auto modernBefore = liarsoft::GscFile::fromBytes(modernSource, "UTF-8");
        const auto modernAfter = liarsoft::GscFile::fromBytes(modernOutput, "CP932");
        require(modernAfter.headerLength == 36 && modernAfter.commandSection == modernBefore.commandSection &&
                modernAfter.endSequence == modernBefore.endSequence &&
                modernAfter.unknown3 == modernBefore.unknown3 && modernAfter.unknown4 == modernBefore.unknown4,
                "Modern GSC container or trailing sections changed");
        GlyphTokens nativeScript;
        liarsoft::runtime::reserveNativeGsc(fixture(std::string("\xf0\x40", 2)), nativeScript);
        require(nativeScript.encode(u8"简") != std::string("\xf0\x40", 2), "Native GSC did not reserve EUDC");
        std::cout << "Unicode transport tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
