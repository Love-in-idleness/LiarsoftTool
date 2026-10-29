#include <algorithm>
#pragma once
#include "../runtime/tests/pe_fixture.h"
#include "encoding.h"
#include <array>

inline std::vector<uint8_t> classifierFixture(bool optimized = false, bool extended = false,
                                              bool bx = false) {
    std::vector<uint8_t> code =
        bx ? std::vector<uint8_t>{0x53, 0x56, 0x8b, 0x74, 0x24, 0x0c, 0x66, 0xbb, 0x21, 0x21}
           : std::vector<uint8_t>{0x56, 0x8b, 0x74, 0x24, 0x08};
    const uint16_t values[] = {
        0x20,   0x27,   0x22,   0x2c,   0x2e,   0x3f,   0x21,   0x29,   0x7d,   0x5d,   0x8140, 0x8141,
        0x8142, 0x8143, 0x8144, 0x8148, 0x8149, 0x815b, 0x8160, 0x8163, 0x816a, 0x816c, 0x816e, 0x8170,
        0x8172, 0x8174, 0x8176, 0x8178, 0x817a, 0x8166, 0x8168, 0x82c1, 0x829f, 0x82a1, 0x82a3, 0x82a5,
        0x82a7, 0x82e1, 0x82e3, 0x82e5, 0x8340, 0x8342, 0x8344, 0x8346, 0x8348, 0x8383, 0x8385, 0x8387,
        0x8165, 0x8167, 0x8169, 0x816b, 0x816d, 0x816f, 0x8171, 0x8173, 0x8175, 0x8177, 0x8179, 0x814a};
    auto put = [&](uint32_t v) {
        for (unsigned n = 0; n < 4; ++n)
            code.push_back(uint8_t(v >> (n * 8)));
    };
    std::vector<std::pair<size_t, bool>> branches;
    auto compare = [&](uint16_t value, bool first, bool useBx = false) {
        if (useBx)
            code.insert(code.end(), {0x66, 0x3b, 0xf3});
        else if (optimized) {
            code.push_back(0xb8);
            put(value);
            code.insert(code.end(), {0x66, 0x3b, 0xf0});
        } else
            code.insert(code.end(), {0x66, 0x81, 0xfe, uint8_t(value), uint8_t(value >> 8)});
        code.insert(code.end(), {0x0f, 0x84});
        branches.push_back({code.size(), first});
        put(0);
    };
    for (size_t n = 0; n < std::size(values); ++n) {
        if (n == 48 && extended) {
            compare(0x213f, true);
            compare(0x3f21, true);
            compare(0x2121, true, bx);
        }
        compare(values[n], n < 48);
    }
    auto result = [&](uint32_t value) {
        auto pos = code.size();
        code.push_back(0xb8);
        put(value);
        code.push_back(0x5e);
        if (bx)
            code.push_back(0x5b);
        code.insert(code.end(), {0xc2, 4, 0});
        return pos;
    };
    result(0);
    const auto end = result(2), start = result(1);
    for (auto [pos, first] : branches) {
        const auto distance = uint32_t((first ? start : end) - pos - 4);
        for (unsigned n = 0; n < 4; ++n)
            code[pos + n] = uint8_t(distance >> (n * 8));
    }
    return code;
}
inline PeFixture editorFixture(bool optimized = false, bool extended = false, bool bx = false) {
    PeFixture p;
    p.bytes.resize(0x3500);
    p.put(0x86, 2, 2);
    p.put(0x98 + 16, 0x1000);
    p.put(0x98 + 28, 0x400000);
    p.put(0x98 + 32, 0x1000);
    p.put(0x98 + 36, 0x200);
    std::memcpy(p.bytes.data() + 0x178, ".text", 6);
    p.put(0x178 + 8, 0x2000);
    p.put(0x178 + 16, 0x2000);
    p.put(0x178 + 36, 0x60000020);
    const auto second = 0x178 + 40;
    std::memcpy(p.bytes.data() + second, ".rdata", 7);
    p.put(second + 8, 0x1000);
    p.put(second + 12, 0x3000);
    p.put(second + 16, 0x1000);
    p.put(second + 20, 0x2400);
    p.put(second + 36, 0x40000040);
    p.put(0x98 + 104, 0x3000);
    p.put(0x98 + 108, 60);
    for (unsigned n = 0; n < 2; ++n) {
        const auto d = p.file(0x3000 + n * 20);
        const auto lookup = 0x3100 + n * 0x40, iat = lookup + 0x20, name = 0x3200 + n * 0x30,
                   dll = 0x3180 + n * 0x10;
        p.put(d, lookup);
        p.put(d + 12, dll);
        p.put(d + 16, iat);
        p.put(p.file(lookup), name);
        p.put(p.file(iat), name);
        p.text(name + 2, n ? "MessageBoxA" : "CreateFontA");
        p.text(dll, n ? "USER32.dll" : "GDI32.dll");
    }
    p.text(0x3300, liarsoft::convertEncoding(u8"保存 %s", "UTF-8", "CP932", true));
    p.text(0x3340, liarsoft::convertEncoding(u8"確認", "UTF-8", "CP932", true));
    size_t at = p.file(0x1000);
    auto byte = [&](uint8_t b) { p.bytes[at++] = b; };
    auto push = [&](uint32_t v) {
        byte(0x68);
        p.put(at, v);
        at += 4;
    };
    for (unsigned n = 0; n < 2; ++n) {
        push(0x403300);
        push(0x403340);
        byte(0xe8);
        const auto rva = uint32_t(at - 0x400 + 0x1000);
        p.put(at, 0x1200 - rva - 4);
        at += 4;
    }
    for (unsigned n = 0; n < 14; ++n)
        push(n == 5 ? 0x80 : 0);
    byte(0xff);
    byte(0x15);
    p.put(at, 0x403120);
    at += 4;
    byte(0xc3);
    const std::vector<uint8_t> wrapper = {0x8b, 0x81, 0x98, 2,    0,    0,    0x85, 0xc0, 0x74, 0x13, 0x8b,
                                          0x54, 0x24, 8,    0x8b, 0x49, 4,    0x52, 0x8b, 0x54, 0x24, 8,
                                          0x52, 0x51, 0xff, 0xd0, 0xc2, 8,    0,    0x8b, 0x54, 0x24, 4,
                                          0x8b, 0x44, 0x24, 8,    0x8b, 0x49, 4,    0x6a, 0x24, 0x52, 0x50,
                                          0x51, 0xff, 0x15, 0x60, 0x31, 0x40, 0,    0xc2, 8,    0};
    std::copy(wrapper.begin(), wrapper.end(), p.bytes.begin() + p.file(0x1200));
    const auto code = classifierFixture(optimized, extended, bx);
    std::copy(code.begin(), code.end(), p.bytes.begin() + p.file(0x1400));
    std::fill(p.bytes.begin() + 0x3400, p.bytes.end(), 0x7b);
    return p;
}
inline PeFixture expandedEditorFixture(bool fontGetter = false) {
    auto p = editorFixture();
    p.put(p.file(0x33c0), 0x401c00);
    p.text(0x3500, "APINI");
    p.text(0x3506, liarsoft::convertEncoding(u8"原作タイトル", "UTF-8", "CP932", true));
    for (unsigned n = 0; n < 2; ++n) {
        p.put(p.file(0x3800 + n * 58), 7000 + n, 2);
        p.text(0x3806 + n * 58,
               liarsoft::convertEncoding(n ? u8"第二場面" : u8"第一場面", "UTF-8", "CP932", true));
    }
    p.text(0x33a0, liarsoft::convertEncoding(u8"サンプル文章", "UTF-8", "CP932", true));
    p.put(p.file(0x3144), 0x3260);
    p.put(p.file(0x3164), 0x3260);
    p.text(0x3262, "SetWindowTextA");
    p.text(0x33e0, "Original ASCII window title");
    const std::vector<uint8_t> code{0x68, 0,    0x35, 0x40, 0,    0x58, 0x8d, 0x3c, 0x55, 0,
                                    0x38, 0x40, 0,    0x8d, 0x04, 0x55, 6,    0x38, 0x40, 0,
                                    0x68, 0xa0, 0x33, 0x40, 0,    0x58, 0x68, 0xe0, 0x33, 0x40,
                                    0,    0x6a, 0,    0xff, 0x15, 0x64, 0x31, 0x40, 0,    0xc3};
    std::copy(code.begin(), code.end(), p.bytes.begin() + p.file(0x1c00));
    if (fontGetter) {
        const auto first = p.file(0x1000);
        const auto at = first + 30 + 14 * 5;
        std::move_backward(p.bytes.begin() + at, p.bytes.begin() + at + 7, p.bytes.begin() + at + 12);
        p.bytes[at] = 0xe8;
        p.put(at + 1, 0x1d00 - (0x1000 + uint32_t(at - first)) - 5);
        p.bytes[p.file(0x1d00)] = 0xc3;
    }
    return p;
}
