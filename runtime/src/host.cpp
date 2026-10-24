#include "rscript_plugin.h"
#include "win_util.h"

#include <shlobj.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
struct Plugin {
    HMODULE module;
    const RScriptPluginDescriptor* descriptor;
    bool initialized = false;
};

std::vector<Plugin> plugins;
RScriptHostContext context{};
std::wstring game_path, root_path;
void* engine_api = nullptr;
bool initialized = false;
SRWLOCK log_lock = SRWLOCK_INIT;
HANDLE log_file = INVALID_HANDLE_VALUE;
std::wstring log_path;

void initialize_logging() noexcept {
    if (log_file != INVALID_HANDLE_VALUE) return;
    try {
        wchar_t local[MAX_PATH]{};
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                nullptr, SHGFP_TYPE_CURRENT, local))) return;
        std::wstring directory = std::wstring(local) + L"\\LiarsoftTool";
        for (unsigned depth = 0; depth < 2; ++depth) {
            if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return;
            DWORD attributes = GetFileAttributesW(directory.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return;
            if (!depth) directory += L"\\RScriptRuntime";
        }
        log_path = directory + L"\\host-" + std::to_wstring(GetCurrentProcessId()) + L".log";
        log_file = CreateFileW(log_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE |
            FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (log_file == INVALID_HANDLE_VALUE) { log_path.clear(); return; }
        OutputDebugStringW((L"[RScript] UTF-8 log: " + log_path + L"\n").c_str());
    } catch (...) {
        OutputDebugStringA("[RScript] Could not initialize persistent logging\n");
    }
}

void RSCRIPT_CALL log_message(uint32_t level, const char* message) {
    const char* prefix = level == RSCRIPT_LOG_ERROR ? "[RScript error] " :
        level == RSCRIPT_LOG_WARNING ? "[RScript warning] " : "[RScript] ";
    AcquireSRWLockExclusive(&log_lock);
    OutputDebugStringA(prefix);
    OutputDebugStringA(message ? message : "");
    OutputDebugStringA("\n");
    if (log_file != INVALID_HANDLE_VALUE) {
        SYSTEMTIME time{};
        GetSystemTime(&time);
        char timestamp[80]{};
        int length = std::snprintf(timestamp, sizeof(timestamp),
            "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ [%u] ", time.wYear, time.wMonth,
            time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, level);
        size_t message_length = 0;
        while (message && message_length < 65536 && message[message_length]) ++message_length;
        DWORD written = 0;
        bool ok = length > 0 && WriteFile(log_file, timestamp, static_cast<DWORD>(length), &written, nullptr) &&
            written == static_cast<DWORD>(length);
        if (ok && message_length)
            ok = WriteFile(log_file, message, static_cast<DWORD>(message_length), &written, nullptr) && written == message_length;
        if (ok) ok = WriteFile(log_file, "\r\n", 2, &written, nullptr) && written == 2;
        if (!ok) OutputDebugStringA("[RScript] Persistent log write failed; debugger output remains available\n");
    }
    ReleaseSRWLockExclusive(&log_lock);
}

void RSCRIPT_CALL set_engine_api(void* api) { engine_api = api; }
void* RSCRIPT_CALL get_engine_api() { return engine_api; }

void rollback() {
    for (auto it = plugins.rbegin(); it != plugins.rend(); ++it) {
        if (it->initialized && it->descriptor->shutdown) {
            try { it->descriptor->shutdown(); }
            catch (...) { log_message(RSCRIPT_LOG_ERROR, "A DLL threw during shutdown"); }
        }
    }
    // Keep modules mapped: a failed plugin may already have installed hooks.
    plugins.clear();
    engine_api = nullptr;
    initialized = false;
}

int plugin_order(const Plugin& plugin) {
    uint32_t features = plugin.descriptor->features;
    if (features & RSCRIPT_FEATURE_ENGINE) return 0;
    if (features & RSCRIPT_FEATURE_RESOURCES) return 1;
    if (features & RSCRIPT_FEATURE_UNICODE) return 2;
    return 3;
}

