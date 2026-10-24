#include "runtime_bridge.h"
#include "engine_text.h"
#include "unicode.h"
#include "fileio.h"
#include "archive_index.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <cstring>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {

using liarsoft::runtime::GlyphTokens;
GlyphTokens glyphs;
RScriptRuntimeBridge* bridge = nullptr;
const RScriptHostContext* host = nullptr;
std::filesystem::path dictionaryPath;
std::string dictionaryIdentity;
std::vector<uint8_t> persisted;
std::mutex transformMutex;
HANDLE dictionaryLock = INVALID_HANDLE_VALUE;

std::string utf8(const std::wstring& text) {
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()),
                                        nullptr, 0, nullptr, nullptr);
    if (!count && !text.empty()) throw std::runtime_error("Cannot encode the Unicode dictionary identity");
    std::string output(size_t(count), '\0');
    if (count && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), int(text.size()),
                                    output.data(), count, nullptr, nullptr) != count)
        throw std::runtime_error("Cannot encode the Unicode dictionary identity");
    return output;
}

std::wstring canonical(const wchar_t* path) {
    if (!path || !*path) throw std::runtime_error("Missing game or override path");
    const DWORD size = GetFullPathNameW(path, 0, nullptr, nullptr);
    if (!size) throw std::runtime_error("Cannot resolve the Unicode dictionary path");
    std::wstring result(size, L'\0');
    const auto written = GetFullPathNameW(path, size, result.data(), nullptr);
    if (!written || written >= size) throw std::runtime_error("Cannot resolve the Unicode dictionary path");
    result.resize(written);
    while (result.size() > 3 && (result.back() == L'\\' || result.back() == L'/')) result.pop_back();
    CharLowerBuffW(result.data(), DWORD(result.size()));
    return result;
}

std::string sha256(const std::string& bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<uint8_t, 32> digest{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Cannot open the Unicode dictionary hash provider");
    auto status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (status >= 0) status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), ULONG(bytes.size()), 0);
    if (status >= 0) status = BCryptFinishHash(hash, digest.data(), ULONG(digest.size()), 0);
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("Cannot hash the Unicode dictionary identity");
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (auto byte : digest) output << std::setw(2) << unsigned(byte);
    return output.str();
}

std::vector<uint8_t> cacheBytes() {
    const auto body = glyphs.serialize();
    std::vector<uint8_t> output{'R', 'S', 'D', '1'};
    const auto count = uint32_t(dictionaryIdentity.size());
    for (unsigned byte = 0; byte < 4; ++byte) output.push_back(uint8_t(count >> (byte * 8)));
    output.insert(output.end(), dictionaryIdentity.begin(), dictionaryIdentity.end());
    output.insert(output.end(), body.begin(), body.end());
    return output;
}

void writeDictionary() {
    auto bytes = cacheBytes();
    if (bytes == persisted) return;
    const auto temporary = dictionaryPath.wstring() + L".tmp-" + std::to_wstring(GetCurrentProcessId());
    HANDLE output = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create a temporary Unicode dictionary file");
    bool closed = false;
    try {
        size_t offset = 0;
        while (offset < bytes.size()) {
            DWORD written = 0;
            if (!WriteFile(output, bytes.data() + offset, DWORD(bytes.size() - offset), &written, nullptr) || !written)
                throw std::runtime_error("Cannot write the Unicode dictionary");
            offset += written;
        }
        if (!CloseHandle(output)) {
            closed = true;
            throw std::runtime_error("Cannot close the Unicode dictionary");
        }
        closed = true;
        if (!MoveFileExW(temporary.c_str(), dictionaryPath.c_str(), MOVEFILE_REPLACE_EXISTING))
            throw std::runtime_error("Cannot replace the Unicode dictionary; the previous copy is intact");
        persisted = std::move(bytes);
    } catch (...) {
        if (!closed) CloseHandle(output);
        DeleteFileW(temporary.c_str());
        throw;
    }
}

