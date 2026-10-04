#include "exe_patch.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition) {
    if (!condition) throw std::runtime_error("EXE patch regression failed");
}

const std::vector<uint8_t> kPattern1Prefix = {
    0x6A,0x00,0x6A,0x00,0x6A,0x00,0x6A,0x00,0x68
};

using KinsokuTable = std::array<uint16_t, 50>;

const KinsokuTable kCp932Kinsoku = {
    0x8140,0x8141,0x8142,0x8143,0x8144,0x8148,0x8149,0x815B,
    0x8160,0x8163,0x816A,0x816C,0x816E,0x8170,0x8172,0x8174,
    0x8176,0x8178,0x817A,0x8166,0x8168,0x82C1,0x829F,0x82A1,
    0x82A3,0x82A5,0x82A7,0x82E1,0x82E3,0x82E5,0x8340,0x8342,
    0x8344,0x8346,0x8348,0x8383,0x8385,0x8387,
    0x8165,0x8167,0x8169,0x816B,0x816D,0x816F,0x8171,0x8173,
    0x8175,0x8177,0x8179,0x814A
};

const KinsokuTable kGbkKinsoku = {
    0xA1A1,0xA1A2,0xA1A3,0xA3AC,0xA3AE,0xA3BF,0xA3A1,0xA1AB,
    0xA1AD,0xA3A9,0xA1B3,0xA3DD,0xA3FD,0xA1B5,0xA1B7,0xA1B9,
    0xA1BB,0xA1BF,0xA1AF,0xA1B1,0xA3BA,0xA3BB,0xA3A5,0xA1A4,
    0xA1A8,0xA1A9,0xA1BD,0xA1E6,0xA1E3,0xA1E4,0xA1E5,0xA1EB,
    0x003A,0x003B,0x0025,0xFFFF,0xFFFF,0xFFFF,
    0xA1AE,0xA1B0,0xA3A8,0xA1B2,0xA3DB,0xA3FB,0xA1B4,0xA1B6,
    0xA1B8,0xA1BA,0xA1BE,0xA1BC
};

const KinsokuTable kCp1251Kinsoku = {
    0xFFBB,0xFF93,0xFF94,0xFF92,0xFF85,0x003A,0x003B,0x0025,
    0xFF89,0xFFB0,0xFF96,0xFF97,0x002D,0xFFA0,0xFF99,0xFFAE,
    0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,
    0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,
    0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,
    0xFFAB,0xFF84,0xFF91,0x0028,0x005B,0x007B,0xFFB9,0xFFA7,
    0x0024,0xFF88,0x8000,0x8000
};

void appendCmp(std::vector<uint8_t>& data, uint16_t value, bool nearJump) {
    const uint8_t prefix[] = {
        0x66, 0x81, 0xFE, static_cast<uint8_t>(value),
        static_cast<uint8_t>(value >> 8)
    };
    data.insert(data.end(), std::begin(prefix), std::end(prefix));
    if (nearJump) {
        const uint8_t jump[] = {0x0F,0x84,0x00,0x00,0x00,0x00};
        data.insert(data.end(), std::begin(jump), std::end(jump));
    } else {
        const uint8_t jump[] = {0x74,0x00};
        data.insert(data.end(), std::begin(jump), std::end(jump));
    }
}

void appendKinsoku(std::vector<uint8_t>& data, const KinsokuTable& table,
                    bool extended) {
    for (size_t i = 0; i < 38; ++i) appendCmp(data, table[i], true);
    if (extended) {
        appendCmp(data, 0x213F, true);
        appendCmp(data, 0x3F21, true);
        appendCmp(data, 0x2121, true);
    }
    for (size_t i = 38; i < table.size(); ++i)
        appendCmp(data, table[i], false);
}

