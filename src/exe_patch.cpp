#include "exe_patch.h"
#include <fstream>
#include <stdexcept>
#include <cstring>

namespace liarsoft {

std::vector<uint8_t> exeConvertEncoding(const std::vector<uint8_t>& data, bool toGBK) {
    std::vector<uint8_t> out = data;

    // Pattern 1 (17 bytes):
    //   6A 00 6A 00 6A 00 6A 00  68 XX 00 00 00  6A 00 6A 00
    uint8_t pat1_from[17] = {
        0x6A,0x00,0x6A,0x00,0x6A,0x00,0x6A,0x00,
        0x68,0x80,0x00,0x00,0x00,
        0x6A,0x00,0x6A,0x00
    };
    uint8_t pat1_to[17] = {
        0x6A,0x00,0x6A,0x00,0x6A,0x00,0x6A,0x00,
        0x68,0x86,0x00,0x00,0x00,
        0x6A,0x00,0x6A,0x00
    };

    // Pattern 2 (3 bytes): DA 68 XX
    uint8_t pat2_from[3] = {0xDA, 0x68, 0x80};
    uint8_t pat2_to[3]   = {0xDA, 0x68, 0x86};

    if (!toGBK) {
        // Reverse: swap from/to
        std::swap(pat1_from[9], pat1_to[9]);
        std::swap(pat2_from[2], pat2_to[2]);
    }

    // Apply pattern 1
    for (size_t i = 0; i + 17 <= out.size(); ++i) {
        if (std::memcmp(&out[i], pat1_from, 17) == 0) {
            std::memcpy(&out[i], pat1_to, 17);
            i += 16;
        }
    }

    // Apply pattern 2
    for (size_t i = 0; i + 3 <= out.size(); ++i) {
        if (std::memcmp(&out[i], pat2_from, 3) == 0) {
            std::memcpy(&out[i], pat2_to, 3);
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

    bool toGBK = (encoding == "GBK");
    auto patched = exeConvertEncoding(data, toGBK);

    std::ofstream out(outputPath, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write: " + outputPath);
    out.write(reinterpret_cast<const char*>(patched.data()), patched.size());
}

} // namespace liarsoft