void openDictionary() {
    const DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!count) throw std::runtime_error("LOCALAPPDATA is unavailable for the Unicode dictionary");
    std::wstring local(count, L'\0');
    const auto written = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), count);
    if (!written || written >= count) throw std::runtime_error("Cannot read LOCALAPPDATA");
    local.resize(written);
    const auto executable = liarsoft::readFile(std::filesystem::path(host->game_exe));
    dictionaryIdentity = "RScript-v2:" + sha256(std::string(executable.begin(), executable.end())) + "\n" +
        utf8(canonical(host->game_exe)) + "\n" + utf8(canonical(host->override_root));
    const auto key = sha256(dictionaryIdentity);
    const auto folder = std::filesystem::path(local) / L"LiarsoftTool" / L"RScriptRuntime" /
                        std::filesystem::path(std::wstring(key.begin(), key.end()));
    std::filesystem::create_directories(folder);
    dictionaryPath = folder / L"glyphs.bin";
    const auto lockPath = folder / L"glyphs.lock";
    dictionaryLock = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dictionaryLock == INVALID_HANDLE_VALUE)
        throw std::runtime_error("The Unicode dictionary is locked by another game instance or inaccessible");
    if (!std::filesystem::exists(dictionaryPath)) return;
    const liarsoft::ResourceSource dictionary(dictionaryPath);
    if (dictionary.size() > 8 + dictionaryIdentity.size() + 8 + GlyphTokens::capacity * 3)
        throw std::runtime_error("Oversized Unicode dictionary");
    auto bytes = dictionary.readAll();
    if (bytes.size() < 8 || std::string(bytes.begin(), bytes.begin() + 4) != "RSD1")
        throw std::runtime_error("Invalid Unicode dictionary header");
    uint32_t identitySize = 0;
    for (unsigned byte = 0; byte < 4; ++byte) identitySize |= uint32_t(bytes[4 + byte]) << (byte * 8);
    if (identitySize != dictionaryIdentity.size() || identitySize > bytes.size() - 8 ||
        std::string(bytes.begin() + 8, bytes.begin() + 8 + identitySize) != dictionaryIdentity)
        throw std::runtime_error("Unicode dictionary belongs to a different game or override directory");
    glyphs.restore({bytes.begin() + 8 + identitySize, bytes.end()});
    persisted = std::move(bytes);
}

bool gsc(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    CharLowerBuffW(extension.data(), DWORD(extension.size()));
    return extension == L".gsc";
}

void reserveOriginalScripts(const std::filesystem::path& root) {
    for (const auto& item : std::filesystem::recursive_directory_iterator(root)) {
        if (!item.is_regular_file()) continue;
        if (gsc(item.path())) {
            liarsoft::runtime::reserveNativeGsc(liarsoft::readFile(item.path()), glyphs);
            continue;
        }
        auto extension = item.path().extension().wstring();
        CharLowerBuffW(extension.data(), DWORD(extension.size()));
        if (extension != L".xfl") continue;
        const liarsoft::ResourceSource source(item.path());
        const auto index = liarsoft::readArchiveIndex(source);
        for (const auto& entry : index.entries) {
            if (gsc(std::filesystem::u8path(entry.name)))
                liarsoft::runtime::reserveNativeGsc(source.subrange(entry.offset, entry.size).readAll(), glyphs);
        }
    }
}

