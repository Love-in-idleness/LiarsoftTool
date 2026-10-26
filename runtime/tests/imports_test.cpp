#include "imports.h"
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using liarsoft::runtime::ImportPatch;
using liarsoft::runtime::hookImport;
using liarsoft::runtime::restoreImports;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void replacement() {}
void* replacementAddress() {
    auto fn = replacement;
    void* address;
    std::memcpy(&address, &fn, sizeof(address));
    return address;
}
void* api(const char* name) {
    auto fn = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), name);
    void* address;
    std::memcpy(&address, &fn, sizeof(address));
    require(address != nullptr, "Missing test API");
    return address;
}
template<class F> void rejects(F fn, const char* message) {
    try { fn(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}

struct Fixture {
    uint8_t* image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x4000,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    size_t shift;
    explicit Fixture(size_t offset = 0) : shift(offset) {
        require(image != nullptr, "Cannot allocate PE fixture");
        auto& dos = at<IMAGE_DOS_HEADER>(0);
        dos.e_magic = IMAGE_DOS_SIGNATURE;
        dos.e_lfanew = 0x80;
        auto& nt = headers();
        nt.Signature = IMAGE_NT_SIGNATURE;
#ifdef _WIN64
        nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
#else
        nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
#endif
        nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER);
        nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR_MAGIC;
        nt.OptionalHeader.SizeOfHeaders = 0x200;
        nt.OptionalHeader.SizeOfImage = 0x4000;
        nt.OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
        nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] =
            {static_cast<DWORD>(0x200 + shift), 3 * sizeof(IMAGE_IMPORT_DESCRIPTOR)};
        std::memcpy(image + 0x300 + shift, "kernel32.dll", 13);
        name(0x400 + shift, "GetFileSize");
        name(0x440 + shift, "CloseHandle");
        for (size_t index = 0; index < 2; ++index) {
            auto& d = descriptor(index);
            d.Name = static_cast<DWORD>(0x300 + shift);
            d.OriginalFirstThunk = static_cast<DWORD>(0x500 + shift + index * 0x80);
            d.FirstThunk = static_cast<DWORD>(0x700 + shift + index * 0x80);
            at<IMAGE_THUNK_DATA>(d.OriginalFirstThunk).u1.AddressOfData = 0x400 + shift;
            at<IMAGE_THUNK_DATA>(d.OriginalFirstThunk + sizeof(IMAGE_THUNK_DATA)).u1.AddressOfData = 0x440 + shift;
            at<IMAGE_THUNK_DATA>(d.FirstThunk).u1.Function = reinterpret_cast<uintptr_t>(api("GetFileSize"));
            at<IMAGE_THUNK_DATA>(d.FirstThunk + sizeof(IMAGE_THUNK_DATA)).u1.Function = reinterpret_cast<uintptr_t>(api("CloseHandle"));
        }
    }
    Fixture(const Fixture&) = delete;
    ~Fixture() { VirtualFree(image, 0, MEM_RELEASE); }
    template<class T> T& at(size_t rva) { return *reinterpret_cast<T*>(image + rva); }
    IMAGE_NT_HEADERS& headers() { return at<IMAGE_NT_HEADERS>(0x80); }
    IMAGE_IMPORT_DESCRIPTOR& descriptor(size_t index) {
        return at<IMAGE_IMPORT_DESCRIPTOR>(0x200 + shift + index * sizeof(IMAGE_IMPORT_DESCRIPTOR));
    }
    void** slot(size_t index) { return reinterpret_cast<void**>(image + descriptor(index).FirstThunk); }
    void name(size_t rva, const char* text) { std::memcpy(image + rva + sizeof(WORD), text, std::strlen(text) + 1); }
};
}

