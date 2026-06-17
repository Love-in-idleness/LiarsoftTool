#ifndef LIARSOFTTOOL_WCG_DECODER_H
#define LIARSOFTTOOL_WCG_DECODER_H

#include <cstdint>
#include <vector>
#include <string>

namespace liarsoft {

struct WcgImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels; // BGRA8888 (internal), converted to RGBA for PNG
};

/// Decode WCG → BGRA pixels.
WcgImage wcgDecode(const std::vector<uint8_t>& data);

/// Save as PNG (handles BGRA→RGBA internally).
void wcgSavePng(const WcgImage& img, const std::string& path);

/// Encode RGBA pixels → WCG binary (PNG→WCG).
/// `pixels` is RGBA8888, width × height × 4.
std::vector<uint8_t> wcgEncode(const uint8_t* rgba, uint32_t width, uint32_t height);

/// Encode from a raw RGBA file buffer.
std::vector<uint8_t> wcgEncode(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height);

} // namespace liarsoft

#endif // LIARSOFTTOOL_WCG_DECODER_H
