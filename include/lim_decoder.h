#ifndef LIARSOFTTOOL_LIM_DECODER_H
#define LIARSOFTTOOL_LIM_DECODER_H

#include <cstdint>
#include <vector>
#include <string>

namespace liarsoft {

struct LimImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels; // RGBA8888
};

/// Decode LIM → RGBA pixels.
LimImage limDecode(const std::vector<uint8_t>& data);

/// Save as PNG.
void limSavePng(const LimImage& img, const std::string& path);

} // namespace liarsoft

#endif // LIARSOFTTOOL_LIM_DECODER_H
