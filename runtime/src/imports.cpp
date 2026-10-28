#include "imports.h"
#include "pe_image.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace liarsoft::runtime {
namespace {

void readable(const void* address, size_t bytes, const void* allocation) {
    auto cursor = reinterpret_cast<uintptr_t>(address);
    if (bytes > std::numeric_limits<uintptr_t>::max() - cursor)
        throw std::runtime_error("Import image address overflow");
    const auto end = cursor + bytes;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region, sizeof(region)) ||
            region.AllocationBase != allocation || region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(region.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
            throw std::runtime_error("Unreadable import image range");
        auto start = reinterpret_cast<uintptr_t>(region.BaseAddress);
        if (region.RegionSize > std::numeric_limits<uintptr_t>::max() - start ||
            start + region.RegionSize <= cursor)
            throw std::runtime_error("Invalid import image memory region");
        cursor = (start + region.RegionSize < end) ? start + region.RegionSize : end;
    }
}

struct Image {
    uint8_t* base;
    size_t size;
    void* range(size_t rva, size_t bytes) const {
        if (rva > size || bytes > size - rva)
            throw std::runtime_error("Import RVA is outside the image");
        readable(base + rva, bytes, base);
        return base + rva;
    }
    std::string string(size_t rva) const {
        std::string result;
        while (rva < size) {
            MEMORY_BASIC_INFORMATION region{};
            const auto* first = static_cast<const char*>(range(rva, 1));
            if (!VirtualQuery(first, &region, sizeof(region)))
                throw std::runtime_error("Cannot query import name memory");
            size_t count = region.RegionSize -
                (reinterpret_cast<uintptr_t>(first) - reinterpret_cast<uintptr_t>(region.BaseAddress));
            if (count > size - rva) count = size - rva;
            range(rva, count);
            const auto* last = static_cast<const char*>(std::memchr(first, 0, count));
            result.append(first, last ? static_cast<size_t>(last - first) : count);
            if (last) return result;
            rva += count;
        }
        throw std::runtime_error("Unterminated import name");
    }
    template<class T> T value(size_t rva) const {
        T result;
        std::memcpy(&result, range(rva, sizeof(result)), sizeof(result));
        return result;
    }
};

void replace(void** slot, void* value, void* expected = nullptr) {
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
        throw std::runtime_error("Cannot make import slot writable");
    auto previous = expected ? InterlockedCompareExchangePointer(slot, value, expected) :
                               InterlockedExchangePointer(slot, value);
    DWORD ignored = 0;
    if (!VirtualProtect(slot, sizeof(*slot), old, &ignored)) {
        InterlockedExchangePointer(slot, previous);
        VirtualProtect(slot, sizeof(*slot), old, &ignored);
        throw std::runtime_error("Cannot restore import slot protection");
    }
    if (expected && previous != expected)
        throw std::runtime_error("Import changed while installing hook");
}

} // namespace

void restoreImports(std::vector<ImportPatch>& patches) {
    while (!patches.empty()) {
        const auto& patch = patches.back();
        replace(patch.slot, patch.original);
        patches.pop_back();
    }
}

void* hookImport(void* module, const char* name, void* replacement,
                 std::vector<ImportPatch>& patches) {
    if (!module || !name || !*name || !replacement)
        throw std::runtime_error("Invalid import hook arguments");
    readable(module, sizeof(IMAGE_DOS_HEADER), module);
    IMAGE_DOS_HEADER dos;
    std::memcpy(&dos, module, sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos)))
        throw std::runtime_error("Invalid import image DOS header");
    auto* base = static_cast<uint8_t*>(module);
    const size_t nt = static_cast<size_t>(dos.e_lfanew);
    if (nt > std::numeric_limits<uintptr_t>::max() - reinterpret_cast<uintptr_t>(base))
        throw std::runtime_error("Import NT header address overflow");
    readable(base + nt, sizeof(IMAGE_NT_HEADERS), module);
    IMAGE_NT_HEADERS headers;
    std::memcpy(&headers, base + nt, sizeof(headers));
#ifdef _WIN64
    constexpr WORD machine = IMAGE_FILE_MACHINE_AMD64;
#else
    constexpr WORD machine = IMAGE_FILE_MACHINE_I386;
#endif
    if (headers.Signature != IMAGE_NT_SIGNATURE || headers.FileHeader.Machine != machine ||
        headers.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER) ||
        headers.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC ||
        headers.OptionalHeader.SizeOfImage < sizeof(headers) ||
        headers.OptionalHeader.SizeOfHeaders > headers.OptionalHeader.SizeOfImage ||
        nt > headers.OptionalHeader.SizeOfHeaders ||
        sizeof(headers) > headers.OptionalHeader.SizeOfHeaders - nt)
        throw std::runtime_error("Invalid or incompatible import PE header");
    Image image{base, headers.OptionalHeader.SizeOfImage};
    if (image.size > std::numeric_limits<uintptr_t>::max() - reinterpret_cast<uintptr_t>(base) ||
        headers.FileHeader.SizeOfOptionalHeader >
            headers.OptionalHeader.SizeOfHeaders - nt - offsetof(IMAGE_NT_HEADERS, OptionalHeader) ||
        headers.OptionalHeader.NumberOfRvaAndSizes > IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
        throw std::runtime_error("Invalid optional import PE header extent");
    image.range(0, headers.OptionalHeader.SizeOfHeaders);
    if (headers.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_IMPORT)
        return nullptr;
    const auto directory = headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress && !directory.Size) return nullptr;
    if (!directory.VirtualAddress || directory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR))
        throw std::runtime_error("Invalid import directory");
    image.range(directory.VirtualAddress, directory.Size);
    std::vector<ImportPatch> selected;
    void* original = nullptr;
    for (const auto& imported : walkImports(image, directory.VirtualAddress, directory.Size, sizeof(void*) == 8)) {
        if (imported.name != name) continue;
        const auto dll = GetModuleHandleA(imported.dll.c_str());
        const auto expected = dll ? GetProcAddress(dll, name) : nullptr;
        void* expectedAddress = nullptr;
        static_assert(sizeof(expectedAddress) == sizeof(expected));
        std::memcpy(&expectedAddress, &expected, sizeof(expectedAddress));
        if (!expectedAddress || reinterpret_cast<uintptr_t>(expectedAddress) != imported.target)
            throw std::runtime_error("Imported API is unresolved or already hooked: " + imported.name);
        if (original && original != expectedAddress)
            throw std::runtime_error("Conflicting imports for API: " + imported.name);
        if (imported.slot % alignof(void*)) throw std::runtime_error("Unaligned import address slot");
        auto** slot = static_cast<void**>(image.range(imported.slot, sizeof(void*)));
        for (const auto& patch : selected)
            if (patch.slot == slot) throw std::runtime_error("Overlapping import descriptors");
        selected.push_back({slot, expectedAddress});
        original = expectedAddress;
    }
    patches.reserve(patches.size() + selected.size());
    const auto start = patches.size();
    try {
        for (const auto& patch : selected) {
            replace(patch.slot, replacement, patch.original);
            patches.push_back(patch);
        }
    } catch (...) {
        while (patches.size() > start) {
            replace(patches.back().slot, patches.back().original);
            patches.pop_back();
        }
        throw;
    }
    return original;
}

} // namespace liarsoft::runtime
