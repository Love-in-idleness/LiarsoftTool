#include "win_util.h"
#include "rscript_plugin.h"
#include "kinsoku.h"
#include "utf8_text.h"

#include <tlhelp32.h>
#include <psapi.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace rscript {
namespace {
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

[[noreturn]] void fail(const std::wstring& message) {
    throw std::runtime_error(utf8(message));
}

[[noreturn]] void win_fail(const wchar_t* operation) {
    DWORD code = GetLastError();
    fail(std::wstring(operation) + L": " + error_text(code));
}

uintptr_t remote_module(DWORD process, const std::wstring& path) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process));
    if (snapshot.value != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Module32FirstW(snapshot.value, &entry)) {
            do {
                if (_wcsicmp(entry.szExePath, path.c_str()) == 0 ||
                    _wcsicmp(entry.szModule, path.c_str()) == 0)
                    return reinterpret_cast<uintptr_t>(entry.modBaseAddr);
            } while (Module32NextW(snapshot.value, &entry));
        }
    }
    // A suspended process has not populated its loader list yet; ntdll is mapped.
    if (path.find_first_of(L"\\/") == std::wstring::npos) {
        Handle target(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, process));
        MEMORY_BASIC_INFORMATION region{};
        uintptr_t cursor = 0;
        while (target.value && VirtualQueryEx(target.value, reinterpret_cast<void*>(cursor), &region, sizeof(region))) {
            if (region.Type == MEM_IMAGE && region.AllocationBase == region.BaseAddress) {
                wchar_t mapped[32768]{};
                if (GetMappedFileNameW(target.value, region.BaseAddress, mapped, 32768)) {
                    std::wstring name = mapped;
                    name = name.substr(name.find_last_of(L"\\/") + 1);
                    if (_wcsicmp(name.c_str(), path.c_str()) == 0)
                        return reinterpret_cast<uintptr_t>(region.AllocationBase);
                }
            }
            uintptr_t next = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
            if (next <= cursor) break;
            cursor = next;
        }
    }
    fail(L"Target module not found: " + path);
}

DWORD remote_call(HANDLE process, uintptr_t function, void* argument) {
    Handle thread(CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(function), argument, 0, nullptr));
    if (!thread.value) win_fail(L"Start DLL initialization thread");
    DWORD wait = WaitForSingleObject(thread.value, 60000);
    if (wait != WAIT_OBJECT_0) {
        if (wait == WAIT_FAILED) win_fail(L"Wait for DLL initialization");
        fail(L"DLL initialization timed out; the game was not started");
    }
    DWORD result = 0;
    if (!GetExitCodeThread(thread.value, &result)) win_fail(L"Read DLL initialization result");
    return result;
}

struct RemoteBuffer {
    HANDLE process;
    void* data;
    RemoteBuffer(HANDLE target, const void* bytes, size_t size) : process(target) {
        data = VirtualAllocEx(target, nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!data) win_fail(L"Allocate target initialization data");
        SIZE_T written = 0;
        if (!WriteProcessMemory(target, data, bytes, size, &written) || written != size) {
            VirtualFreeEx(target, data, 0, MEM_RELEASE);
            win_fail(L"Write target initialization data");
        }
    }
    ~RemoteBuffer() { if (data) VirtualFreeEx(process, data, 0, MEM_RELEASE); }
};

void load_host(HANDLE process, DWORD process_id, const std::wstring& path) {
    if (path.size() >= 32767) fail(L"The host DLL path is too long");
    RemoteBuffer name(process, path.c_str(), (path.size() + 1) * sizeof(wchar_t));
    struct UnicodeString { USHORT length, capacity; wchar_t* buffer; };
    UnicodeString string{static_cast<USHORT>(path.size() * sizeof(wchar_t)),
        static_cast<USHORT>((path.size() + 1) * sizeof(wchar_t)), static_cast<wchar_t*>(name.data)};
    RemoteBuffer unicode(process, &string, sizeof(string));
    uintptr_t module = 0;
    RemoteBuffer output(process, &module, sizeof(module));
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    FARPROC loader = GetProcAddress(ntdll, "LdrLoadDll");
    if (!loader) win_fail(L"Resolve native Windows loader");
    uintptr_t loader_address = remote_module(process_id, L"ntdll.dll") +
        (reinterpret_cast<uintptr_t>(loader) - reinterpret_cast<uintptr_t>(ntdll));
    std::vector<unsigned char> code;
    auto address = [&](uintptr_t value) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
        code.insert(code.end(), bytes, bytes + sizeof(value));
    };
    if (sizeof(void*) == 4) {
        code = {0x55, 0x89, 0xe5, 0x68}; // Save frame; push output module.
        address(reinterpret_cast<uintptr_t>(output.data));
        code.push_back(0x68); address(reinterpret_cast<uintptr_t>(unicode.data));
        code.insert(code.end(), {0x6a, 0, 0x6a, 0, 0xb8}); address(loader_address);
        code.insert(code.end(), {0xff, 0xd0, 0x89, 0xec, 0x5d, 0xc2, 4, 0});
    } else {
        code = {0x48, 0x83, 0xec, 0x28, 0x31, 0xc9, 0x31, 0xd2, 0x49, 0xb8};
        address(reinterpret_cast<uintptr_t>(unicode.data));
        code.insert(code.end(), {0x49, 0xb9}); address(reinterpret_cast<uintptr_t>(output.data));
        code.insert(code.end(), {0x48, 0xb8}); address(loader_address);
        code.insert(code.end(), {0xff, 0xd0, 0x48, 0x83, 0xc4, 0x28, 0xc3});
    }
    RemoteBuffer bootstrap(process, code.data(), code.size());
    DWORD previous = 0;
    if (!VirtualProtectEx(process, bootstrap.data, code.size(), PAGE_EXECUTE_READ, &previous) ||
        !FlushInstructionCache(process, bootstrap.data, code.size())) win_fail(L"Prepare DLL loader bootstrap");
    DWORD status = remote_call(process, reinterpret_cast<uintptr_t>(bootstrap.data), nullptr);
    if (static_cast<LONG>(status) < 0)
        fail(L"The game could not load RScriptHost.dll (NTSTATUS " + std::to_wstring(status) + L")");
}

