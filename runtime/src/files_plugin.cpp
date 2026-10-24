#include "runtime_bridge.h"
#include "imports.h"
#include <windows.h>
#include <algorithm>
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
RScriptRuntimeBridge bridge{};
struct MemoryFile { std::vector<uint8_t> bytes; uint64_t position = 0; };
std::map<HANDLE, std::shared_ptr<MemoryFile>> files;
std::mutex files_mutex;
decltype(&CreateFileA) native_open;
decltype(&ReadFile) native_read;
decltype(&SetFilePointer) native_seek;
decltype(&GetFileSize) native_size;
decltype(&CloseHandle) native_close;
std::vector<liarsoft::runtime::ImportPatch> patches;

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

template<class Function> void add(const char* name, Function hook, Function& original) {
    auto* address = liarsoft::runtime::hookImport(host->game_module, name,
        reinterpret_cast<void*>(hook), patches);
    if (!address) throw std::runtime_error(std::string("Required synchronous file API is not imported: ") + name);
    std::memcpy(&original, &address, sizeof(original));
}

void RSCRIPT_CALL shutdown() {
    try { liarsoft::runtime::restoreImports(patches); }
    catch (...) { host->log(RSCRIPT_LOG_ERROR, "Cannot restore file imports safely"); TerminateProcess(GetCurrentProcess(), ERROR_INVALID_ADDRESS); }
    if (host) host->set_engine_api(nullptr);
}

int32_t RSCRIPT_CALL initialize(const RScriptHostContext* context) {
    try {
        if (!context || context->size < sizeof(*context) || context->abi_version != RSCRIPT_PLUGIN_ABI) return 1;
        host = context;
        bridge = {sizeof(bridge), 2, host->game_module, host, nullptr, nullptr};
        add("CreateFileA", openFile, native_open);
        add("ReadFile", readFile, native_read);
        add("SetFilePointer", seekFile, native_seek);
        add("GetFileSize", sizeFile, native_size);
        add("CloseHandle", closeFile, native_close);
        host->set_engine_api(&bridge);
        host->log(RSCRIPT_LOG_INFO, "Synchronous file imports resolved by name; no EXE profile is required");
        return 0;
    } catch (const std::exception& error) {
        if (host) host->log(RSCRIPT_LOG_ERROR, error.what());
        shutdown();
        return 1;
    }
}
const RScriptPluginDescriptor descriptor{sizeof(descriptor), RSCRIPT_PLUGIN_ABI,
    "rscript.files", "RScript file overrides", RSCRIPT_FEATURE_ENGINE, initialize, shutdown};
}

extern "C" RSCRIPT_EXPORT const RScriptPluginDescriptor* RSCRIPT_CALL
RScriptPlugin_GetDescriptor(uint32_t version) { return version == RSCRIPT_PLUGIN_ABI ? &descriptor : nullptr; }
