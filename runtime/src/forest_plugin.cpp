#include "forest_bridge.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
const RScriptHostContext* host = nullptr;
RScriptForestBridge bridge{};
struct MemoryFile { std::vector<uint8_t> bytes; uint64_t position = 0; };
std::map<HANDLE, std::shared_ptr<MemoryFile>> files;
std::mutex files_mutex;
decltype(&CreateFileA) native_open;
decltype(&ReadFile) native_read;
decltype(&SetFilePointer) native_seek;
decltype(&GetFileSize) native_size;
decltype(&CloseHandle) native_close;
struct Patch { void** slot; void* original; void* replacement; };
std::vector<Patch> patches;

std::wstring wide(const char* text) {
    int count = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (!count) throw std::runtime_error("Invalid CP932 engine filename");
    std::wstring value(count, 0);
    MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, text, -1, value.data(), count);
    value.resize(count - 1);
    return value;
}

[[noreturn]] void fail(const std::string& message) {
    host->log(RSCRIPT_LOG_ERROR, message.c_str());
    int count = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
    std::wstring text(count ? count : 1, 0);
    if (count) MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, text.data(), count);
    MessageBoxW(nullptr, text.c_str(), L"RScript resource error", MB_OK | MB_ICONERROR);
    ExitProcess(ERROR_INVALID_DATA);
}

bool logicalPath(const char* name, std::wstring& relative, uint32_t& kind) {
    std::filesystem::path path(wide(name));
    const auto root = std::filesystem::absolute(std::filesystem::path(host->game_exe).parent_path()).lexically_normal();
    path = std::filesystem::absolute(path).lexically_normal();
    auto part = path.begin();
    for (const auto& rootPart : root) {
        if (part == path.end() || CompareStringOrdinal(rootPart.c_str(), -1, part->c_str(), -1, TRUE) != CSTR_EQUAL)
            return false;
        ++part;
    }
    std::filesystem::path logical;
    for (; part != path.end(); ++part) logical /= *part;
    path = std::move(logical);
    if (path.empty() || path.is_absolute()) return false;
    for (const auto& part : path) if (part == L"..") return false;
    auto ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    if (ext == L".gsc") kind = RScriptScript;
    else if (ext == L".lim" || ext == L".wcg") kind = RScriptImage;
    else if (ext == L".wav") kind = RScriptAudio;
    else if (ext == L".lwg") kind = RScriptComponent;
    else return false;
    relative = path.wstring();
    return true;
}

void takeBuffer(RScriptBuffer& buffer, std::vector<uint8_t>& bytes) {
    struct Release { RScriptBuffer& value; ~Release() { if (value.release) value.release(value.owner); } } release{buffer};
    if ((!buffer.data && buffer.size) || !buffer.release)
        throw std::runtime_error("Resource provider returned an invalid buffer");
    if (buffer.size) bytes.assign(buffer.data, buffer.data + buffer.size);
}

std::shared_ptr<MemoryFile> findFile(HANDLE handle) {
    auto found = files.find(handle);
    return found == files.end() ? nullptr : found->second;
}

HANDLE WINAPI openFile(LPCSTR name, DWORD access, DWORD sharing, LPSECURITY_ATTRIBUTES security,
                      DWORD disposition, DWORD flags, HANDLE template_file) {
    if (!name || !bridge.load_resource || disposition != OPEN_EXISTING ||
        (access & (GENERIC_WRITE | DELETE)) || !(access & GENERIC_READ))
        return native_open(name, access, sharing, security, disposition, flags, template_file);
    try {
        uint32_t kind;
        std::wstring relative;
        if (!logicalPath(name, relative, kind))
            return native_open(name, access, sharing, security, disposition, flags, template_file);
        RScriptBuffer buffer{};
        const char* error = nullptr;
        int result = bridge.load_resource(relative.c_str(), kind, &buffer, &error);
        if (result < 0) throw std::runtime_error(error ? error : "Resource provider failed");
        if (!result) return native_open(name, access, sharing, security, disposition, flags, template_file);
        auto file = std::make_shared<MemoryFile>();
        const uint32_t utf8 = buffer.utf8_script;
        takeBuffer(buffer, file->bytes);
        if (kind == RScriptScript && bridge.transform_script) {
            RScriptBuffer converted{};
            result = bridge.transform_script(file->bytes.data(), static_cast<uint32_t>(file->bytes.size()),
                                            utf8, &converted, &error);
            if (result < 0) throw std::runtime_error(error ? error : "Unicode script conversion failed");
            if (result) takeBuffer(converted, file->bytes);
        }
        // A real kernel handle avoids collisions with native files.
        HANDLE handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!handle) throw std::runtime_error("Cannot allocate resource handle");
        std::lock_guard<std::mutex> lock(files_mutex);
        files.emplace(handle, std::move(file));
        return handle;
    } catch (const std::exception& error) { fail(error.what()); }
}

BOOL WINAPI readFile(HANDLE handle, LPVOID output, DWORD count, LPDWORD read, LPOVERLAPPED overlapped) {
    std::lock_guard<std::mutex> lock(files_mutex);
    auto file = findFile(handle);
    if (!file) return native_read(handle, output, count, read, overlapped);
    if (read) *read = 0;
    if ((!output && count) || overlapped || !read) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    const auto available = file->position >= file->bytes.size() ? 0 : file->bytes.size() - file->position;
    *read = static_cast<DWORD>(std::min<uint64_t>(count, available));
    if (*read) std::memcpy(output, file->bytes.data() + file->position, *read);
    file->position += *read;
    return TRUE;
}