void appendRegisterCmp(std::vector<uint8_t>& data, uint16_t value,
                       bool nearJump) {
    const uint8_t load[] = {0xB8, static_cast<uint8_t>(value),
        static_cast<uint8_t>(value >> 8), 0x00, 0x00, 0x66, 0x3B, 0xF0};
    data.insert(data.end(), std::begin(load), std::end(load));
    if (nearJump) {
        const uint8_t jump[] = {0x0F,0x84,0x11,0x22,0x33,0x44};
        data.insert(data.end(), std::begin(jump), std::end(jump));
    } else {
        const uint8_t jump[] = {0x74,0x55};
        data.insert(data.end(), std::begin(jump), std::end(jump));
    }
}

void appendRegisterKinsoku(std::vector<uint8_t>& data,
                           const KinsokuTable& table, bool extended) {
    for (size_t i = 0; i < 38; ++i)
        appendRegisterCmp(data, table[i], true);
    if (extended) {
        appendRegisterCmp(data, 0x213F, true);
        appendRegisterCmp(data, 0x3F21, true);
        // The engine has already loaded BX with 0x2121. Keep this comparison.
        const uint8_t extra[] = {0x66,0x3B,0xF3,0x0F,0x84,0x11,0x22,0x33,0x44};
        data.insert(data.end(), std::begin(extra), std::end(extra));
    }
    for (size_t i = 38; i < table.size(); ++i)
        appendRegisterCmp(data, table[i], i < (extended ? 40u : 39u));
}

void appendPattern1(std::vector<uint8_t>& data, uint8_t charset) {
    data.insert(data.end(), kPattern1Prefix.begin(), kPattern1Prefix.end());
    const uint8_t tail[] = {charset,0x00,0x00,0x00,0x6A,0x00,0x6A,0x00};
    data.insert(data.end(), std::begin(tail), std::end(tail));
}

void appendPattern2(std::vector<uint8_t>& data, uint8_t charset) {
    const uint8_t pattern[] = {0xDA, 0x68, charset};
    data.insert(data.end(), std::begin(pattern), std::end(pattern));
}

std::vector<uint8_t> interleavedFont(uint8_t charset) {
    return {
        0x6A,0x00,0x6A,0x00,0x8B,0x35,0x24,0x80,0x4C,0x00,
        0xF7,0xD8,0x6A,0x00,0x6A,0x00,0x68,charset,0x00,0x00,0x00,
        0x6A,0x00,0x6A,0x00,0x53,0x1B,0xC0,0x25,0xBC,0x02,0x00,0x00,
        0x50,0x6A,0x00,0x6A,0x00,0x6A,0x00,0x57,0x89,0x45,0xB4,0xFF,0xD6
    };
}

std::vector<uint8_t> makeRegisterFixture(const KinsokuTable& table,
                                        uint8_t charset) {
    std::vector<uint8_t> data;
    appendPattern2(data, charset);
    appendRegisterKinsoku(data, table, false);
    data.push_back(0x90);
    appendRegisterKinsoku(data, table, true);
    return data;
}

std::vector<uint8_t> makeKinsokuFixture(const KinsokuTable& table,
                                        uint8_t charset) {
    std::vector<uint8_t> data = {0x4D,0x5A,0x90};
    appendPattern2(data, charset);
    data.push_back(0x90);
    appendKinsoku(data, table, false);
    data.push_back(0x90);
    appendKinsoku(data, table, true);
    return data;
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
}

} // namespace

