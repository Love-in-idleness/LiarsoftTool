#include "exe_patch.h"
#include <fstream>
#include <stdexcept>
#include <cstring>

namespace liarsoft {

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
    size_t sz = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(sz);
    in.read(reinterpret_cast<char*>(data.data()), sz);

    // Map encoding to code-page byte:
    //   SJIS=0x80, GBK=0x86, CP1251=0xCC
    // Forward: always from 0x80 (SJIS default) to target
    // Reverse (encoding=="SHIFT_JIS"): detect current byte and revert to 0x80
    uint8_t fromByte = 0x80;
    uint8_t toByte   = 0x80;

    if (encoding == "GBK") {
        toByte = 0x86;
    } else if (encoding == "CP1251") {
        toByte = 0xCC;
    } else {
        // SHIFT_JIS: detect and revert to 0x80
        // Search for the 2-byte marker in pattern 1 context
        uint8_t p1gbk[17] = {0x6A,0x00,0x6A,0x00,0x6A,0x00,0x6A,0x00,0x68,0x86,0x00,0x00,0x00,0x6A,0x00,0x6A,0x00};
        uint8_t p1cp[17]  = {0x6A,0x00,0x6A,0x00,0x6A,0x00,0x6A,0x00,0x68,0xCC,0x00,0x00,0x00,0x6A,0x00,0x6A,0x00};
        bool found = false;
        for (size_t i = 0; i + 17 <= data.size(); ++i) {
            if (std::memcmp(&data[i], p1gbk, 17) == 0) { fromByte = 0x86; found = true; break; }
            if (std::memcmp(&data[i], p1cp, 17) == 0)  { fromByte = 0xCC; found = true; break; }
        }
        if (!found) return; // nothing to revert
        toByte = 0x80;
    }

    auto patched = exeConvertEncoding(data, fromByte, toByte);

    std::ofstream out(outputPath, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write: " + outputPath);
    out.write(reinterpret_cast<const char*>(patched.data()), patched.size());
}

} // namespace liarsoft

