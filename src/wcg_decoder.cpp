#include "wcg_decoder.h"
#include "cg_decompress.h"
#include "stb_image_write.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace liarsoft {

// ---- Decode (matches GARbro Reader / arc_unpacker wcg_image_decoder) ----

WcgImage wcgDecode(const std::vector<uint8_t>& data) {
    if (data.size() < 16 || data[0] != 'W' || data[1] != 'G')
        throw std::runtime_error("Not a valid WCG image");

    const uint8_t* p = data.data() + 2; // skip "WG"
    
    // version u16
    uint16_t ver = p[0] | (p[1]<<8); p += 2;
    if ((ver & 0xF) != 1 || (ver & 0x1C0) != 64)
        throw std::runtime_error("WCG: unsupported version");
    
    // depth u16
    uint16_t depth = p[0] | (p[1]<<8); p += 2;
    if (depth != 32)
        throw std::runtime_error("WCG: unsupported depth");
    
    p += 2; // skip 2
    
    uint32_t w = p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24); p += 4;
    uint32_t h = p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24); p += 4;
    size_t n = static_cast<size_t>(w) * h;

    // Output is BGRA8888 (matching arc_unpacker)
    std::vector<uint8_t> pixels(n * 4, 0);
    std::vector<uint8_t> m_index;

    cg_decompress(pixels, 2, 4, p, 2, 0, m_index);
    cg_decompress(pixels, 0, 4, p, 2, 0, m_index);

    // Invert alpha (matches arc_unpacker and GARbro)
    for (size_t i = 3; i < pixels.size(); i += 4)
        pixels[i] ^= 0xFF;

    return {w, h, std::move(pixels)};
}

void wcgSavePng(const WcgImage& img, const std::string& path) {
    // Convert BGRA → RGBA for PNG output
    size_t n = static_cast<size_t>(img.width) * img.height;
    std::vector<uint8_t> rgba(n * 4);
    for (size_t i = 0; i < n; ++i) {
        rgba[i*4+0] = img.pixels[i*4+2]; // B→R
        rgba[i*4+1] = img.pixels[i*4+1]; // G→G
        rgba[i*4+2] = img.pixels[i*4+0]; // R→B
        rgba[i*4+3] = img.pixels[i*4+3]; // A→A
    }
    if (!stbi_write_png(path.c_str(), img.width, img.height, 4, rgba.data(), img.width*4))
        throw std::runtime_error("Failed to write PNG: " + path);
}

// ---- Encode (matches GARbro Writer.Pack) ----

static void wU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(v & 0xFF); out.push_back((v>>8) & 0xFF);
}
static void wU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(v & 0xFF); out.push_back((v>>8) & 0xFF);
    out.push_back((v>>16) & 0xFF); out.push_back((v>>24) & 0xFF);
}

// GARbro-style bit writer (PutBit / PutBits / Flush)
struct GbBitWriter {
    std::vector<uint8_t>& out;
    int bits = 1;  // GARbro starts at 1
    
    explicit GbBitWriter(std::vector<uint8_t>& o) : out(o) {}
    
    void putBit(bool bit) {
        bits <<= 1;
        bits |= bit ? 1 : 0;
        if (bits & 0x100) {
            out.push_back(static_cast<uint8_t>(bits & 0xFF));
            bits = 1;
        }
    }
    
    void putBits(uint32_t length, uint32_t x) {
        x <<= (32 - length);
        while (length--) {
            putBit((x & 0x80000000) != 0);
            x <<= 1;
        }
    }
    
    void flush() {
        if (bits != 1) {
            do { bits <<= 1; } while ((bits & 0x100) == 0);
            out.push_back(static_cast<uint8_t>(bits & 0xFF));
            bits = 1;
        }
    }
};

static uint32_t getBitsLength(uint16_t val) {
    uint32_t len = 0;
    do { ++len; val >>= 1; } while (val != 0);
    return len;
}

static void putIndex(GbBitWriter& bw, uint16_t index, 
                     uint32_t baseLength, uint32_t baseIndexLength) {
    uint32_t length = getBitsLength(index);
    if (length < baseIndexLength) {
        bw.putBits(baseLength, length);
        if (length == 1)
            bw.putBit(index != 0);
        else
            bw.putBits(length - 1, index);
    } else {
        bw.putBits(baseLength, baseIndexLength);
        for (uint32_t i = baseIndexLength; i < length; ++i)
            bw.putBit(true);
        bw.putBit(false);
        bw.putBits(length - 1, index);
    }
}

