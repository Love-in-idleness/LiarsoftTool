#include "native_kinsoku.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include "import_fixture.h"
#endif

using namespace liarsoft::runtime;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void put(std::vector<uint8_t>& code, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) code.push_back(uint8_t(value >> (i * 8)));
}
std::vector<uint8_t> fixture(bool optimized, bool extended) {
    std::vector<uint8_t> code{0x56,0x8b,0x74,0x24,0x08};
    const uint16_t values[] = {
        0x20,0x27,0x22,0x2c,0x2e,0x3f,0x21,0x29,0x7d,0x5d,
        0x8140,0x8141,0x8142,0x8143,0x8144,0x8148,0x8149,0x815b,
        0x8160,0x8163,0x816a,0x816c,0x816e,0x8170,0x8172,0x8174,
        0x8176,0x8178,0x817a,0x8166,0x8168,0x82c1,0x829f,0x82a1,
        0x82a3,0x82a5,0x82a7,0x82e1,0x82e3,0x82e5,0x8340,0x8342,
        0x8344,0x8346,0x8348,0x8383,0x8385,0x8387,
        0x8165,0x8167,0x8169,0x816b,0x816d,0x816f,0x8171,0x8173,
        0x8175,0x8177,0x8179,0x814a};
    std::vector<size_t> toStart, toEnd;
    auto compare = [&](uint16_t value, bool start) {
        if (optimized) {
            code.push_back(0xb8); put(code, value); code.insert(code.end(), {0x66,0x3b,0xf0});
        } else if (value < 0x80) code.insert(code.end(), {0x66,0x83,0xfe,uint8_t(value)});
        else code.insert(code.end(), {0x66,0x81,0xfe,uint8_t(value),uint8_t(value >> 8)});
        code.insert(code.end(), {0x0f,0x84});
        (start ? toStart : toEnd).push_back(code.size()); put(code, 0);
    };
    for (size_t i = 0; i < std::size(values); ++i) {
        if (i == 48 && extended) for (auto value : {0x213f,0x3f21,0x2121}) compare(value, true);
        compare(values[i], i < 48);
    }
    auto result = [&](uint32_t value) {
        auto offset = code.size();
        code.push_back(0xb8); put(code, value); code.insert(code.end(), {0x5e,0xc2,0x04,0x00});
        return offset;
    };
    result(0);
    const auto noEnd = result(2), noStart = result(1);
    for (const auto& group : {std::make_pair(toStart, noStart), std::make_pair(toEnd, noEnd)})
        for (auto offset : group.first) {
            const uint32_t distance = uint32_t(group.second - offset - 4);
            std::memcpy(code.data() + offset, &distance, 4);
        }
    return code;
}
#if defined(_WIN32) && !defined(_WIN64) && defined(__GNUC__)
__attribute__((target("sse")))
void checkSimd(uint32_t (WINAPI *function)(uint32_t), uint32_t character) {
    const uint32_t marker = 0x12345678;
    uint32_t restored = 0, category = 0;
    __asm__ volatile("movss %[marker], %%xmm0\n\tpush %[character]\n\tcall *%[function]\n\tmovss %%xmm0, %[restored]"
        : "=a"(category), [restored] "=m"(restored)
        : [marker] "m"(marker), [character] "r"(character), [function] "r"(function)
        : "ecx", "edx", "xmm0", "cc", "memory");
    require(category == 1 && restored == marker, "Classifier bridge corrupted SIMD state");
}
#endif
}

int main() {
    try {
        for (bool optimized : {false, true}) for (bool extended : {false, true}) {
            auto code = fixture(optimized, extended);
            const auto found = findKinsokuBlocks(code.data(), code.size());
            require(found.size() == 1 && found[0].offset == 5, "Complete shared classifier not recognized");
            for (size_t size = 0; size <= found[0].normal; ++size)
                require(findKinsokuBlocks(code.data(), size).empty(), "Truncated classifier accepted");
            auto changed = code;
            changed.at(5 + (optimized ? 1 : 3)) = 0x21;
            require(findKinsokuBlocks(changed.data(), changed.size()).empty(), "Changed classifier accepted");
            changed = code;
            changed.at(5 + found[0].firstPrefix + 2) ^= 1;
            require(findKinsokuBlocks(changed.data(), changed.size()).empty(), "Conflicting branch targets accepted");
#if defined(_WIN32) && !defined(_WIN64)
            ImportFixture image;
            auto* pe = reinterpret_cast<IMAGE_NT_HEADERS*>(image.image + 0x80);
            pe->FileHeader.NumberOfSections = 1;
            auto* section = IMAGE_FIRST_SECTION(pe);
            section->VirtualAddress = 0x8000;
            section->Misc.VirtualSize = 0x2000;
            section->Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
            auto* executable = image.image + section->VirtualAddress;
            std::memcpy(executable, code.data(), code.size());
            DWORD protection;
            require(VirtualProtect(executable, section->Misc.VirtualSize, PAGE_EXECUTE_READ, &protection), "Cannot protect fixture");
            using Classifier = uint32_t (WINAPI *)(uint32_t);
            auto classify = reinterpret_cast<Classifier>(executable);
            require(classify(0x8163) == 1 && classify(0x8169) == 2, "Fixture native behavior is wrong");
            GlyphTokens tokens;
            const auto close = tokens.encode(u8"⟩"), open = tokens.encode(u8"⟨");
            const auto token = [](const std::string& bytes) { return uint32_t(uint8_t(bytes[0]) << 8) | uint8_t(bytes[1]); };
            KinsokuRules rules{u"）⟩", u"（⟨"};
            require(installNativeKinsoku(image.image, tokens, rules) == 1, "Cannot install shared classifier");
            require(classify(token(close)) == 1 && classify(token(open)) == 2, "UTF-8 token classification is wrong");
            require(classify(0x8163) == 0 && classify(0x816a) == 1 && classify(0x8169) == 2,
                    "JSON rules did not replace native classification");
            require(classify('A') == 0 && classify(0x213f) == (extended ? 1u : 0u), "Native fallback changed");
#ifdef __GNUC__
            checkSimd(classify, token(close));
#endif
            removeNativeKinsoku();
            require(!std::memcmp(executable, code.data(), code.size()) && classify(0x8163) == 1,
                    "Original in-memory classifier was not restored");
#endif
        }
        std::cout << "Shared classifier recognition, Unicode rules and restoration passed\n";
        return 0;
    } catch (const std::exception& error) {
        removeNativeKinsoku();
        std::cerr << error.what() << '\n'; return 1;
    }
}