int main() {
    std::vector<uint8_t> mixed = {0x4D, 0x5A, 0x00};
    appendPattern1(mixed, 0x80);
    mixed.push_back(0x90);
    appendPattern2(mixed, 0x86);
    mixed.push_back(0x90);
    appendPattern1(mixed, 0xCC);

    auto normalized = mixed;
    for (uint8_t source : {0x80, 0x86, 0xCC})
        normalized = liarsoft::exeConvertEncoding(normalized, source, 0xCC);
    auto expected = std::vector<uint8_t>{0x4D, 0x5A, 0x00};
    appendPattern1(expected, 0xCC);
    expected.push_back(0x90);
    appendPattern2(expected, 0xCC);
    expected.push_back(0x90);
    appendPattern1(expected, 0xCC);
    require(normalized == expected);

    const auto optimizedFont = interleavedFont(0x80);
    require(liarsoft::exeConvertEncoding(optimizedFont, 0x80, 0x86) ==
            interleavedFont(0x86));
    for (size_t size = 0; size < optimizedFont.size(); ++size) {
        const std::vector<uint8_t> truncated(optimizedFont.begin(),
                                              optimizedFont.begin() + size);
        require(liarsoft::exeConvertEncoding(truncated, 0x80, 0x86) == truncated);
    }
    auto notFont = optimizedFont;
    notFont.back() = 0xD7; // Different call register: not the observed font path.
    require(liarsoft::exeConvertEncoding(notFont, 0x80, 0x86) == notFont);

    const auto cp932 = makeKinsokuFixture(kCp932Kinsoku, 0x80);
    const auto gbk = makeKinsokuFixture(kGbkKinsoku, 0x86);
    const auto cp1251 = makeKinsokuFixture(kCp1251Kinsoku, 0xCC);
    require(liarsoft::exeConvertEncoding(cp932, 0x80, 0x86) == gbk);
    require(liarsoft::exeConvertEncoding(gbk, 0x86, 0x80) == cp932);
    require(liarsoft::exeConvertEncoding(cp932, 0x80, 0xCC) == cp1251);
    require(liarsoft::exeConvertEncoding(cp1251, 0xCC, 0x80) == cp932);

    const auto register932 = makeRegisterFixture(kCp932Kinsoku, 0x80);
    const auto registerGbk = makeRegisterFixture(kGbkKinsoku, 0x86);
    const auto register1251 = makeRegisterFixture(kCp1251Kinsoku, 0xCC);
    require(liarsoft::exeConvertEncoding(register932, 0x80, 0x86) == registerGbk);
    require(liarsoft::exeConvertEncoding(registerGbk, 0x86, 0xCC) == register1251);
    require(liarsoft::exeConvertEncoding(register1251, 0xCC, 0x80) == register932);

    for (bool extended : {false, true}) {
        std::vector<uint8_t> table;
        appendRegisterKinsoku(table, kCp932Kinsoku, extended);
        // Incomplete or altered tables must not be partially patched.
        for (size_t size = 0; size < table.size(); ++size) {
            const std::vector<uint8_t> truncated(table.begin(), table.begin() + size);
            require(liarsoft::exeConvertEncoding(truncated, 0x80, 0x86) == truncated);
        }
        auto altered = table;
        altered[1] ^= 1;
        require(liarsoft::exeConvertEncoding(altered, 0x80, 0x86) == altered);
        altered = table;
        altered[3] = 1; // MOV EAX must contain a zero-extended 16-bit constant.
        require(liarsoft::exeConvertEncoding(altered, 0x80, 0x86) == altered);
        altered = table;
        altered[9] = 0x85; // JNE instead of JE is not this known classifier.
        require(liarsoft::exeConvertEncoding(altered, 0x80, 0x86) == altered);
    }

    const std::string input = "exe_patch_test_input.bin";
    const std::string output = "exe_patch_test_output.bin";
    const std::string unsupported = "exe_patch_test_unsupported.bin";
    std::remove(input.c_str());
    std::remove(output.c_str());
    std::remove(unsupported.c_str());

    writeFile(input, mixed);
    liarsoft::exeConvertFile(input, output, "CP1251");
    require(readFile(output) == expected);

    writeFile(input, optimizedFont);
    liarsoft::exeConvertFile(input, output, "CP1251");
    require(readFile(output) == interleavedFont(0xCC));

    // A patched font charset does not imply that punctuation tables were patched.
    auto mixedRegister = register932;
    mixedRegister[2] = 0x86;
    writeFile(input, mixedRegister);
    liarsoft::exeConvertFile(input, output, "GBK");
    require(readFile(output) == registerGbk);

    writeFile(unsupported, {0x4D, 0x5A, 0x00});
    bool rejected = false;
    try {
        liarsoft::exeConvertFile(unsupported, output, "GBK");
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected);

    std::remove(input.c_str());
    std::remove(output.c_str());
    std::remove(unsupported.c_str());
    return 0;
}
