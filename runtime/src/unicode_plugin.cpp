#include "runtime_bridge.h"
#include "engine_text.h"
#include "native_kinsoku.h"
#include "unicode.h"
#include "unicode_state.h"
#include "archive_index.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <memory>
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

void writeDictionary() {
    auto bytes = liarsoft::runtime::dictionaryBytes(dictionaryIdentity, glyphs);
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
    const auto location = liarsoft::runtime::dictionaryLocation(host->game_exe, host->override_root);
    dictionaryPath = location.file;
    dictionaryIdentity = location.identity;
    std::filesystem::create_directories(dictionaryPath.parent_path());
    const auto lockPath = dictionaryPath.parent_path() / L"glyphs.lock";
    dictionaryLock = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dictionaryLock == INVALID_HANDLE_VALUE)
        throw std::runtime_error("The Unicode dictionary is locked by another game instance or inaccessible");
    if (!std::filesystem::exists(dictionaryPath)) return;
    const liarsoft::ResourceSource dictionary(dictionaryPath);
    if (dictionary.size() > 8 + dictionaryIdentity.size() + 8 + GlyphTokens::capacity * 3)
        throw std::runtime_error("Oversized Unicode dictionary");
    auto bytes = dictionary.readAll();
    liarsoft::runtime::restoreDictionary(bytes, dictionaryIdentity, glyphs);
    persisted = std::move(bytes);
}

void reserveOriginalText() {
    liarsoft::runtime::reserveOriginalText(host->game_exe, host->override_root, glyphs);
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
    try { liarsoft::runtime::removeNativeKinsoku(); }
    catch (...) {
        if (host && host->log) host->log(RSCRIPT_LOG_ERROR, "Cannot restore line-break hooks safely");
        TerminateProcess(GetCurrentProcess(), ERROR_INVALID_ADDRESS);
    }
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
        if (!context->kinsoku_json) throw std::runtime_error("Unicode requires a line-break JSON configuration");
        const auto rules = liarsoft::runtime::parseKinsoku(context->kinsoku_json);
        openDictionary();
        liarsoft::runtime::installEngineTextHooks(bridge->game_module, glyphs, context->log);
        const auto classifiers = liarsoft::runtime::installNativeKinsoku(bridge->game_module, glyphs, rules);
        if (context->log) context->log(RSCRIPT_LOG_INFO,
            ("UTF-8 line-break rules connected to " + std::to_string(classifiers) + " shared classifier(s)").c_str());
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
