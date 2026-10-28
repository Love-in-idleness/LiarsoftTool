#pragma once
#include "unicode.h"
#include <filesystem>
#include <functional>

namespace liarsoft::runtime {
struct ScanCallbacks {
    std::function<void(const std::string&)> progress;
    std::function<void(const std::string&, const std::string&)> error;
};
void reserveOriginalText(const std::filesystem::path& game, const std::filesystem::path& root,
                         GlyphTokens& tokens, const ScanCallbacks& callbacks = {});
std::vector<uint8_t> dictionaryBytes(const std::string& identity, const GlyphTokens& tokens);
void restoreDictionary(const std::vector<uint8_t>& bytes, const std::string& identity, GlyphTokens& tokens);
#ifdef _WIN32
struct DictionaryLocation { std::filesystem::path file; std::string identity; };
DictionaryLocation dictionaryLocation(const std::filesystem::path& game, const std::filesystem::path& root);
#endif
}
