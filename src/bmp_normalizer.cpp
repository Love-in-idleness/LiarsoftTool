#include "bmp_normalizer.h"
#include "fileio.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace liarsoft {
namespace {
uint32_t u32(const std::vector<uint8_t>& data, size_t pos) {
    return uint32_t(data[pos]) | (uint32_t(data[pos + 1]) << 8) |
           (uint32_t(data[pos + 2]) << 16) | (uint32_t(data[pos + 3]) << 24);
}
void put32(std::vector<uint8_t>& data, size_t pos, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data[pos + i] = uint8_t(value >> (8 * i));
}
} // namespace

std::vector<uint8_t> normalizeCodexBmp(const std::vector<uint8_t>& data) {
    if (data.size() < 18 || data[0] != 'B' || data[1] != 'M')
        throw std::runtime_error("Invalid BMP header");
    const auto header = u32(data, 14);
    if (header < 12 || header > data.size() - 14)
        throw std::runtime_error("Truncated BMP DIB header");
    if (header != 40 || data[28] != 32 || data[29] != 0 || u32(data, 30) != 0)
        return data;

    const auto width = u32(data, 18), height = u32(data, 22);
    const auto offset = u32(data, 10), declared = u32(data, 2);
    // The CodeX variant is bottom-up, with one plane and four bytes per pixel.
    if (!width || width > INT32_MAX || !height || height > INT32_MAX ||
        data[26] != 1 || data[27] != 0 || offset < 54 || offset > data.size())
        throw std::runtime_error("Unsupported CodeX BMP geometry");
    const uint64_t pixels = uint64_t(width) * height * 4;
    if (pixels > data.size() - offset ||
        (declared && (declared > data.size() || declared < uint64_t(offset) + pixels)))
        throw std::runtime_error("Truncated CodeX BMP pixels");
    constexpr size_t extra = 108 - 40;
    if (data.size() > std::numeric_limits<uint32_t>::max() - extra)
        throw std::runtime_error("BMP exceeds 32-bit file size");

    // BI_RGB's unused high byte is not portable alpha (SDL treats all-zero
    // alpha as opaque). V4's explicit alpha mask also makes this idempotent.
    std::vector<uint8_t> output(data.size() + extra, 0);
    std::copy_n(data.begin(), 54, output.begin());
    std::copy(data.begin() + 54, data.end(), output.begin() + 54 + extra);
    put32(output, 2, static_cast<uint32_t>(output.size()));
    put32(output, 10, offset + extra);
    put32(output, 14, 108);
    put32(output, 30, 3); // BI_BITFIELDS
    put32(output, 34, static_cast<uint32_t>(pixels));
    put32(output, 54, 0x00ff0000); // R
    put32(output, 58, 0x0000ff00); // G
    put32(output, 62, 0x000000ff); // B
    put32(output, 66, 0xff000000); // A
    put32(output, 70, 0x73524742); // LCS_sRGB
    for (size_t pos = offset + extra + 3; pos < offset + extra + pixels; pos += 4)
        output[pos] = 255 - output[pos];
    return output;
}

void normalizeCodexBmpFile(const std::string& input, const std::string& output) {
    const auto time = std::filesystem::last_write_time(input);
    writeFileIfChanged(output, normalizeCodexBmp(readFile(input)));
    std::filesystem::last_write_time(output, time);
}
} // namespace liarsoft
