#include "forest_bridge.h"
#include <windows.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
RScriptForestBridge* bridge;
void RSCRIPT_CALL set_api(void* value) { bridge = static_cast<RScriptForestBridge*>(value); }
void* RSCRIPT_CALL get_api() { return bridge; }
void RSCRIPT_CALL log(uint32_t, const char* message) { std::fprintf(stderr, "%s\n", message); }
void RSCRIPT_CALL release(void*) {}
int RSCRIPT_CALL provide(const wchar_t*, uint32_t kind, RScriptBuffer* output, const char**) {
    static const uint8_t bytes[] = {'P', 'C', 'M', '!', '1', '2'};
    if (kind != RScriptAudio) return 0;
    *output = {bytes, sizeof(bytes), nullptr, release, 0};
    return 1;
}
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> F function(void* image, size_t offset) {
    F result;
    std::memcpy(&result, static_cast<uint8_t*>(image) + offset, sizeof(result));
    return result;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    try {
        std::wstring exe = argv[2];
        if (exe[0] == L'/') exe = L"Z:" + exe;
        for (auto& ch : exe) if (ch == L'/') ch = L'\\';
        HMODULE module = LoadLibraryW(argv[1]);
        require(module != nullptr, "Cannot load adapter");
        auto entry = GetProcAddress(module, RSCRIPT_PLUGIN_ENTRY);
        RScriptGetDescriptor query = nullptr;
        std::memcpy(&query, &entry, sizeof(query));
        require(query != nullptr, "Missing descriptor");
        const auto* descriptor = query(RSCRIPT_PLUGIN_ABI);
        require(descriptor != nullptr, "Missing ABI");
        auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x60000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        require(image != nullptr, "Cannot allocate fixture image");
        const std::array<std::pair<uint32_t, const char*>, 5> imports{{
            {0x59098, "CreateFileA"}, {0x5907c, "ReadFile"}, {0x590c8, "SetFilePointer"},
            {0x590cc, "GetFileSize"}, {0x5909c, "CloseHandle"}}};
        for (const auto& entry : imports) {
            auto address = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), entry.second);
            std::memcpy(image + entry.first, &address, sizeof(address));
        }
        wchar_t cwd[32768];
        require(GetCurrentDirectoryW(32768, cwd) != 0, "Cannot get fixture directory");
        RScriptHostContext context{sizeof(context), RSCRIPT_PLUGIN_ABI, image, module, exe.c_str(), cwd,
            RSCRIPT_FEATURE_ENGINE | RSCRIPT_FEATURE_RESOURCES, log, set_api, get_api};
        require(descriptor->initialize(&context) == 0 && bridge, "Adapter initialization failed");
        bridge->load_resource = provide;
        // Keep the logical path inside the named game's root, not the fixture cwd.
        std::wstring root = exe.substr(0, exe.find_last_of(L'\\')) + L"\\voice\\1234.wav";
        int length = WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, root.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string name(length, 0);
        WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, root.c_str(), -1, name.data(), length, nullptr, nullptr);
        auto open = function<decltype(&CreateFileA)>(image, 0x59098);
        auto read = function<decltype(&ReadFile)>(image, 0x5907c);
        auto seek = function<decltype(&SetFilePointer)>(image, 0x590c8);
        auto size = function<decltype(&GetFileSize)>(image, 0x590cc);
        auto close = function<decltype(&CloseHandle)>(image, 0x5909c);
        HANDLE handle = open(name.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        require(handle != INVALID_HANDLE_VALUE, "Override open failed");
        require(size(handle, nullptr) == 6, "Wrong size");
        char bytes[8]{}; DWORD count = 0;
        require(read(handle, bytes, 4, &count, nullptr) && count == 4 && !std::memcmp(bytes, "PCM!", 4), "Wrong bytes");
        require(seek(handle, -1, nullptr, FILE_CURRENT) == 3, "Relative seek failed");
        require(read(handle, bytes, 8, &count, nullptr) && count == 3 && !std::memcmp(bytes, "!12", 3), "Wrong partial read");
        require(read(handle, bytes, 8, &count, nullptr) && count == 0, "EOF failed");
        require(seek(handle, -10, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER && GetLastError() == ERROR_NEGATIVE_SEEK,
                "Negative seek accepted");
        require(close(handle), "Close failed");
        // Windows paths are case-insensitive even when the EXE was selected
        // with a different spelling from a later absolute engine filename.
        std::wstring alternate = root;
        CharUpperBuffW(alternate.data(), static_cast<DWORD>(alternate.size()));
        length = WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, alternate.c_str(), -1, nullptr, 0, nullptr, nullptr);
        name.assign(length, 0);
        WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, alternate.c_str(), -1, name.data(), length, nullptr, nullptr);
        handle = open(name.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        require(handle != INVALID_HANDLE_VALUE && size(handle, nullptr) == 6,
                "Case-insensitive absolute override was skipped");
        require(read(handle, bytes, 4, &count, nullptr) && count == 4 && !std::memcmp(bytes, "PCM!", 4),
                "Case-insensitive absolute override returned native bytes");
        require(close(handle), "Case-insensitive override close failed");
        descriptor->shutdown();
        require(!bridge, "Bridge survived shutdown");
        for (const auto& entry : imports) {
            auto actual = function<FARPROC>(image, entry.first);
            require(actual == GetProcAddress(GetModuleHandleW(L"kernel32.dll"), entry.second), "Import was not restored");
        }
        // A modified target must fail without changing any imports.
        context.game_exe = L"C:\\no-such-game.exe";
        require(descriptor->initialize(&context) != 0, "Unknown executable accepted");
        VirtualFree(image, 0, MEM_RELEASE);
        std::puts("Forest verified-profile IAT resource delivery passed (game not executed)");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