// Pack one pass (data=0 for low/RA pass, data=1 for high/BG pass? No - original uses offset into data array)
// GARbro Writer.Pack: first call with (1, 0xff00), second with (0, 0)
// The 'data' parameter is the starting offset in the pixel array for this pass.
// mask is XOR'd with the word before index lookup.
static std::vector<uint8_t> packPass(const uint8_t* bgra, size_t n, int data, uint16_t mask) {
    std::vector<uint8_t> out;
    GbBitWriter bw(out);
    
    // Build frequency-sorted index (GARbro BuildIndex)
    std::unordered_map<uint16_t, uint16_t> index;  // color -> palette_index
    std::vector<std::pair<uint16_t, uint32_t>> freq;
    
    {
        std::unordered_map<uint16_t, uint32_t> freqMap;
        for (size_t i = 0; i < n; ++i) {
            uint8_t b0, b1;
            if (data == 1) {
                b0 = bgra[i*4 + 2];          // R (BGRA offset 2)
                b1 = bgra[i*4 + 3] ^ 0xFF;    // A inverted
            } else {
                b0 = bgra[i*4 + 0];          // B
                b1 = bgra[i*4 + 1];          // G
            }
            uint16_t word = b0 | (static_cast<uint16_t>(b1) << 8);
            freqMap[word ^ mask]++;
        }
        for (auto& kv : freqMap) freq.push_back({kv.first, kv.second});
        std::sort(freq.begin(), freq.end(),
                  [](auto& a, auto& b) { return a.second > b.second; });
    }
    
    bool smallIndex = freq.size() < 0x1002;
    uint32_t baseLength = smallIndex ? 3 : 4;
    uint32_t baseIndexLength = smallIndex ? 7 : 15;
    
    // Write header FIRST (placeholders, to be patched later)
    size_t headerPos = out.size();
    wU32(out, 0); // size_orig placeholder
    wU32(out, 0); // size_comp placeholder
    wU16(out, static_cast<uint16_t>(freq.size())); // table count
    wU16(out, smallIndex ? 7 : 14); // skip (GARbro writes 7 or 14)
    
    // Write palette (GARbro BuildIndex writes palette during index building)
    uint16_t j = 0;
    for (auto& kv : freq) {
        wU16(out, kv.first);
        index[kv.first] = j++;
    }
    
    // Encode pixels (GARbro Pack)
    for (size_t i = 0; i < n; ) {
        uint8_t b0, b1;
        if (data == 1) {
            b0 = bgra[i*4 + 2];
            b1 = bgra[i*4 + 3] ^ 0xFF;
        } else {
            b0 = bgra[i*4 + 0];
            b1 = bgra[i*4 + 1];
        }
        uint16_t word = b0 | (static_cast<uint16_t>(b1) << 8);
        uint16_t color = word ^ mask;
        auto it = index.find(color);
        if (it == index.end()) it = index.begin();
        uint16_t idx = it->second;
        
        uint32_t runLen = 1;
        ++i;
        while (i < n) {
            uint8_t nb0, nb1;
            if (data == 1) {
                nb0 = bgra[i*4 + 2];
                nb1 = bgra[i*4 + 3] ^ 0xFF;
            } else {
                nb0 = bgra[i*4 + 0];
                nb1 = bgra[i*4 + 1];
            }
            uint16_t nw = nb0 | (static_cast<uint16_t>(nb1) << 8);
            if ((nw ^ mask) != color) break;
            ++runLen;
            ++i;
            if (runLen >= 0x11) break;
        }
        
        if (runLen > 1) {
            bw.putBits(baseLength, 0);
            bw.putBits(4, runLen - 2);
        }
        putIndex(bw, idx, baseLength, baseIndexLength);
    }
    bw.flush();
    
    // Patch header
    uint32_t dataSize = static_cast<uint32_t>(out.size() - headerPos - 12 - freq.size() * 2);
    uint8_t* hdr = out.data() + headerPos;
    uint32_t pixels_x2 = static_cast<uint32_t>(n * 2);
    hdr[0] = pixels_x2 & 0xFF; hdr[1] = (pixels_x2 >> 8) & 0xFF;
    hdr[2] = (pixels_x2 >> 16) & 0xFF; hdr[3] = (pixels_x2 >> 24) & 0xFF;
    hdr[4] = dataSize & 0xFF; hdr[5] = (dataSize >> 8) & 0xFF;
    hdr[6] = (dataSize >> 16) & 0xFF; hdr[7] = (dataSize >> 24) & 0xFF;
    
    return out;
}

std::vector<uint8_t> wcgEncode(const uint8_t* rgba, uint32_t width, uint32_t height) {
    // rgba is RGBA8888 — convert to BGRA for the encoder
    size_t n = static_cast<size_t>(width) * height;
    std::vector<uint8_t> bgra(n * 4);
    for (size_t i = 0; i < n; ++i) {
        bgra[i*4+0] = rgba[i*4+2]; // B
        bgra[i*4+1] = rgba[i*4+1]; // G
        bgra[i*4+2] = rgba[i*4+0]; // R
        bgra[i*4+3] = rgba[i*4+3]; // A
    }
    
    std::vector<uint8_t> out;
    // Header: magic + version + depth + pad + width + height
    out.push_back('W'); out.push_back('G');
    wU16(out, 0x0271);       // version (low nibble=1, bits 6-8=64)
    wU16(out, 32);           // depth
    wU16(out, 0x4000);       // pad/skip
    
    std::vector<uint8_t> w(width * 4);
    wU32(out, width);
    wU32(out, height);
    
    auto pass1 = packPass(bgra.data(), n, 1, 0); // low: R+A, mask=0xFF00
    auto pass2 = packPass(bgra.data(), n, 0, 0);      // high: B+G, mask=0
    
    out.insert(out.end(), pass1.begin(), pass1.end());
    out.insert(out.end(), pass2.begin(), pass2.end());
    return out;
}

std::vector<uint8_t> wcgEncode(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height) {
    return wcgEncode(rgba.data(), width, height);
}

} // namespace liarsoft
