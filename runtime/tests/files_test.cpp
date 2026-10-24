#include "runtime_bridge.h"
#include "import_fixture.h"
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
RScriptRuntimeBridge* bridge;
void RSCRIPT_CALL set_api(void* value) { bridge = static_cast<RScriptRuntimeBridge*>(value); }
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
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    try {
        HMODULE module = LoadLibraryW(argv[1]);
        require(module != nullptr, "Cannot load adapter");
        auto entry = GetProcAddress(module, RSCRIPT_PLUGIN_ENTRY);
        RScriptGetDescriptor query = nullptr;
        std::memcpy(&query, &entry, sizeof(query));
        require(query != nullptr, "Missing descriptor");
        const auto* descriptor = query(RSCRIPT_PLUGIN_ABI);
        require(descriptor != nullptr, "Missing ABI");
        ImportFixture image(0x9000);
        for (const auto* name : {"CreateFileA", "ReadFile", "SetFilePointer", "GetFileSize", "CloseHandle"})
            image.add("kernel32.dll", name);
        wchar_t cwd[32768];
        require(GetCurrentDirectoryW(32768, cwd) != 0, "Cannot get fixture directory");
        const std::wstring exe = std::wstring(cwd) + L"\\UnknownGame.exe";
        RScriptHostContext context{sizeof(context), RSCRIPT_PLUGIN_ABI, image.image, module, exe.c_str(), cwd,
            RSCRIPT_FEATURE_ENGINE | RSCRIPT_FEATURE_RESOURCES, log, set_api, get_api};
        require(descriptor->initialize(&context) == 0 && bridge, "Adapter initialization failed");
        bridge->load_resource = provide;
        // Keep the logical path inside the named game's root, not the fixture cwd.
        std::wstring root = exe.substr(0, exe.find_last_of(L'\\')) + L"\\voice\\1234.wav";
        int length = WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, root.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string name(length, 0);
        WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, root.c_str(), -1, name.data(), length, nullptr, nullptr);
        auto open = image.function<decltype(&CreateFileA)>("CreateFileA");
        auto read = image.function<decltype(&ReadFile)>("ReadFile");
        auto seek = image.function<decltype(&SetFilePointer)>("SetFilePointer");
        auto size = image.function<decltype(&GetFileSize)>("GetFileSize");
        auto close = image.function<decltype(&CloseHandle)>("CloseHandle");
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
        for (const auto& entry : image.slots) {
            auto actual = image.function<FARPROC>(entry.first.c_str());
            require(actual == GetProcAddress(GetModuleHandleW(L"kernel32.dll"), entry.first.c_str()), "Import was not restored");
        }
        *image.slots.at("CreateFileA") = reinterpret_cast<void*>(&provide);
        require(descriptor->initialize(&context) != 0, "Conflicting file hook was accepted");
        std::puts("Named-import file delivery passed without any game name/hash/address profile");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
