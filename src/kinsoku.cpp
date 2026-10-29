#include "kinsoku.h"
#include "utf8_text.h"

#include <bitset>
#include <stdexcept>

namespace liarsoft::runtime {
namespace {
struct Parser {
    std::string_view text;
    size_t offset = 0;

    [[noreturn]] void fail(const char* message) const {
        throw std::runtime_error(std::string("Invalid kinsoku JSON at byte ") +
                                 std::to_string(offset) + ": " + message);
    }
    void whitespace() {
        while (offset < text.size() && (text[offset] == ' ' || text[offset] == '\t' ||
               text[offset] == '\r' || text[offset] == '\n')) ++offset;
    }
    bool take(char character) {
        whitespace();
        if (offset == text.size() || text[offset] != character) return false;
        ++offset;
        return true;
    }
    void expect(char character) { if (!take(character)) fail("unexpected token"); }
    char16_t hexadecimal() {
        if (text.size() - offset < 4) fail("incomplete Unicode escape");
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const char character = text[offset++];
            unsigned digit;
            if (character >= '0' && character <= '9') digit = character - '0';
            else if (character >= 'a' && character <= 'f') digit = character - 'a' + 10;
            else if (character >= 'A' && character <= 'F') digit = character - 'A' + 10;
            else fail("invalid Unicode escape");
            value = (value << 4) | digit;
        }
        if (value >= 0xd800 && value <= 0xdfff) fail("surrogate and non-BMP characters are not supported");
        return char16_t(value);
    }
    std::u16string string() {
        expect('"');
        std::u16string result;
        while (offset < text.size()) {
            if (text[offset] == '"') { ++offset; return result; }
            if (text[offset] == '\\') {
                if (++offset == text.size()) fail("incomplete string escape");
                switch (text[offset++]) {
                case '"': result += u'"'; break;
                case '\\': result += u'\\'; break;
                case '/': result += u'/'; break;
                case 'b': result += u'\b'; break;
                case 'f': result += u'\f'; break;
                case 'n': result += u'\n'; break;
                case 'r': result += u'\r'; break;
                case 't': result += u'\t'; break;
                case 'u': result += hexadecimal(); break;
                default: fail("invalid string escape");
                }
            } else {
                if (static_cast<unsigned char>(text[offset]) < 0x20) fail("unescaped control character");
                const auto character = nextUtf8(text, offset);
                if (character > 0xffff) fail("non-BMP characters are not supported");
                result += char16_t(character);
            }
        }
        fail("unterminated string");
    }
};
}

uint32_t KinsokuRules::classify(char16_t character) const {
    if (noLineStart.find(character) != std::u16string::npos) return 1;
    if (noLineEnd.find(character) != std::u16string::npos) return 2;
    return 0;
}

KinsokuRules parseKinsoku(std::string_view json) {
    Parser parser{json};
    parser.expect('{');
    KinsokuRules rules;
    unsigned fields = 0;
    if (!parser.take('}')) {
        for (;;) {
            const auto key = parser.string();
            const unsigned field = key == u"version" ? 1 : key == u"no_line_start" ? 2 : key == u"no_line_end" ? 4 : 0;
            if (!field) parser.fail("unknown key");
            if (fields & field) parser.fail("duplicate key");
            fields |= field;
            parser.expect(':');
            if (field == 1) {
                if (!parser.take('1')) parser.fail("unsupported version (expected integer 1)");
            } else if (field == 2) rules.noLineStart = parser.string();
            else rules.noLineEnd = parser.string();
            if (parser.take('}')) break;
            parser.expect(',');
        }
    }
    if (fields != 7) parser.fail("version and both character sets are required");
    parser.whitespace();
    if (parser.offset != json.size()) parser.fail("trailing content");
    std::bitset<65536> starts;
    for (const char16_t character : rules.noLineStart) {
        if (!character) parser.fail("NUL is not a rule character");
        starts.set(character);
    }
    for (const char16_t character : rules.noLineEnd) {
        if (!character) parser.fail("NUL is not a rule character");
        if (starts.test(character)) parser.fail("the character sets overlap");
    }
    return rules;
}
}
