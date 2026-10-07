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

/// Encode RGBA as version-3 LIM (separate A/R/G/B channels).
std::vector<uint8_t> limEncode(const LimImage& img);

/// Decode a still WebP image to RGBA. Animated WebP is rejected.
LimImage webpDecode(const std::vector<uint8_t>& data);

/// Encode lossless WebP, including RGB values under fully transparent pixels.
std::vector<uint8_t> webpEncode(const LimImage& img);

void limSaveWebp(const LimImage& img, const std::string& path);

} // namespace liarsoft

#endif // LIARSOFTTOOL_LIM_DECODER_H
