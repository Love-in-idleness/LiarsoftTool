#pragma once

#include <windows.h>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>

struct ImportFixture {
    uint8_t* image;
    size_t next;
    unsigned count = 0;
    std::map<std::string, void**> slots;
    explicit ImportFixture(size_t start = 0x3000) : next(start) {
        image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x20000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!image) throw std::runtime_error("Cannot allocate import fixture");
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = 0x80;
        auto* pe = reinterpret_cast<IMAGE_NT_HEADERS*>(image + 0x80);
        pe->Signature = IMAGE_NT_SIGNATURE;
        pe->FileHeader.Machine = sizeof(void*) == 4 ? IMAGE_FILE_MACHINE_I386 : IMAGE_FILE_MACHINE_AMD64;
        pe->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER);
        pe->OptionalHeader.Magic = sizeof(void*) == 4 ? IMAGE_NT_OPTIONAL_HDR32_MAGIC : IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        pe->OptionalHeader.SizeOfImage = 0x20000;
        pe->OptionalHeader.SizeOfHeaders = 0x400;
        pe->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
        pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT] = {0x1000, 0x1000};
    }
    ~ImportFixture() { VirtualFree(image, 0, MEM_RELEASE); }
    void add(const char* library, const char* name) {
        if (count >= 60 || next + 512 > 0x20000) throw std::runtime_error("Import fixture capacity exceeded");
        auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image + 0x1000) + count++;
        descriptor->Name = static_cast<DWORD>(next);
        std::strcpy(reinterpret_cast<char*>(image + next), library);
        next += 64;
        const size_t named = next;
        std::strcpy(reinterpret_cast<char*>(image + next + 2), name);
        next += 64;
        descriptor->OriginalFirstThunk = static_cast<DWORD>(next);
        reinterpret_cast<IMAGE_THUNK_DATA*>(image + next)->u1.AddressOfData = named;
        next += sizeof(IMAGE_THUNK_DATA) * 2;
        descriptor->FirstThunk = static_cast<DWORD>(next);
        auto address = GetProcAddress(GetModuleHandleA(library), name);
        if (!address) throw std::runtime_error("Fixture API is unavailable");
        std::memcpy(image + next, &address, sizeof(address));
        slots[name] = reinterpret_cast<void**>(image + next);
        next += sizeof(IMAGE_THUNK_DATA) * 2;
    }
    template<class F> F function(const char* name) const {
        F result;
        std::memcpy(&result, slots.at(name), sizeof(result));
        return result;
    }
    ImportFixture(const ImportFixture&) = delete;
    ImportFixture& operator=(const ImportFixture&) = delete;
};
