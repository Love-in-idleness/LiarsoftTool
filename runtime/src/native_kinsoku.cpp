#include "native_kinsoku.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace liarsoft::runtime {
namespace {
constexpr uint16_t ascii[] = {0x20,0x27,0x22,0x2c,0x2e,0x3f,0x21,0x29,0x7d,0x5d};
constexpr uint16_t start[] = {
    0x8140,0x8141,0x8142,0x8143,0x8144,0x8148,0x8149,0x815b,
    0x8160,0x8163,0x816a,0x816c,0x816e,0x8170,0x8172,0x8174,
    0x8176,0x8178,0x817a,0x8166,0x8168,0x82c1,0x829f,0x82a1,
    0x82a3,0x82a5,0x82a7,0x82e1,0x82e3,0x82e5,0x8340,0x8342,
    0x8344,0x8346,0x8348,0x8383,0x8385,0x8387};
constexpr uint16_t finish[] = {
    0x8165,0x8167,0x8169,0x816b,0x816d,0x816f,
    0x8171,0x8173,0x8175,0x8177,0x8179,0x814a};

uint16_t word(const uint8_t* bytes) { return uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8); }
int32_t integer(const uint8_t* bytes) { int32_t value; std::memcpy(&value, bytes, 4); return value; }

bool compare(const uint8_t* bytes, size_t length, size_t& pos, uint16_t value,
             size_t& target, size_t* prefix = nullptr) {
    const auto begin = pos;
    if (pos > length || length - pos < 6) return false;
    if (bytes[pos] == 0x66 && bytes[pos + 1] == 0x83 && bytes[pos + 2] == 0xfe &&
        uint16_t(int16_t(int8_t(bytes[pos + 3]))) == value) pos += 4;
    else if (length - pos >= 7 && bytes[pos] == 0x66 && bytes[pos + 1] == 0x81 &&
             bytes[pos + 2] == 0xfe && word(bytes + pos + 3) == value) pos += 5;
    else if (length - pos >= 10 && bytes[pos] == 0xb8 && word(bytes + pos + 1) == value &&
             !word(bytes + pos + 3) && bytes[pos + 5] == 0x66 &&
             bytes[pos + 6] == 0x3b && bytes[pos + 7] == 0xf0) pos += 8;
    else return false;
    if (prefix) *prefix = pos - begin;
    int64_t destination;
    if (length - pos >= 6 && bytes[pos] == 0x0f && bytes[pos + 1] == 0x84) {
        destination = int64_t(pos) + 6 + integer(bytes + pos + 2); pos += 6;
    } else if (length - pos >= 2 && bytes[pos] == 0x74) {
        destination = int64_t(pos) + 2 + int8_t(bytes[pos + 1]); pos += 2;
    } else return false;
    if (destination < 0 || uint64_t(destination) >= length) return false;
    target = size_t(destination);
    return true;
}
}

std::vector<KinsokuBlock> findKinsokuBlocks(const uint8_t* bytes, size_t length) {
    if (!bytes && length) throw std::runtime_error("Invalid classifier range");
    std::vector<KinsokuBlock> result;
    for (size_t offset = 0; offset < length; ++offset) {
        size_t pos = offset, noStart = 0, noEnd = 0, target = 0, prefix = 0;
        if (!compare(bytes, length, pos, ascii[0], noStart, &prefix)) continue;
        const auto firstLength = pos - offset;
        bool match = true;
        for (size_t i = 1; i < std::size(ascii) && match; ++i)
            match = compare(bytes, length, pos, ascii[i], target) && target == noStart;
        for (auto value : start) {
            if (!match) break;
            match = compare(bytes, length, pos, value, target) && target == noStart;
        }
        if (!match) continue;
        size_t extra = pos;
        if (compare(bytes, length, extra, 0x213f, target) && target == noStart) {
            if (!compare(bytes, length, extra, 0x3f21, target) || target != noStart ||
                !compare(bytes, length, extra, 0x2121, target) || target != noStart) continue;
            pos = extra;
        }
        for (size_t i = 0; i < std::size(finish) && match; ++i) {
            match = compare(bytes, length, pos, finish[i], target);
            if (!i) noEnd = target;
            else match = match && target == noEnd;
        }
        if (!match || noStart == noEnd || noStart < pos || noEnd < pos ||
            noStart - pos > 256 || noEnd - pos > 256) continue;
        result.push_back({offset, prefix, firstLength, pos, noStart, noEnd});
        offset = pos - 1;
    }
    return result;
}

