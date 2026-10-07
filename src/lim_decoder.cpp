#include "lim_decoder.h"
#include "cg_decompress.h"
#include "fileio.h"
#include "wcg_decoder.h"
#include <webp/decode.h>
#include <webp/encode.h>
#include <memory>
#include <stdexcept>
#include <cstring>

namespace liarsoft {

LimImage limDecode(const std::vector<uint8_t>& data) {
    if (data.size() < 16 || data[0] != 'L' || data[1] != 'M')
        throw std::runtime_error("Not a valid LIM image");

    const uint8_t* p = data.data() + 2; // skip "LM"
    const uint8_t* end = data.data() + data.size();
    
    auto rU16 = [&](){ uint16_t v = p[0] | (p[1]<<8); p += 2; return v; };
    auto rU32 = [&](){ uint32_t v = uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24); p += 4; return v; };

    uint16_t flags = rU16();
    uint16_t bppF  = rU16();
    rU16(); // skip
    uint32_t w = rU32();
    uint32_t h = rU32();
    int bpp = (bppF == 0x10) ? 16 : 32;
    const size_t imageSize = checkedRgbaSize(w, h);
    const size_t n = imageSize / 4;
    if ((bpp == 32 || (flags & 0x110)) && n > uint64_t(data.size() - 16) * 17)
        throw std::runtime_error("LIM dimensions exceed the available pixel data");

    std::vector<uint8_t> m_index; // reusable palette
    LimImage img;
    img.width = w;
    img.height = h;

    if (bpp == 32) {
        // 4 channels, each decompressed separately, card=3
        std::vector<uint8_t> raw(n * 4, 0);
        uint8_t mask = 0xFF;
        // Cannonball.exe 0x43bab6/0x43bac9: version 3 always has alpha,
        // but RGB blocks exist only when flag 0x10 is set.
        for (int ch = 3; ch >= 0; --ch) {
            if (ch != 3 && !(flags & 0x10)) continue;
            cg_decompress(raw, static_cast<size_t>(ch), 4, p, 1, 3, m_index, end);
            for (size_t i = static_cast<size_t>(ch); i < raw.size(); i += 4)
                raw[i] ^= mask;
            mask = 0;
        }
        // BGRA → RGBA
        img.pixels.resize(n * 4);
        for (size_t i = 0; i < n; ++i) {
            img.pixels[i*4+0] = raw[i*4+2];
            img.pixels[i*4+1] = raw[i*4+1];
            img.pixels[i*4+2] = raw[i*4+0];
            img.pixels[i*4+3] = raw[i*4+3];
        }
    } else { // bpp == 16
        std::vector<uint8_t> raw16(n * 2, 0);
        bool hasAlpha = (flags & 0x100) != 0;

        // Decode BGR565 image
        if (flags & 0x10) {
            if (flags & 0xE0) {
                // Table coding is determined by the shared decompressor.
                cg_decompress_16bpp(raw16, n * 2, p, 0, m_index, end);
            } else {
                if (n * 2 > static_cast<size_t>(end - p))
                    throw std::runtime_error("Truncated LIM 16bpp pixels");
                for (size_t i = 0; i < n * 2; ++i)
                    raw16[i] = *p++;
            }
        }

        // Decode alpha if present
        std::vector<uint8_t> alpha;
        if (hasAlpha) {
            if (flags & 0xE00) {
                alpha.resize(n, 0);
                cg_decompress(alpha, 0, 1, p, 1, 3, m_index, end);
            } else {
                if (n > static_cast<size_t>(end - p))
                    throw std::runtime_error("Truncated LIM alpha channel");
                alpha.assign(p, p + n);
                p += n;
            }
        }

        // Convert to RGBA
        img.pixels.resize(n * 4, 0xFF);
        for (size_t i = 0; i < n; ++i) {
            uint16_t px = raw16[i*2] | (raw16[i*2+1] << 8);
            // Match 0x43bd56..0x43bd89: expand by shifting, and use pure
            // BGR565 green as a transparency key unless a separate alpha
            // channel overrides it afterwards.
            img.pixels[i*4+0] = ((px >> 11) & 0x1F) << 3;
            img.pixels[i*4+1] = ((px >> 5)  & 0x3F) << 2;
            img.pixels[i*4+2] = (px & 0x1F) << 3;
            img.pixels[i*4+3] = px == 0x07e0 ? 0 : 0xff;
            if (hasAlpha)
                img.pixels[i*4+3] = static_cast<uint8_t>(~alpha[i]);
        }
    }

    return img;
}

std::vector<uint8_t> limEncode(const LimImage& img) {
    auto data = wcgEncode(img.pixels, img.width, img.height, true);
    // Version 3 uses precisely the WCG single-channel block framing, but a
    // LIM header: RGB present (0x10), depth 24, and no WCG-specific flags.
    data[0] = 'L'; data[1] = 'M';
    data[2] = 0x13; data[3] = 0;
    data[4] = 24; data[5] = 0;
    data[6] = 0; data[7] = 0;
    return data;
}

LimImage webpDecode(const std::vector<uint8_t>& data) {
    WebPBitstreamFeatures features;
    if (WebPGetFeatures(data.data(), data.size(), &features) != VP8_STATUS_OK)
        throw std::runtime_error("Not a valid WebP image");
    if (features.has_animation)
        throw std::runtime_error("Animated WebP cannot be converted to LIM");
    LimImage img;
    img.width = static_cast<uint32_t>(features.width);
    img.height = static_cast<uint32_t>(features.height);
    img.pixels.resize(checkedRgbaSize(img.width, img.height));
    if (!WebPDecodeRGBAInto(data.data(), data.size(), img.pixels.data(),
                            img.pixels.size(), features.width * 4))
        throw std::runtime_error("Failed to decode WebP image");
    return img;
}

std::vector<uint8_t> webpEncode(const LimImage& img) {
    if (img.pixels.size() != checkedRgbaSize(img.width, img.height))
        throw std::runtime_error("LIM pixel buffer does not match image dimensions");
    if (img.width > WEBP_MAX_DIMENSION || img.height > WEBP_MAX_DIMENSION)
        throw std::runtime_error("WebP dimensions must not exceed 16383 pixels");
    WebPConfig config;
    WebPPicture picture;
    if (!WebPConfigInit(&config) || !WebPPictureInit(&picture))
        throw std::runtime_error("Failed to initialize WebP encoder");
    config.lossless = 1;
    config.exact = 1; // Preserve invisible RGB; the simple lossless API does not.
    config.quality = 100;
    std::unique_ptr<WebPPicture, decltype(&WebPPictureFree)> pixels(
        &picture, WebPPictureFree);
    picture.use_argb = 1;
    picture.width = static_cast<int>(img.width);
    picture.height = static_cast<int>(img.height);
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    std::unique_ptr<WebPMemoryWriter, decltype(&WebPMemoryWriterClear)> output(
        &writer, WebPMemoryWriterClear);
    picture.writer = WebPMemoryWrite;
    picture.custom_ptr = &writer;
    if (!WebPValidateConfig(&config) ||
        !WebPPictureImportRGBA(&picture, img.pixels.data(), picture.width * 4) ||
        !WebPEncode(&config, &picture))
        throw std::runtime_error("Failed to encode lossless WebP image");
    return {writer.mem, writer.mem + writer.size};
}

void limSaveWebp(const LimImage& img, const std::string& path) {
    // Identical content already on disk is left untouched (mtime preserved).
    writeFileIfChanged(path, webpEncode(img));
}

} // namespace liarsoft