DWORD WINAPI seekFile(HANDLE handle, LONG distance, PLONG high, DWORD method) {
    std::lock_guard<std::mutex> lock(files_mutex);
    auto file = findFile(handle);
    if (!file) return native_seek(handle, distance, high, method);
    int64_t delta = high ? static_cast<int64_t>(*high) * 0x100000000ll + static_cast<uint32_t>(distance) : distance;
    int64_t base = method == FILE_BEGIN ? 0 : method == FILE_CURRENT ? static_cast<int64_t>(file->position) :
                   method == FILE_END ? static_cast<int64_t>(file->bytes.size()) : -1;
    if (base < 0 || delta < -base || (delta > 0 && base > INT64_MAX - delta)) {
        SetLastError(ERROR_NEGATIVE_SEEK); return INVALID_SET_FILE_POINTER;
    }
    file->position = static_cast<uint64_t>(base + delta);
    if (high) *high = static_cast<LONG>(file->position >> 32);
    SetLastError(NO_ERROR);
    return static_cast<DWORD>(file->position);
}

DWORD WINAPI sizeFile(HANDLE handle, LPDWORD high) {
    std::lock_guard<std::mutex> lock(files_mutex);
    auto file = findFile(handle);
    if (!file) return native_size(handle, high);
    if (high) *high = 0;
    return static_cast<DWORD>(file->bytes.size());
}

BOOL WINAPI closeFile(HANDLE handle) {
    std::lock_guard<std::mutex> lock(files_mutex);
    files.erase(handle);
    return native_close(handle);
}

std::string digestFile(const wchar_t* path) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot read target EXE");
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<unsigned char, 32> digest{};
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0;
    std::array<unsigned char, 65536> bytes;
    DWORD read;
    while (ok) {
        if (!ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) { ok = false; break; }
        if (!read) break;
        ok = BCryptHashData(hash, bytes.data(), read, 0) == 0;
    }
    if (ok) ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (!ok) throw std::runtime_error("Cannot hash target EXE");
    const char* hex = "0123456789abcdef";
    std::string result;
    for (auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}

void replace(const Patch& patch, bool restore = false) {
    DWORD protection;
    if (!VirtualProtect(patch.slot, sizeof(void*), PAGE_READWRITE, &protection))
        throw std::runtime_error("Cannot protect Forest import slot");
    InterlockedExchangePointer(patch.slot, restore ? patch.original : patch.replacement);
    DWORD ignored;
    if (!VirtualProtect(patch.slot, sizeof(void*), protection, &ignored))
        throw std::runtime_error("Cannot restore Forest import protection");
}

template<class Function> void add(uint32_t rva, const char* name, Function hook, Function& original) {
    auto** slot = reinterpret_cast<void**>(static_cast<uint8_t*>(host->game_module) + rva);
    FARPROC expected = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name);
    if (*slot != reinterpret_cast<void*>(expected)) throw std::runtime_error(std::string("Forest import was modified: ") + name);
    static_assert(sizeof(original) == sizeof(void*));
    std::memcpy(&original, slot, sizeof(original));
    void* replacement;
    std::memcpy(&replacement, &hook, sizeof(replacement));
    patches.push_back({slot, *slot, replacement});
}

void RSCRIPT_CALL shutdown() {
    for (auto it = patches.rbegin(); it != patches.rend(); ++it) {
        try { replace(*it, true); } catch (...) { host->log(RSCRIPT_LOG_ERROR, "Cannot restore a Forest import"); }
    }
    patches.clear();
    if (host) host->set_engine_api(nullptr);
}

int32_t RSCRIPT_CALL initialize(const RScriptHostContext* context) {
    try {
        if (!context || context->size < sizeof(*context) || sizeof(void*) != 4) return 1;
        host = context;
        if (digestFile(host->game_exe) != "8eb71d07304d362ec26cc47d93c6ef5854b5acadd7c9fe941755ad50820a3e56")
            throw std::runtime_error("This EXE is not the verified Forest build; no hooks were installed");
        bridge = {sizeof(bridge), 1, host->game_module, host, nullptr, nullptr};
        add(0x59098, "CreateFileA", openFile, native_open);
        add(0x5907c, "ReadFile", readFile, native_read);
        add(0x590c8, "SetFilePointer", seekFile, native_seek);
        add(0x590cc, "GetFileSize", sizeFile, native_size);
        add(0x5909c, "CloseHandle", closeFile, native_close);
        for (const auto& patch : patches) replace(patch);
        host->set_engine_api(&bridge);
        host->log(RSCRIPT_LOG_INFO, "Verified Forest 8eb71d07: game-local file hooks installed");
        return 0;
    } catch (const std::exception& error) {
        if (host) host->log(RSCRIPT_LOG_ERROR, error.what());
        shutdown();
        return 1;
    }
}
const RScriptPluginDescriptor descriptor{sizeof(descriptor), RSCRIPT_PLUGIN_ABI,
    "rscript.forest", "Forest engine adapter", RSCRIPT_FEATURE_ENGINE, initialize, shutdown};
}

extern "C" RSCRIPT_EXPORT const RScriptPluginDescriptor* RSCRIPT_CALL
RScriptPlugin_GetDescriptor(uint32_t version) { return version == RSCRIPT_PLUGIN_ABI ? &descriptor : nullptr; }