#if defined(_WIN32) && !defined(_WIN64)
namespace {
struct Patch {
    uint8_t* address;
    std::array<uint8_t, 5> original;
    std::array<uint8_t, 5> replacement;
    uint8_t* thunk;
};
std::vector<Patch> patches;
GlyphTokens* activeTokens = nullptr;
KinsokuRules activeRules;

uint32_t __cdecl classify(uint32_t character) noexcept {
    try {
        const auto code = uint16_t(character);
        if (const auto cp = activeTokens->glyph(code)) return activeRules.classify(*cp);
        std::string bytes;
        if ((code & 0xff00) && (code & 0xff00) != 0xff00) bytes += char(code >> 8);
        bytes += char(code);
        const auto decoded = activeTokens->decode(bytes);
        if (decoded.size() == 1) return activeRules.classify(decoded.front());
    } catch (...) {}
    return 3; // Preserve native classification of packed ASCII pairs or invalid legacy bytes.
}

void write(uint8_t* address, const uint8_t* bytes) {
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(address, &region, sizeof(region)) || region.RegionSize < 5 ||
        reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(region.BaseAddress) > region.RegionSize - 5)
        throw std::runtime_error("Unsupported classifier memory boundary");
    DWORD protection, ignored;
    if (!VirtualProtect(address, 5, PAGE_READWRITE, &protection))
        throw std::runtime_error("Cannot update the in-memory line-break classifier");
    std::memcpy(address, bytes, 5);
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), address, 5) != FALSE;
    if (!VirtualProtect(address, 5, protection, &ignored) || !flushed)
        throw std::runtime_error("Cannot restore classifier memory protection or instruction cache");
}

Patch makePatch(uint8_t* data, const KinsokuBlock& block) {
    auto* thunk = static_cast<uint8_t*>(VirtualAlloc(nullptr, 256, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!thunk) throw std::runtime_error("Cannot allocate the line-break classifier bridge");
    try {
        // The replaced block was not a call site: preserve floating-point/SIMD state too.
        std::vector<uint8_t> code{0x9c,0x60,0x8b,0xdc,0x83,0xe4,0xf0,
            0x81,0xec,0x10,0x02,0,0,0x0f,0xae,0x44,0x24,0x10,0x89,0x34,0x24,0xb8};
        auto append = [&](uintptr_t value) {
            for (unsigned i = 0; i < 4; ++i) code.push_back(uint8_t(value >> (i * 8)));
        };
        auto relative = [&](const void* target) {
            append(reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(thunk + code.size() + 4));
        };
        auto jump = [&](const void* target) { code.push_back(0xe9); relative(target); };
        append(reinterpret_cast<uintptr_t>(&classify));
        code.insert(code.end(), {0xff,0xd0,0x0f,0xae,0x4c,0x24,0x10,0x8b,0xe3});
        std::array<size_t, 3> branches;
        for (unsigned value = 1; value <= 3; ++value) {
            code.insert(code.end(), {0x83,0xf8,uint8_t(value),0x0f,0x84});
            branches[value - 1] = code.size(); append(0);
        }
        code.insert(code.end(), {0x61,0x9d}); jump(data + block.normal);
        for (unsigned i = 0; i < 3; ++i) {
            const uint32_t distance = uint32_t(code.size() - branches[i] - 4);
            std::memcpy(code.data() + branches[i], &distance, 4);
            code.insert(code.end(), {0x61,0x9d});
            if (i < 2) jump(data + (i == 0 ? block.noStart : block.noEnd));
            else {
                code.insert(code.end(), data + block.offset, data + block.offset + block.firstPrefix);
                code.insert(code.end(), {0x0f,0x84}); relative(data + block.noStart);
                jump(data + block.offset + block.firstLength);
            }
        }
        if (code.size() > 256) throw std::runtime_error("Classifier bridge exceeds its allocation");
        std::memcpy(thunk, code.data(), code.size());
        DWORD previous;
        if (!VirtualProtect(thunk, 256, PAGE_EXECUTE_READ, &previous) ||
            !FlushInstructionCache(GetCurrentProcess(), thunk, code.size()))
            throw std::runtime_error("Cannot make the classifier bridge executable");
        Patch patch{data + block.offset, {}, {0xe9,0,0,0,0}, thunk};
        std::memcpy(patch.original.data(), patch.address, 5);
        const uint32_t distance = uint32_t(reinterpret_cast<uintptr_t>(thunk) -
            reinterpret_cast<uintptr_t>(patch.address + 5));
        std::memcpy(patch.replacement.data() + 1, &distance, 4);
        return patch;
    } catch (...) { VirtualFree(thunk, 0, MEM_RELEASE); throw; }
}
}

