#include "exe_patch.h"
#include "fileio.h"
#include <fstream>
#include <stdexcept>
#include <cstring>

namespace liarsoft {

namespace {

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
