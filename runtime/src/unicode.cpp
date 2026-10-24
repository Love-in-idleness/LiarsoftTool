#include "unicode.h"
#include "encoding.h"
#include "gscfile.h"

#include <limits>
#include <set>
#include <stdexcept>

namespace liarsoft::runtime {
namespace {

bool lead(unsigned char c) {
    return (c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc);
}

std::optional<size_t> tokenIndex(uint16_t token) {
    const unsigned high = token >> 8, low = token & 255;
    if (high < 0xf0 || high > 0xf9 || low < 0x40 || low > 0xfc || low == 0x7f)
        return {};
    return (high - 0xf0) * 188 + low - 0x40 - (low > 0x7f);
}

uint16_t tokenValue(size_t index) {
    unsigned low = unsigned(index % 188) + 0x40;
    if (low >= 0x7f) ++low;
    return uint16_t(((index / 188 + 0xf0) << 8) | low);
}

uint32_t nextUtf8(std::string_view text, size_t& offset) {
    const size_t start = offset;
    const auto bad = [start]() -> uint32_t {
        throw std::runtime_error("Invalid UTF-8 at byte " + std::to_string(start));
    };
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
        const unsigned char c = text[offset++];
        if ((c & 0xc0) != 0x80) return bad();
        value = (value << 6) | (c & 0x3f);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
        return bad();
    return value;
}

std::u16string utf16(std::string_view text) {
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

std::vector<size_t> nativeSlots(std::string_view bytes) {
    std::vector<size_t> result;
    for (size_t offset = 0; offset < bytes.size(); ++offset) {
        const unsigned char first = bytes[offset];
        if (lead(first) && offset + 1 < bytes.size()) {
            const auto index = tokenIndex(uint16_t((first << 8) | uint8_t(bytes[offset + 1])));
            if (index) result.push_back(*index);
            ++offset;
        }
    }
    return result;
}

} // namespace

std::string GlyphTokens::encode(std::string_view text) {
    struct Character { char16_t cp; std::string native; };
    std::vector<Character> characters;
    std::vector<size_t> usedNative;
    std::set<char16_t> required;
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t offset = 0; offset < text.size();) {
        const auto start = offset;
        const uint32_t cp = nextUtf8(text, offset);
        if (cp == 0) throw std::runtime_error("A runtime script string contains NUL");
        // shortcut: the original glyph renderer consumes one UTF-16 unit; add
        // shaping and glyph-index delivery before accepting supplementary text.
        if (cp > 0xffff)
            throw std::runtime_error("Forest Unicode renderer does not support supplementary characters");
        Character character{char16_t(cp), {}};
        const std::string scalar(text.substr(start, offset - start));
        const auto cached = nativeEncoding_.find(character.cp);
        if (cached != nativeEncoding_.end()) character.native = cached->second;
        else {
            try {
                auto encoded = convertEncoding(scalar, "UTF-8", "CP932", true);
                if (convertEncoding(encoded, "CP932", "UTF-8", true) == scalar)
                    character.native = std::move(encoded);
            } catch (const std::runtime_error&) {
                // A lossless internal token, not '?' or a best-fit substitute.
            }
            nativeEncoding_.emplace(character.cp, character.native);
        }
        if (character.native.empty()) required.insert(character.cp);
        else {
            const auto slots = nativeSlots(character.native);
            usedNative.insert(usedNative.end(), slots.begin(), slots.end());
        }
        characters.push_back(std::move(character));
    }

    for (const auto slot : usedNative)
        if (glyphs_[slot])
            throw std::runtime_error("Native EUDC conflicts with an allocated Unicode glyph token");
    auto reserved = native_;
    for (const auto slot : usedNative) reserved[slot] = true;
    size_t available = 0, needed = 0;
    for (size_t slot = 0; slot < capacity; ++slot)
        available += !reserved[slot] && !glyphs_[slot];
    for (const auto cp : required) needed += tokens_.find(cp) == tokens_.end();
    if (needed > available)
        throw std::runtime_error("Forest Unicode glyph-token capacity exhausted (1880 slots)");

    std::string output;
    output.reserve(text.size());
    // Reserve allocations before publishing any new token, including on failure.
    tokens_.reserve(tokens_.size() + needed);
    native_ = reserved;
    for (const auto cp : required) {
        if (tokens_.find(cp) != tokens_.end()) continue;
        size_t slot = 0;
        while (native_[slot] || glyphs_[slot]) ++slot;
        tokens_.emplace(cp, tokenValue(slot));
        glyphs_[slot] = cp;
    }
    for (const auto& character : characters) {
        if (!character.native.empty()) output += character.native;
        else {
            const auto token = tokens_.at(character.cp);
            output += char(token >> 8);
            output += char(token & 255);
        }
    }
    return output;
}

void GlyphTokens::reserveNative(std::string_view text) {
    const auto slots = nativeSlots(text);
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto slot : slots)
        if (glyphs_[slot])
            throw std::runtime_error("Original GSC uses an allocated Unicode glyph token; restart without Unicode");
    for (const auto slot : slots) native_[slot] = true;
}