int main() {
    try {
        for (size_t offset : {size_t(0), size_t(0x1000)}) {
            Fixture fixture(offset);
            DWORD old = 0;
            require(VirtualProtect(fixture.image, 0x4000, PAGE_READONLY, &old), "Cannot protect fixture");
            std::vector<ImportPatch> patches;
            require(hookImport(fixture.image, "GetFileSize", replacementAddress(), patches) == api("GetFileSize"), "Wrong original API");
            require(patches.size() == 2 && *fixture.slot(0) == replacementAddress() && *fixture.slot(1) == replacementAddress(), "Did not patch every named import");
            MEMORY_BASIC_INFORMATION region{};
            VirtualQuery(fixture.image, &region, sizeof(region));
            require(region.Protect == PAGE_READONLY, "Page protection changed");
            require(!hookImport(fixture.image, "NoSuchApi", replacementAddress(), patches), "Missing API was reported present");
            rejects([&] { hookImport(fixture.image, "GetFileSize", replacementAddress(), patches); }, "Double hook accepted");
            restoreImports(patches);
            require(patches.empty() && *fixture.slot(0) == api("GetFileSize") && *fixture.slot(1) == api("GetFileSize"), "Original imports not restored");
            restoreImports(patches);
        }
        std::vector<ImportPatch> patches;
        {
            auto module = GetModuleHandleW(nullptr);
            auto fn = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetModuleHandleW");
            void* original;
            std::memcpy(&original, &fn, sizeof(original));
            require(hookImport(module, "GetModuleHandleW", replacementAddress(), patches) == original,
                    "Real mapped executable imports were not parsed");
            require(!patches.empty(), "Real mapped executable had no named imports");
            restoreImports(patches);
        }
        rejects([&] { hookImport(nullptr, "ReadFile", replacementAddress(), patches); }, "Null image accepted");
        auto malformed = [&](auto change) {
            Fixture fixture;
            change(fixture);
            rejects([&] { hookImport(fixture.image, "GetFileSize", replacementAddress(), patches); }, "Malformed PE accepted");
            require(patches.empty(), "Rejected image was partially hooked");
        };
        malformed([](Fixture& f) { f.at<IMAGE_DOS_HEADER>(0).e_magic = 0; });
        malformed([](Fixture& f) { f.at<IMAGE_DOS_HEADER>(0).e_lfanew = 0x7fffffff; });
        malformed([](Fixture& f) { f.headers().OptionalHeader.Magic = 0; });
        malformed([](Fixture& f) { f.headers().Signature = 0; });
        malformed([](Fixture& f) { f.headers().FileHeader.SizeOfOptionalHeader = 1; });
        malformed([](Fixture& f) { f.headers().FileHeader.SizeOfOptionalHeader = 0xffff; });
        malformed([](Fixture& f) { f.headers().OptionalHeader.SizeOfImage = 0x100; });
        malformed([](Fixture& f) { f.headers().OptionalHeader.NumberOfRvaAndSizes = 100; });
        malformed([](Fixture& f) { f.headers().OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = {0x3ff0, 100}; });
        malformed([](Fixture& f) { f.headers().OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = sizeof(IMAGE_IMPORT_DESCRIPTOR); });
        malformed([](Fixture& f) { f.descriptor(0).Name = 0x3fff; f.image[0x3fff] = 'x'; });
        malformed([](Fixture& f) { f.descriptor(0).OriginalFirstThunk = 0; });
        malformed([](Fixture& f) { f.descriptor(0).FirstThunk = 0x3fff; });
        malformed([](Fixture& f) {
            f.descriptor(0).OriginalFirstThunk = 0x4000 - sizeof(IMAGE_THUNK_DATA);
            f.at<IMAGE_THUNK_DATA>(f.descriptor(0).OriginalFirstThunk).u1.AddressOfData = 0x400;
        });
        malformed([](Fixture& f) { f.at<IMAGE_THUNK_DATA>(f.descriptor(0).OriginalFirstThunk).u1.AddressOfData = 0x3fff; });
        malformed([](Fixture& f) {
            f.at<IMAGE_THUNK_DATA>(f.descriptor(0).FirstThunk + 2 * sizeof(IMAGE_THUNK_DATA)).u1.Function = 1;
        });
        malformed([](Fixture& f) {
            *f.slot(1) = api("CloseHandle");
        });
        malformed([](Fixture& f) {
            f.descriptor(1).FirstThunk = f.descriptor(0).FirstThunk;
        });
        malformed([](Fixture& f) {
            DWORD old;
            VirtualProtect(f.image + 0x1000, 0x1000, PAGE_NOACCESS, &old);
            f.descriptor(0).Name = 0x1000;
        });
        {
            Fixture fixture;
            fixture.headers().OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = {};
            require(!hookImport(fixture.image, "ReadFile", replacementAddress(), patches), "Empty import directory rejected");
        }
        {
            Fixture fixture;
            fixture.at<IMAGE_THUNK_DATA>(fixture.descriptor(0).OriginalFirstThunk).u1.Ordinal = IMAGE_ORDINAL_FLAG | 1;
            require(hookImport(fixture.image, "GetFileSize", replacementAddress(), patches) == api("GetFileSize") && patches.size() == 1,
                    "Ordinal import was interpreted as a name");
            restoreImports(patches);
        }
        {
            Fixture fixture;
            HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 0x4000, nullptr);
            require(mapping != nullptr, "Cannot create read-only patch fixture");
            void* writer = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0x4000);
            require(writer != nullptr, "Cannot initialize read-only patch fixture");
            std::memcpy(writer, fixture.image, 0x4000);
            UnmapViewOfFile(writer);
            void* reader = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0x4000);
            require(reader != nullptr, "Cannot map read-only patch fixture");
            rejects([&] { hookImport(reader, "GetFileSize", replacementAddress(), patches); }, "Read-only mapping was patched");
            require(patches.empty(), "Failed write left a patch record");
            require(*reinterpret_cast<void**>(static_cast<uint8_t*>(reader) + 0x700) == api("GetFileSize"), "Failed write changed original import");
            UnmapViewOfFile(reader);
            CloseHandle(mapping);
        }
        std::puts("Generic named PE import hooks and validation passed");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
