#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace liarsoft::runtime {

// Forest stores characters and font-cache keys as 16-bit DBCS values. Keep the
// original character walker, using unused CP932 EUDC slots as lossless glyph IDs.
class GlyphTokens {
public:
    static constexpr size_t capacity = 1880;

    std::string encode(std::string_view utf8);
    std::u16string decode(std::string_view transport) const;
    std::optional<char16_t> glyph(uint16_t token) const;
    bool containsTokens(std::string_view transport) const;
    void reserveNative(std::string_view cp932);
    size_t size() const;
    std::vector<uint8_t> serialize() const;
    void restore(const std::vector<uint8_t>& bytes);

private:
    mutable std::mutex mutex_;
    std::array<char16_t, capacity> glyphs_{};
    std::array<bool, capacity> native_{};
    std::unordered_map<char16_t, uint16_t> tokens_;
    std::unordered_map<char16_t, std::string> nativeEncoding_;
};

// These accept the legacy sectioned GSC layout used by the verified Forest EXE.
std::vector<uint8_t> transformUtf8Gsc(const std::vector<uint8_t>& input,
                                      GlyphTokens& tokens);
void reserveNativeGsc(const std::vector<uint8_t>& input, GlyphTokens& tokens);

} // namespace liarsoft::runtime