void initialize(rscript::HostInitPacket* packet) {
    if (initialized || !plugins.empty()) throw std::runtime_error("The host has already been initialized");
    if (packet->size != sizeof(*packet) || packet->abi_version != RSCRIPT_PLUGIN_ABI ||
        packet->total_size < sizeof(*packet) || packet->total_size > rscript::max_packet_size ||
        packet->plugin_count > rscript::max_plugins)
        throw std::runtime_error("Invalid host initialization packet");
    const wchar_t* game = rscript::packet_string(packet, packet->game_offset);
    const wchar_t* root = rscript::packet_string(packet, packet->root_offset);
    if (!game || !root || !game[0] || !root[0]) throw std::runtime_error("Invalid game or resource path");
    game_path = game;
    root_path = root;
    if (_wcsicmp(rscript::full_path(game_path).c_str(), rscript::module_path().c_str()) != 0)
        throw std::runtime_error("The host configuration does not name the current game");
    log_message(RSCRIPT_LOG_INFO, ("Game: " + rscript::utf8(game_path)).c_str());
    log_message(RSCRIPT_LOG_INFO, ("Override resources: " + rscript::utf8(root_path)).c_str());
    std::set<std::string> ids;
    uint32_t features = 0;
    for (uint32_t i = 0; i < packet->plugin_count; ++i) {
        const wchar_t* path = rscript::packet_string(packet, packet->plugin_offsets[i]);
        if (!path || !path[0]) throw std::runtime_error("Invalid extension DLL path");
        HMODULE module = LoadLibraryExW(path, nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module) {
            DWORD error = GetLastError();
            throw std::runtime_error("Could not load extension DLL " + rscript::utf8(path) +
                ": " + rscript::utf8(rscript::error_text(error)));
        }
        FARPROC entry = GetProcAddress(module, RSCRIPT_PLUGIN_ENTRY);
        RScriptGetDescriptor query = nullptr;
        static_assert(sizeof(query) == sizeof(entry), "Windows function pointer size mismatch");
        std::memcpy(&query, &entry, sizeof(query));
        if (!query) throw std::runtime_error("An enabled DLL does not export " RSCRIPT_PLUGIN_ENTRY);
        const RScriptPluginDescriptor* descriptor = query(RSCRIPT_PLUGIN_ABI);
        if (!descriptor || descriptor->size < sizeof(*descriptor) ||
            descriptor->abi_version != RSCRIPT_PLUGIN_ABI || !descriptor->id || !descriptor->id[0] ||
            !descriptor->name || !descriptor->initialize)
            throw std::runtime_error("An enabled DLL has an incompatible plugin ABI");
        if (!ids.insert(descriptor->id).second) throw std::runtime_error("Duplicate plugin ID: " + std::string(descriptor->id));
        const uint32_t builtins = descriptor->features &
            (RSCRIPT_FEATURE_ENGINE | RSCRIPT_FEATURE_RESOURCES | RSCRIPT_FEATURE_UNICODE);
        if (builtins & features) throw std::runtime_error("More than one DLL provides the same built-in feature");
        features |= builtins;
        plugins.push_back({module, descriptor, false});
        log_message(RSCRIPT_LOG_INFO, ("Enabled DLL: " + std::string(descriptor->id) + " (" +
            rscript::utf8(path) + ")").c_str());
    }
    if ((features & (RSCRIPT_FEATURE_RESOURCES | RSCRIPT_FEATURE_UNICODE)) && !(features & RSCRIPT_FEATURE_ENGINE))
        throw std::runtime_error("Resource and Unicode extensions require an enabled engine adapter");
    std::stable_sort(plugins.begin(), plugins.end(), [](const Plugin& a, const Plugin& b) {
        return plugin_order(a) < plugin_order(b);
    });
    HMODULE host = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&log_message), &host))
        throw std::runtime_error("Could not identify the host module");
    context = {sizeof(context), RSCRIPT_PLUGIN_ABI, GetModuleHandleW(nullptr), host,
        game_path.c_str(), root_path.c_str(), features, log_message, set_engine_api, get_engine_api};
    for (auto& plugin : plugins) {
        int32_t result = plugin.descriptor->initialize(&context);
        if (result != 0) throw std::runtime_error(std::string(plugin.descriptor->id) +
            " initialization failed (" + std::to_string(result) + ")");
        plugin.initialized = true;
        log_message(RSCRIPT_LOG_INFO, plugin.descriptor->name);
    }
    initialized = true;
    log_message(RSCRIPT_LOG_INFO, "Initialization complete; game entry may resume");
}
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI RScriptHost_Initialize(void* data) {
    auto* packet = static_cast<rscript::HostInitPacket*>(data);
    if (!packet) return ERROR_INVALID_PARAMETER;
    initialize_logging();
    if (initialized) {
        packet->result = ERROR_ALREADY_INITIALIZED;
        wcscpy(packet->error, L"The host has already been initialized");
        return ERROR_ALREADY_INITIALIZED;
    }
    try {
        initialize(packet);
        packet->result = 0;
        packet->error[0] = 0;
        return 0;
    } catch (const std::exception& error) {
        log_message(RSCRIPT_LOG_ERROR, error.what());
        std::wstring message = rscript::utf16(error.what());
        if (!log_path.empty()) message += L"\nLog: " + log_path;
        wcsncpy(packet->error, message.c_str(), 1023);
        packet->error[1023] = 0;
    } catch (...) {
        log_message(RSCRIPT_LOG_ERROR, "An extension DLL threw an unexpected exception");
        wcscpy(packet->error, L"An extension DLL threw an unexpected exception");
    }
    rollback();
    packet->result = ERROR_DLL_INIT_FAILED;
    return ERROR_DLL_INIT_FAILED;
}

extern "C" __declspec(dllexport) DWORD WINAPI RScriptHost_Shutdown(void*) {
    rollback();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