std::vector<unsigned char> make_packet(const LaunchOptions& options, const std::wstring& kinsoku) {
    std::vector<unsigned char> bytes(sizeof(HostInitPacket), 0);
    HostInitPacket header{};
    header.size = sizeof(header);
    header.abi_version = RSCRIPT_PLUGIN_ABI;
    header.plugin_count = static_cast<uint32_t>(options.plugins.size());
    auto append = [&](const std::wstring& text) {
        const size_t size = (text.size() + 1) * sizeof(wchar_t);
        if (size > max_packet_size || bytes.size() > max_packet_size - size)
            fail(L"DLL startup configuration is too large");
        uint32_t offset = static_cast<uint32_t>(bytes.size());
        const auto* begin = reinterpret_cast<const unsigned char*>(text.c_str());
        bytes.insert(bytes.end(), begin, begin + size);
        return offset;
    };
    header.game_offset = append(options.game);
    header.root_offset = append(options.root);
    if (!kinsoku.empty()) header.kinsoku_offset = append(kinsoku);
    for (size_t i = 0; i < options.plugins.size(); ++i)
        header.plugin_offsets[i] = append(options.plugins[i]);
    header.total_size = static_cast<uint32_t>(bytes.size());
    std::memcpy(bytes.data(), &header, sizeof(header));
    return bytes;
}
} // namespace

std::wstring error_text(DWORD code) {
    wchar_t* buffer = nullptr;
    DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = length && buffer ? std::wstring(buffer, length) : L"Windows error " + std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r')) text.pop_back();
    return text;
}

std::wstring utf16(const char* text) {
    if (!text) return {};
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (!length) return L"Invalid UTF-8 error message";
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, &result[0], length);
    result.pop_back();
    return result;
}

std::string utf8(const std::wstring& text) {
    int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.c_str(), -1,
        nullptr, 0, nullptr, nullptr);
    if (!length) return "Invalid UTF-16 error message";
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.c_str(), -1, &result[0], length, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::wstring full_path(const std::wstring& path) {
    if (path.empty()) fail(L"A file path is required");
    DWORD size = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!size) win_fail(L"Resolve file path");
    std::wstring result(size, L'\0');
    DWORD actual = GetFullPathNameW(path.c_str(), size, &result[0], nullptr);
    if (!actual || actual >= size) win_fail(L"Resolve file path");
    result.resize(actual);
    return result;
}

std::wstring module_path(HMODULE module) {
    std::wstring path(32768, L'\0');
    DWORD size = GetModuleFileNameW(module, &path[0], static_cast<DWORD>(path.size()));
    if (!size || size >= path.size()) win_fail(L"Read executable path");
    path.resize(size);
    return path;
}

std::wstring parent_path(const std::wstring& path) {
    size_t end = path.find_last_of(L"\\/");
    return end == std::wstring::npos ? L"." : path.substr(0, end);
}

std::wstring quote_argument(const std::wstring& text) {
    std::wstring quoted = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : text) {
        if (ch == L'\\') { ++slashes; continue; }
        quoted.append(ch == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
        quoted += ch;
        slashes = 0;
    }
    quoted.append(slashes * 2, L'\\');
    return quoted + L"\"";
}

WORD pe_machine(const std::wstring& path) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) win_fail(L"Open executable or DLL");
    IMAGE_DOS_HEADER dos{};
    DWORD read = 0;
    if (!ReadFile(file.value, &dos, sizeof(dos), &read, nullptr) || read != sizeof(dos) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos)))
        fail(L"Invalid PE header: " + path);
    LARGE_INTEGER position{};
    position.QuadPart = dos.e_lfanew;
    if (!SetFilePointerEx(file.value, position, nullptr, FILE_BEGIN)) win_fail(L"Seek PE header");
    struct { DWORD signature; IMAGE_FILE_HEADER file; } header{};
    if (!ReadFile(file.value, &header, sizeof(header), &read, nullptr) || read != sizeof(header) ||
        header.signature != IMAGE_NT_SIGNATURE)
        fail(L"Invalid PE header: " + path);
    return header.file.Machine;
}