std::optional<char16_t> GlyphTokens::glyph(uint16_t token) const {
    const auto index = tokenIndex(token);
    if (!index) return {};
    std::lock_guard<std::mutex> lock(mutex_);
    if (!glyphs_[*index]) return {};
    return glyphs_[*index];
}

bool GlyphTokens::containsTokens(std::string_view text) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t offset = 0; offset + 1 < text.size(); ++offset) {
        const unsigned char first = text[offset];
        if (!lead(first)) continue;
        const auto index = tokenIndex(uint16_t((first << 8) | uint8_t(text[++offset])));
        if (index && glyphs_[*index]) return true;
    }
    return false;
}

std::u16string GlyphTokens::decode(std::string_view text) const {
    std::u16string output;
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t offset = 0; offset < text.size();) {
        const unsigned char first = text[offset];
        const auto count = lead(first) ? size_t(2) : size_t(1);
        if (count > text.size() - offset)
            throw std::runtime_error("Truncated runtime DBCS character");
        if (count == 2) {
            const auto index = tokenIndex(uint16_t((first << 8) | uint8_t(text[offset + 1])));
            if (index && glyphs_[*index]) {
                output += glyphs_[*index];
                offset += count;
                continue;
            }
        }
        output += utf16(convertEncoding(std::string(text.substr(offset, count)), "CP932", "UTF-8", true));
        offset += count;
    }
    return output;
}

size_t GlyphTokens::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tokens_.size();
}

std::vector<uint8_t> GlyphTokens::serialize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint8_t> output{'R', 'S', 'G', '1', 1, 0, 0, 0};
    output.reserve(8 + capacity * 3);
    for (size_t slot = 0; slot < capacity; ++slot) {
        output.push_back(uint8_t(glyphs_[slot]));
        output.push_back(uint8_t(glyphs_[slot] >> 8));
        output.push_back(native_[slot] ? 1 : 0);
    }
    return output;
}

void GlyphTokens::restore(const std::vector<uint8_t>& bytes) {
    if (bytes.size() != 8 + capacity * 3 ||
        std::string(bytes.begin(), bytes.begin() + 4) != "RSG1" ||
        bytes[4] != 1 || bytes[5] || bytes[6] || bytes[7])
        throw std::runtime_error("Unsupported or damaged Unicode glyph dictionary");
    std::array<char16_t, capacity> glyphs{};
    std::array<bool, capacity> native{};
    std::unordered_map<char16_t, uint16_t> tokens;
    for (size_t slot = 0; slot < capacity; ++slot) {
        const auto offset = 8 + slot * 3;
        const char16_t cp = char16_t(bytes[offset] | (uint16_t(bytes[offset + 1]) << 8));
        if (bytes[offset + 2] > 1 || (cp && bytes[offset + 2]) ||
            (cp >= 0xd800 && cp <= 0xdfff) ||
            (cp && !tokens.emplace(cp, tokenValue(slot)).second))
            throw std::runtime_error("Invalid Unicode glyph dictionary entry");
        glyphs[slot] = cp;
        native[slot] = bytes[offset + 2] != 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    glyphs_ = glyphs;
    native_ = native;
    tokens_ = std::move(tokens);
}

std::vector<uint8_t> transformUtf8Gsc(const std::vector<uint8_t>& input, GlyphTokens& tokens) {
    auto script = GscFile::fromBytes(input, "UTF-8");
    const size_t pool = size_t(script.headerLength) + size_t(script.commandLength) +
                        size_t(script.stringDeclarationLength);
    convertEncoding(std::string(reinterpret_cast<const char*>(input.data() + pool),
                               size_t(script.stringDefinitionLength)), "UTF-8", "UTF-8", true);
    // Glyph IDs remain stable even if a later string prevents script delivery.
    for (auto& text : script.strings) {
        const auto encoded = tokens.encode(text);
        text = convertEncoding(encoded, "CP932", "UTF-8", true);
    }
    script.encoding = "CP932";
    return script.toBytes();
}

void reserveNativeGsc(const std::vector<uint8_t>& input, GlyphTokens& tokens) {
    const auto script = GscFile::fromBytes(input, "CP932");
    const size_t begin = size_t(script.headerLength) + size_t(script.commandLength) +
                         size_t(script.stringDeclarationLength);
    tokens.reserveNative(std::string_view(reinterpret_cast<const char*>(input.data() + begin),
                                        size_t(script.stringDefinitionLength)));
}

} // namespace liarsoft::runtime