size_t installNativeKinsoku(void* module, GlyphTokens& tokens, const KinsokuRules& rules) {
    if (activeTokens) throw std::runtime_error("Line-break hooks are already installed");
    if (!IsProcessorFeaturePresent(PF_XMMI_INSTRUCTIONS_AVAILABLE))
        throw std::runtime_error("The line-break bridge requires FXSAVE/SSE support");
    // Named-import validation has already admitted this loaded PE. Check section bounds too.
    auto* base = static_cast<uint8_t*>(module);
    const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    const auto* pe = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const size_t section = size_t(dos->e_lfanew) + offsetof(IMAGE_NT_HEADERS, OptionalHeader) + pe->FileHeader.SizeOfOptionalHeader;
    if (section > pe->OptionalHeader.SizeOfHeaders || pe->FileHeader.NumberOfSections >
        (pe->OptionalHeader.SizeOfHeaders - section) / sizeof(IMAGE_SECTION_HEADER))
        throw std::runtime_error("Invalid classifier PE section table");
    const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(base + section);
    activeTokens = &tokens;
    activeRules = rules;
    try {
        for (unsigned i = 0; i < pe->FileHeader.NumberOfSections; ++i) {
            const auto& item = sections[i];
            if (!(item.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            const auto size = item.Misc.VirtualSize;
            if (item.VirtualAddress > pe->OptionalHeader.SizeOfImage || size > pe->OptionalHeader.SizeOfImage - item.VirtualAddress)
                throw std::runtime_error("Invalid classifier executable section");
            auto* data = base + item.VirtualAddress;
            size_t cursor = 0;
            while (cursor < size) {
                MEMORY_BASIC_INFORMATION region{};
                auto address = reinterpret_cast<uintptr_t>(data + cursor);
                if (!VirtualQuery(data + cursor, &region, sizeof(region)) || region.AllocationBase != base ||
                    region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
                    !(region.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
                    throw std::runtime_error("Unreadable classifier code section");
                const auto available = region.RegionSize - (address - reinterpret_cast<uintptr_t>(region.BaseAddress));
                if (!available) throw std::runtime_error("Empty classifier code region");
                cursor += std::min<size_t>(size - cursor, available);
            }
            for (const auto& block : findKinsokuBlocks(data, size)) {
                auto patch = makePatch(data, block);
                try { patches.push_back(patch); }
                catch (...) { VirtualFree(patch.thunk, 0, MEM_RELEASE); throw; }
                write(patch.address, patch.replacement.data());
            }
        }
        if (patches.empty()) throw std::runtime_error("No supported shared RScript line-break classifier was found");
        return patches.size();
    } catch (...) { removeNativeKinsoku(); throw; }
}

void removeNativeKinsoku() {
    while (!patches.empty()) {
        const auto& patch = patches.back();
        if (std::memcmp(patch.address, patch.replacement.data(), 5) &&
            std::memcmp(patch.address, patch.original.data(), 5))
            throw std::runtime_error("Another module changed the line-break hook");
        write(patch.address, patch.original.data());
        VirtualFree(patch.thunk, 0, MEM_RELEASE);
        patches.pop_back();
    }
    activeTokens = nullptr;
}
#else
size_t installNativeKinsoku(void*, GlyphTokens&, const KinsokuRules&) {
    throw std::runtime_error("Native line-break integration requires 32-bit Windows");
}
void removeNativeKinsoku() {}
#endif
}