std::wstring read_kinsoku(const std::wstring& path) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) win_fail(L"Open kinsoku JSON");
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.value, &size)) win_fail(L"Read kinsoku JSON size");
    if (size.QuadPart < 0 || size.QuadPart > max_kinsoku_size)
        fail(L"Kinsoku JSON must not exceed 64 KiB");
    std::string json(static_cast<size_t>(size.QuadPart), '\0');
    DWORD actual = 0;
    if (!ReadFile(file.value, json.data(), static_cast<DWORD>(json.size()), &actual, nullptr) || actual != json.size())
        fail(L"Could not read the complete kinsoku JSON");
    liarsoft::runtime::parseKinsoku(json);
    const auto decoded = liarsoft::runtime::decodeUtf8(json);
    return {decoded.begin(), decoded.end()};
}

const wchar_t* packet_string(const HostInitPacket* packet, uint32_t offset) {
    if (!packet || offset < sizeof(HostInitPacket) || offset % alignof(wchar_t) ||
        packet->total_size < sizeof(wchar_t) || offset > packet->total_size - sizeof(wchar_t))
        return nullptr;
    const auto* text = reinterpret_cast<const wchar_t*>(reinterpret_cast<const unsigned char*>(packet) + offset);
    size_t count = (packet->total_size - offset) / sizeof(wchar_t);
    for (size_t i = 0; i < count; ++i) if (text[i] == 0) return text;
    return nullptr;
}

DWORD launch(const LaunchOptions& supplied) {
    LaunchOptions options = supplied;
    options.game = full_path(options.game);
    options.root = options.root.empty() ? parent_path(options.game) : full_path(options.root);
    DWORD attributes = GetFileAttributesW(options.root.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        fail(L"Resource directory does not exist: " + options.root);
    if (options.plugins.size() > max_plugins) fail(L"Too many enabled DLLs (maximum 64)");
    const bool unicode = std::any_of(options.plugins.begin(), options.plugins.end(), [](const std::wstring& plugin) {
        const auto name = plugin.substr(plugin.find_last_of(L"\\/") + 1);
        return _wcsicmp(name.c_str(), L"RScriptUnicode.dll") == 0;
    });
    if (options.kinsoku.empty() && unicode) options.kinsoku = parent_path(module_path()) + L"\\kinsoku.json";
    const auto kinsoku = options.kinsoku.empty() ? std::wstring{} : read_kinsoku(full_path(options.kinsoku));
    WORD target_machine = pe_machine(options.game);
    std::wstring host = parent_path(module_path()) + L"\\RScriptHost.dll";
    if (!options.plugins.empty()) {
        if (target_machine != pe_machine(module_path())) fail(L"Launcher and game architectures do not match");
        if (pe_machine(host) != target_machine) fail(L"Host and game architectures do not match");
        for (auto& plugin : options.plugins) {
            plugin = full_path(plugin);
            if (pe_machine(plugin) != target_machine) fail(L"DLL and game architectures do not match: " + plugin);
        }
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command = quote_argument(options.game);
    if (!CreateProcessW(options.game.c_str(), &command[0], nullptr, nullptr, FALSE,
            options.plugins.empty() ? 0 : CREATE_SUSPENDED, nullptr,
            parent_path(options.game).c_str(), &startup, &process)) win_fail(L"Start game");
    Handle process_handle(process.hProcess), thread_handle(process.hThread);
    try {
        if (!options.plugins.empty()) {
            load_host(process.hProcess, process.dwProcessId, host);
            HMODULE local = LoadLibraryExW(host.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
            if (!local) win_fail(L"Read host exports");
            auto entry = GetProcAddress(local, "RScriptHost_Initialize");
            if (!entry) entry = GetProcAddress(local, "RScriptHost_Initialize@4");
            uintptr_t offset = entry ? reinterpret_cast<uintptr_t>(entry) - reinterpret_cast<uintptr_t>(local) : 0;
            FreeLibrary(local);
            if (!offset) fail(L"RScriptHost.dll has no initialization entry");
            auto packet = make_packet(options, kinsoku);
            RemoteBuffer remote(process.hProcess, packet.data(), packet.size());
            DWORD initialized = remote_call(process.hProcess, remote_module(process.dwProcessId, host) + offset, remote.data);
            HostInitPacket result{};
            SIZE_T read = 0;
            if (!ReadProcessMemory(process.hProcess, remote.data, &result, sizeof(result), &read) || read != sizeof(result))
                win_fail(L"Read host initialization result");
            result.error[1023] = 0;
            if (initialized != 0 || result.result != 0)
                fail(result.error[0] ? result.error : L"The extension DLL failed to initialize");
            if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) win_fail(L"Resume game");
        }
    } catch (...) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
        throw;
    }
    if (!options.wait) return 0;
    if (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0) win_fail(L"Wait for game");
    DWORD result = 0;
    if (!GetExitCodeProcess(process.hProcess, &result)) win_fail(L"Read game exit code");
    return result;
}
} // namespace rscript
