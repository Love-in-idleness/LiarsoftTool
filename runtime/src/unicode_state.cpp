#include "unicode_state.h"
#include "pe_image.h"
#include "archive_index.h"
#include "resource_provider.h"
#include "fileio.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <iomanip>
#include <sstream>
#endif

namespace liarsoft::runtime {
namespace fs = std::filesystem;
namespace {
std::string extension(const fs::path& path) {
    auto ext = path.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
    return ext;
}
void report(const ScanCallbacks& callbacks, const std::string& source, const std::exception& error) {
    if (callbacks.error) callbacks.error(source, error.what());
    else throw std::runtime_error(source + ": " + error.what());
}
void archive(const ResourceSource& source, const std::string& name, GlyphTokens& tokens,
             const ScanCallbacks& callbacks, unsigned depth) {
    if (depth > 16) throw std::runtime_error("Native script archive nesting exceeds 16 levels");
    const auto index = readArchiveIndex(source);
    for (const auto& entry : index.entries) {
        const auto label = name + "!" + entry.name;
        if (callbacks.progress) callbacks.progress(label);
        try {
            const auto ext = extension(fs::u8path(entry.name));
            if (ext == ".gsc") reserveNativeGsc(source.subrange(entry.offset, entry.size).readAll(), tokens);
            else if (ext == ".xfl") archive(source.subrange(entry.offset, entry.size), label, tokens, callbacks, depth + 1);
        } catch (const std::exception& error) { report(callbacks, label, error); }
    }
}
void scripts(const fs::path& root, GlyphTokens& tokens, const ScanCallbacks& callbacks, bool overrides) {
    for (const auto& item : fs::recursive_directory_iterator(root)) {
        const auto name = item.path().u8string();
        if (callbacks.progress) callbacks.progress(name);
        try {
            if (!item.is_regular_file()) continue;
            const auto ext = extension(item.path());
            if (ext == ".gsc") {
                // A shadowed legacy file is not CP932 input to the Unicode renderer.
                if (overrides) {
                    const auto selected = selectResource(root, item.path().lexically_relative(root), ResourceKind::Script);
                    if (selected && extension(*selected) == ".tsc") continue;
                }
                reserveNativeGsc(readFile(item.path()), tokens);
            } else if (ext == ".xfl") archive(ResourceSource(item.path()), name, tokens, callbacks, 0);
        } catch (const std::exception& error) { report(callbacks, name, error); }
    }
}
}

void reserveOriginalText(const fs::path& game, const fs::path& root, GlyphTokens& tokens,
                         const ScanCallbacks& callbacks) {
    const auto original = fs::canonical(fs::absolute(game).parent_path());
    for (const auto& directory : {original, fs::canonical(root)}) {
        if (directory != original && fs::equivalent(directory, original)) continue;
        try { scripts(directory, tokens, callbacks, directory != original); }
        catch (const std::exception& error) { report(callbacks, directory.u8string(), error); }
        if (directory == original && fs::equivalent(root, original)) break;
    }
    if (callbacks.progress) callbacks.progress(game.u8string());
    try {
        const auto image = PeImage::read(game);
        for (const auto& section : image.sections) {
            if (section.name != ".data" && section.name != ".rdata") continue;
            const auto bytes = image.sectionBytes(section);
            tokens.reserveNative({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
        }
    } catch (const std::exception& error) { report(callbacks, game.u8string(), error); }
}

std::vector<uint8_t> dictionaryBytes(const std::string& identity, const GlyphTokens& tokens) {
    if (identity.size() > UINT32_MAX) throw std::runtime_error("Unicode dictionary identity is too long");
    std::vector<uint8_t> output{'R','S','D','1'};
    for (unsigned byte = 0; byte < 4; ++byte) output.push_back(uint8_t(identity.size() >> (byte * 8)));
    output.insert(output.end(), identity.begin(), identity.end());
    const auto body = tokens.serialize();
    output.insert(output.end(), body.begin(), body.end());
    return output;
}

void restoreDictionary(const std::vector<uint8_t>& bytes, const std::string& identity, GlyphTokens& tokens) {
    if (bytes.size() < 8 || std::string(bytes.begin(), bytes.begin() + 4) != "RSD1")
        throw std::runtime_error("Invalid Unicode dictionary header");
    uint32_t length = 0;
    for (unsigned byte = 0; byte < 4; ++byte) length |= uint32_t(bytes[4 + byte]) << (byte * 8);
    if (length != identity.size() || length > bytes.size() - 8 ||
        std::string(bytes.begin() + 8, bytes.begin() + 8 + length) != identity)
        throw std::runtime_error("Unicode dictionary belongs to a different game or override directory");
    tokens.restore({bytes.begin() + 8 + length, bytes.end()});
}

#ifdef _WIN32
namespace {
std::string identityPath(const fs::path& path) {
    if (path.empty()) throw std::runtime_error("Missing game or override path");
    const auto count = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("Cannot resolve the Unicode dictionary path");
    std::wstring value(count, L'\0');
    const auto length = GetFullPathNameW(path.c_str(), count, value.data(), nullptr);
    if (!length || length >= count) throw std::runtime_error("Cannot resolve the Unicode dictionary path");
    value.resize(length);
    while (value.size() > 3 && (value.back() == L'\\' || value.back() == L'/')) value.pop_back();
    CharLowerBuffW(value.data(), DWORD(value.size()));
    const auto n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
    if (!n) throw std::runtime_error("Cannot encode the Unicode dictionary identity");
    std::string output(size_t(n), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), int(value.size()), output.data(), n, nullptr, nullptr) != n)
        throw std::runtime_error("Cannot encode the Unicode dictionary identity");
    return output;
}
std::string sha256(const std::string& bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<uint8_t,32> digest{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Cannot open the Unicode dictionary hash provider");
    auto status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    // BCrypt's input count is 32-bit; feed larger executables without truncation.
    for (size_t offset = 0; status >= 0 && offset < bytes.size();) {
        const auto count = ULONG(std::min<size_t>(bytes.size() - offset, 1024 * 1024));
        status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data() + offset)), count, 0);
        offset += count;
    }
    if (status >= 0) status = BCryptFinishHash(hash, digest.data(), ULONG(digest.size()), 0);
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("Cannot hash the Unicode dictionary identity");
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (auto byte : digest) output << std::setw(2) << unsigned(byte);
    return output.str();
}
}
DictionaryLocation dictionaryLocation(const fs::path& game, const fs::path& root) {
    const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!count) throw std::runtime_error("LOCALAPPDATA is unavailable for the Unicode dictionary");
    std::wstring local(count, L'\0');
    const auto n = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), count);
    if (!n || n >= count) throw std::runtime_error("Cannot read LOCALAPPDATA");
    local.resize(n);
    const auto executable = readFile(game);
    auto identity = "RScript-v2:" + sha256({executable.begin(), executable.end()}) + "\n" + identityPath(game) + "\n" + identityPath(root);
    return {fs::path(local) / L"LiarsoftTool" / L"RScriptRuntime" / fs::u8path(sha256(identity)) / L"glyphs.bin", std::move(identity)};
}
#endif
}
