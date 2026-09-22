#include "exe_patch.h"
#include "fileio.h"
#include <array>
#include <fstream>
#include <stdexcept>
#include <cstring>

namespace liarsoft {

namespace {

using KinsokuTable = std::array<uint16_t, 50>;

// Classic codeX RScript stores 38 characters forbidden at line start,
// followed by 12 characters forbidden at line end. Values are character
// bytes written in reading order (for example CP932 "。" is 0x8142).
constexpr KinsokuTable kCp932Kinsoku = {
    0x8140,0x8141,0x8142,0x8143,0x8144,0x8148,0x8149,0x815B,
    0x8160,0x8163,0x816A,0x816C,0x816E,0x8170,0x8172,0x8174,
    0x8176,0x8178,0x817A,0x8166,0x8168,0x82C1,0x829F,0x82A1,
    0x82A3,0x82A5,0x82A7,0x82E1,0x82E3,0x82E5,0x8340,0x8342,
    0x8344,0x8346,0x8348,0x8383,0x8385,0x8387,
    0x8165,0x8167,0x8169,0x816B,0x816D,0x816F,0x8171,0x8173,
    0x8175,0x8177,0x8179,0x814A
};

constexpr KinsokuTable kGbkKinsoku = {
    0xA1A1,0xA1A2,0xA1A3,0xA3AC,0xA3AE,0xA3BF,0xA3A1,0xA1AB,
    0xA1AD,0xA3A9,0xA1B3,0xA3DD,0xA3FD,0xA1B5,0xA1B7,0xA1B9,
    0xA1BB,0xA1BF,0xA1AF,0xA1B1,0xA3BA,0xA3BB,0xA3A5,0xA1A4,
    0xA1A8,0xA1A9,0xA1BD,0xA1E6,0xA1E3,0xA1E4,0xA1E5,0xA1EB,
    0x003A,0x003B,0x0025,0xFFFF,0xFFFF,0xFFFF,
    0xA1AE,0xA1B0,0xA3A8,0xA1B2,0xA3DB,0xA3FB,0xA1B4,0xA1B6,
    0xA1B8,0xA1BA,0xA1BE,0xA1BC
};

// Russian typography normally uses «...», with „...“ for nested quotes.
// Closing punctuation and suffix symbols may not start a line; opening
// punctuation and prefix symbols (№, §, currencies) may not end one. The
// classic parser sign-extends non-ASCII single bytes before this comparison.
// No-start: » “ ” ’ … : ; % ‰ ° – — - NBSP ™ ®
// No-end:   « „ ‘ ( [ { № § $ €
constexpr KinsokuTable kCp1251Kinsoku = {
    0xFFBB,0xFF93,0xFF94,0xFF92,0xFF85,0x003A,0x003B,0x0025,
    0xFF89,0xFFB0,0xFF96,0xFF97,0x002D,0xFFA0,0xFF99,0xFFAE,
    0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,
    0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,
    0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,
    0xFFAB,0xFF84,0xFF91,0x0028,0x005B,0x007B,0xFFB9,0xFFA7,
    0x0024,0xFF88,0x8000,0x8000
};

constexpr std::array<uint16_t, 3> kExtendedKinsoku = {
    0x213F, 0x3F21, 0x2121
};

const KinsokuTable* kinsokuTable(uint8_t charset) {
    if (charset == 0x80) return &kCp932Kinsoku;
    if (charset == 0x86) return &kGbkKinsoku;
    if (charset == 0xCC) return &kCp1251Kinsoku;
    return nullptr;
}

uint16_t readU16(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<uint16_t>(data[offset]) |
           (static_cast<uint16_t>(data[offset + 1]) << 8);
}

void writeU16(std::vector<uint8_t>& data, size_t offset, uint16_t value) {
    data[offset] = static_cast<uint8_t>(value);
    data[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void convertKinsokuTables(std::vector<uint8_t>& data, uint8_t fromCharset,
                          uint8_t toCharset) {
    const auto* from = kinsokuTable(fromCharset);
    const auto* to = kinsokuTable(toCharset);
    if (!from || !to || from == to) return;

    for (size_t start = 0; start + 11 <= data.size(); ++start) {
        if (data[start] != 0x66 || data[start + 1] != 0x81 ||
            data[start + 2] != 0xFE)
            continue;
        std::vector<size_t> operands;
        size_t offset = start;
        size_t noStartCount = 0;

        while (offset + 11 <= data.size() &&
               data[offset] == 0x66 && data[offset + 1] == 0x81 &&
               data[offset + 2] == 0xFE && data[offset + 5] == 0x0F &&
               data[offset + 6] == 0x84) {
            operands.push_back(offset + 3);
            ++noStartCount;
            offset += 11;
        }
        while (offset + 7 <= data.size() &&
               data[offset] == 0x66 && data[offset + 1] == 0x81 &&
               data[offset + 2] == 0xFE && data[offset + 5] == 0x74) {
            operands.push_back(offset + 3);
            offset += 7;
        }

        const bool extended = noStartCount == 41 && operands.size() == 53;
        if (!(noStartCount == 38 && operands.size() == 50) && !extended)
            continue;

        bool matches = true;
        for (size_t i = 0; i < 38; ++i)
            matches = matches && readU16(data, operands[i]) == (*from)[i];
        if (extended) {
            for (size_t i = 0; i < kExtendedKinsoku.size(); ++i)
                matches = matches &&
                    readU16(data, operands[38 + i]) == kExtendedKinsoku[i];
        }
        const size_t endBase = extended ? 41 : 38;
        for (size_t i = 38; i < from->size(); ++i)
            matches = matches &&
                readU16(data, operands[endBase + i - 38]) == (*from)[i];
        if (!matches) continue;

        for (size_t i = 0; i < 38; ++i)
            writeU16(data, operands[i], (*to)[i]);
        for (size_t i = 38; i < to->size(); ++i)
            writeU16(data, operands[endBase + i - 38], (*to)[i]);
        start = offset - 1;
    }
}

bool hasCharsetPattern(const std::vector<uint8_t>& data, uint8_t value) {
    const uint8_t pattern1[17] = {
        0x6A,0x00,0x6A,0x00,0x6A,0x00,0x6A,0x00,
        0x68,value,0x00,0x00,0x00,
        0x6A,0x00,0x6A,0x00
    };
    const uint8_t pattern2[3] = {0xDA, 0x68, value};

    for (size_t i = 0; i + sizeof(pattern1) <= data.size(); ++i) {
        if (std::memcmp(&data[i], pattern1, sizeof(pattern1)) == 0)
            return true;
    }
    for (size_t i = 0; i + sizeof(pattern2) <= data.size(); ++i) {
        if (std::memcmp(&data[i], pattern2, sizeof(pattern2)) == 0)
            return true;
    }
    return false;
}

} // namespace

std::vector<uint8_t> exeConvertEncoding(const std::vector<uint8_t>& data,
                                        uint8_t fromByte, uint8_t toByte) {
    if (fromByte == toByte) return data;
    std::vector<uint8_t> out = data;

    // Pattern 1 (17 bytes):
    //   6A 00 6A 00 6A 00 6A 00  68 XX 00 00 00  6A 00 6A 00
    uint8_t p1f[17] = {
        0x6A,0x00,0x6A,0x00,0x6A,0x00,0x6A,0x00,
        0x68,fromByte,0x00,0x00,0x00,
        0x6A,0x00,0x6A,0x00
    };
    uint8_t p1t[17] = {
        0x6A,0x00,0x6A,0x00,0x6A,0x00,0x6A,0x00,
        0x68,toByte,0x00,0x00,0x00,
        0x6A,0x00,0x6A,0x00
    };

    // Pattern 2 (3 bytes): DA 68 XX
    uint8_t p2f[3] = {0xDA, 0x68, fromByte};
    uint8_t p2t[3] = {0xDA, 0x68, toByte};

    for (size_t i = 0; i + 17 <= out.size(); ++i) {
        if (std::memcmp(&out[i], p1f, 17) == 0) {
            std::memcpy(&out[i], p1t, 17);
            i += 16;
        }
    }
    for (size_t i = 0; i + 3 <= out.size(); ++i) {
        if (std::memcmp(&out[i], p2f, 3) == 0) {
            std::memcpy(&out[i], p2t, 3);
            i += 2;
        }
    }
    convertKinsokuTables(out, fromByte, toByte);
    return out;
}

void exeConvertFile(const std::string& inputPath, const std::string& outputPath,
                    const std::string& encoding) {
    std::ifstream in(inputPath, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open: " + inputPath);
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end < 0) throw std::runtime_error("Cannot read: " + inputPath);
    const size_t sz = static_cast<size_t>(end);
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(sz);
    if (sz > 0) {
        in.read(reinterpret_cast<char*>(data.data()),
                static_cast<std::streamsize>(sz));
        if (!in) throw std::runtime_error("Cannot read: " + inputPath);
    }

    // These are Win32 font charset values, not code-page identifiers:
    // SHIFTJIS_CHARSET=0x80, GB2312_CHARSET=0x86, RUSSIAN_CHARSET=0xCC.
    uint8_t target;
    if (encoding == "CP932") target = 0x80;
    else if (encoding == "GBK") target = 0x86;
    else if (encoding == "CP1251") target = 0xCC;
    else throw std::runtime_error("Unsupported EXE encoding: " + encoding);

    const uint8_t supported[] = {0x80, 0x86, 0xCC};
    bool found = false;
    for (uint8_t value : supported)
        found = found || hasCharsetPattern(data, value);
    if (!found)
        throw std::runtime_error(
            "No supported RScript font charset pattern found in EXE: " + inputPath);

    // Normalize every recognized occurrence. Some released or previously
    // patched executables contain more than one charset value.
    auto patched = data;
    for (uint8_t source : supported)
        patched = exeConvertEncoding(patched, source, target);

    // Identical content already on disk is left untouched (mtime preserved).
    writeFileIfChanged(outputPath, patched);
}

} // namespace liarsoft
