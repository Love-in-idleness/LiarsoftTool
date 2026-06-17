#include "lim_decoder.h"
#include "cg_decompress.h"
#include "stb_image_write.h"
#include <stdexcept>
#include <cstring>

namespace liarsoft {

LimImage limDecode(const std::vector<uint8_t>& data) {
    if (data.size() < 16 || data[0] != 'L' || data[1] != 'M')
        throw std::runtime_error("Not a valid LIM image");

    const uint8_t* p = data.data() + 2; // skip "LM"
    
    auto rU16 = [&](){ uint16_t v = p[0] | (p[1]<<8); p += 2; return v; };
    auto rU32 = [&](){ uint32_t v = p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24); p += 4; return v; };

    uint16_t flags = rU16();
    uint16_t bppF  = rU16();
    rU16(); // skip
    uint32_t w = rU32();
    uint32_t h = rU32();
    int bpp = (bppF == 0x10) ? 16 : 32;
    size_t n = static_cast<size_t>(w) * h;

    std::vector<uint8_t> m_index; // reusable palette
    LimImage img;
    img.width = w;
    img.height = h;

    if (bpp == 32) {
        // 4 channels, each decompressed separately, card=3
        std::vector<uint8_t> raw(n * 4, 0);
        uint8_t mask = 0xFF;
        for (int ch = 3; ch >= 0; --ch) {
            cg_decompress(raw, static_cast<size_t>(ch), 4, p, 1, 3, m_index);
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
                
                // For 16bpp, read header and determine card
                const uint8_t* save = p;
                uint32_t imgSz = p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24); p += 4;
                uint32_t rem  = p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24); p += 4;
                uint16_t idxCnt = p[0] | (p[1]<<8); p += 2;
                int card16 = (static_cast<int>(idxCnt) * 2 > 8192) ? 4 : 3;
                p = save; // reset
                cg_decompress_16bpp(raw16, n * 2, p, card16, m_index);
            } else {
                for (size_t i = 0; i < n * 2; ++i)
                    raw16[i] = *p++;
            }
        }

        // Decode alpha if present
        std::vector<uint8_t> alpha;
        if (hasAlpha) {
            if (flags & 0xE00) {
                alpha.resize(n, 0);
                cg_decompress(alpha, 0, 1, p, 1, 3, m_index);
            } else {
                alpha.assign(p, p + n);
                p += n;
            }
        }

        // Convert to RGBA
        img.pixels.resize(n * 4, 0xFF);
        for (size_t i = 0; i < n; ++i) {
            uint16_t px = raw16[i*2] | (raw16[i*2+1] << 8);
            img.pixels[i*4+0] = ((px >> 11) & 0x1F) * 255 / 31;
            img.pixels[i*4+1] = ((px >> 5)  & 0x3F) * 255 / 63;
            img.pixels[i*4+2] = ((px >> 0)  & 0x1F) * 255 / 31;
            if (hasAlpha)
                img.pixels[i*4+3] = static_cast<uint8_t>(~alpha[i]);
        }
    }

    return img;
}

void limSavePng(const LimImage& img, const std::string& path) {
    if (!stbi_write_png(path.c_str(), img.width, img.height, 4, img.pixels.data(), img.width*4))
        throw std::runtime_error("Failed to write PNG: " + path);
}

} // namespace liarsoft