void reserveOriginalText() {
    // This small startup scan reserves native user-defined characters before
    // any Unicode allocation; it does not decode images/audio or change input.
    const std::filesystem::path original = std::filesystem::path(host->game_exe).parent_path();
    reserveOriginalScripts(original);
    if (canonical(original.c_str()) != canonical(host->override_root))
        reserveOriginalScripts(std::filesystem::path(host->override_root));
    // Static menus/font names can bypass script strings. Conservatively reserve
    // their initialized .rdata/.data byte pairs too, including non-text matches.
    const auto image = liarsoft::readFile(std::filesystem::path(host->game_exe));
    if (image.size() < sizeof(IMAGE_DOS_HEADER))
        throw std::runtime_error("Truncated executable during Unicode initialization");
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        size_t(dos->e_lfanew) > image.size() ||
        sizeof(IMAGE_NT_HEADERS) > image.size() - size_t(dos->e_lfanew))
        throw std::runtime_error("Invalid executable during Unicode initialization");
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS*>(image.data() + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC)
        throw std::runtime_error("Incompatible executable during Unicode initialization");
    const auto* sections = IMAGE_FIRST_SECTION(pe);
    const size_t sectionOffset = reinterpret_cast<const uint8_t*>(sections) - image.data();
    if (sectionOffset > image.size() ||
        pe->FileHeader.NumberOfSections > (image.size() - sectionOffset) / sizeof(IMAGE_SECTION_HEADER))
        throw std::runtime_error("Truncated executable section table");
    for (WORD i = 0; i < pe->FileHeader.NumberOfSections; ++i) {
        const auto& section = sections[i];
        if (std::memcmp(section.Name, ".rdata", 6) && std::memcmp(section.Name, ".data", 5)) continue;
        const auto length = section.Misc.VirtualSize < section.SizeOfRawData
                          ? section.Misc.VirtualSize : section.SizeOfRawData;
        if (section.PointerToRawData > image.size() ||
            length > image.size() - section.PointerToRawData)
            throw std::runtime_error("Invalid executable static-data section");
        glyphs.reserveNative({reinterpret_cast<const char*>(image.data() + section.PointerToRawData), length});
    }
    writeDictionary();
}

void RSCRIPT_CALL releaseBuffer(void* pointer) { delete static_cast<std::vector<uint8_t>*>(pointer); }

int RSCRIPT_CALL transform(const uint8_t* bytes, uint32_t size, uint32_t utf8Script,
                           RScriptBuffer* output, const char** error) {
    thread_local std::string lastError;
    try {
        if (!bytes || !size || !output || utf8Script > 1)
            throw std::runtime_error("Invalid Unicode script-transform arguments");
        std::lock_guard<std::mutex> lock(transformMutex);
        const std::vector<uint8_t> input(bytes, bytes + size);
        if (!utf8Script) {
            liarsoft::runtime::reserveNativeGsc(input, glyphs);
            writeDictionary();
            return 0;
        }
        auto result = std::make_unique<std::vector<uint8_t>>(liarsoft::runtime::transformUtf8Gsc(input, glyphs));
        if (result->size() > UINT32_MAX) throw std::runtime_error("Unicode script is too large");
        writeDictionary();
        *output = {result->data(), uint32_t(result->size()), result.get(), releaseBuffer, 0};
        result.release();
        return 1;
    } catch (const std::exception& exception) {
        lastError = exception.what();
        if (error) *error = lastError.c_str();
        return -1;
    }
}

void RSCRIPT_CALL shutdown() {
    if (bridge && bridge->transform_script == transform) bridge->transform_script = nullptr;
    liarsoft::runtime::removeEngineTextHooks();
    if (dictionaryLock != INVALID_HANDLE_VALUE) CloseHandle(dictionaryLock);
    dictionaryLock = INVALID_HANDLE_VALUE;
    bridge = nullptr;
    host = nullptr;
}

int32_t RSCRIPT_CALL initialize(const RScriptHostContext* context) {
    try {
        bridge = runtimeBridge(context);
        if (!bridge || bridge->transform_script)
            throw std::runtime_error("Unicode requires a single initialized engine adapter");
        host = context;
        openDictionary();
        liarsoft::runtime::installEngineTextHooks(bridge->game_module, glyphs, context->log);
        reserveOriginalText();
        bridge->transform_script = transform;
        if (context->log) context->log(RSCRIPT_LOG_INFO, "Unicode enabled through named GDI imports (BMP glyphs; persistent dictionary)");
        return 0;
    } catch (const std::exception& error) {
        if (context && context->log) context->log(RSCRIPT_LOG_ERROR, error.what());
        shutdown();
        return -1;
    }
}

const RScriptPluginDescriptor descriptor{sizeof(RScriptPluginDescriptor), RSCRIPT_PLUGIN_ABI,
    "liarsoft.unicode", "RScript Unicode text", RSCRIPT_FEATURE_UNICODE, initialize, shutdown};

} // namespace

extern "C" RSCRIPT_EXPORT const RScriptPluginDescriptor* RSCRIPT_CALL
RScriptPlugin_GetDescriptor(uint32_t abi) {
    return abi == RSCRIPT_PLUGIN_ABI ? &descriptor : nullptr;
}
