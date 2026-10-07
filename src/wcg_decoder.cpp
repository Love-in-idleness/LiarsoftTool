#include "wcg_decoder.h"
#include "cg_decompress.h"
#include "fileio.h"
#include "stb_image_write.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace liarsoft {

namespace {

// stbi_write callback that appends encoded bytes to a memory buffer so the
// PNG can be compared against the existing file before writing.
struct PngBuffer {
    std::vector<uint8_t> bytes;
};

void appendPngBytes(void* context, void* data, int size) {
    auto* buf = static_cast<PngBuffer*>(context);
    auto* bytes = static_cast<const uint8_t*>(data);
    buf->bytes.insert(buf->bytes.end(), bytes, bytes + size);
}

} // namespace

// ---- Decode (Cannonball.exe's paired and single-channel paths) ----

WcgImage wcgDecode(const std::vector<uint8_t>& data) {
    if (data.size() < 16 || data[0] != 'W' || data[1] != 'G')
        throw std::runtime_error("Not a valid WCG image");

    const uint8_t* p = data.data() + 2; // skip "WG"
    
    // version u16
    uint16_t ver = p[0] | (p[1]<<8); p += 2;
    if ((ver & 0xF) != 1)
        throw std::runtime_error("WCG: unsupported version");
    
    // depth u16
    uint16_t depth = p[0] | (p[1]<<8); p += 2;
    if (depth != 32)
        throw std::runtime_error("WCG: unsupported depth");
    
    p += 2; // skip 2
    
    uint32_t w = uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24); p += 4;
    uint32_t h = uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24); p += 4;
    const size_t imageSize = checkedRgbaSize(w, h);
    // One run emits at most 17 pixels and takes more than one byte. Even
    // counting every remaining byte as compressed data is a conservative cap.
    if (imageSize / 4 > uint64_t(data.size() - 16) * 17)
        throw std::runtime_error("WCG dimensions exceed the available pixel data");

    // Output is BGRA8888 (matching arc_unpacker)
    std::vector<uint8_t> pixels(imageSize, 0);
    std::vector<uint8_t> m_index;

    const uint8_t* end = data.data() + data.size();
    if ((ver & 0x1C0) == 0x40) {
        cg_decompress(pixels, 2, 4, p, 2, 0, m_index, end);
        cg_decompress(pixels, 0, 4, p, 2, 0, m_index, end);
    } else {
        // Cannonball.exe 0x43c0da: A first, then R/G/B only when bit 0x10
        // is set. Mask-only files have no RGB data; leave those bytes zero.
        cg_decompress(pixels, 3, 4, p, 1, 3, m_index, end);
        if (ver & 0x10)
            for (int channel = 2; channel >= 0; --channel)
                cg_decompress(pixels, channel, 4, p, 1, 3, m_index, end);
    }

    // Invert alpha (matches arc_unpacker and GARbro)
    for (size_t i = 3; i < pixels.size(); i += 4)
        pixels[i] ^= 0xFF;

    return {w, h, std::move(pixels)};
}

void wcgSavePng(const WcgImage& img, const std::string& path) {
    // Convert BGRA → RGBA for PNG output
    const size_t imageSize = checkedRgbaSize(img.width, img.height);
    if (img.pixels.size() != imageSize)
        throw std::runtime_error("WCG pixel buffer does not match image dimensions");
    const size_t n = imageSize / 4;
    std::vector<uint8_t> rgba(imageSize);
    for (size_t i = 0; i < n; ++i) {
        rgba[i*4+0] = img.pixels[i*4+2]; // B→R
        rgba[i*4+1] = img.pixels[i*4+1]; // G→G
        rgba[i*4+2] = img.pixels[i*4+0]; // R→B
        rgba[i*4+3] = img.pixels[i*4+3]; // A→A
    }
    PngBuffer buf;
    if (!stbi_write_png_to_func(appendPngBytes, &buf, img.width, img.height, 4,
                                rgba.data(), img.width * 4))
        throw std::runtime_error("Failed to encode PNG: " + path);
    // Identical content already on disk is left untouched (mtime preserved).
    writeFileIfChanged(path, buf.bytes);
}

// ---- Encode (paired channels, or lossless single-channel fallback) ----

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

// Alpha has already been inverted. An empty result requests single-channel
// fallback: 65536 paired colors cannot fit in the on-disk uint16_t count.
static std::vector<uint8_t> packPass(const uint8_t* bgra, size_t n, int offset, int stride) {
    std::vector<uint8_t> out;
    GbBitWriter bw(out);
    
    // Build frequency-sorted index (GARbro BuildIndex)
    std::unordered_map<uint16_t, uint16_t> index;  // color -> palette_index
    std::vector<std::pair<uint16_t, uint32_t>> freq;
    auto colorAt = [&](size_t pixel) {
        const uint8_t* p = bgra + pixel * 4 + offset;
        return static_cast<uint16_t>(p[0] | (stride == 2 ? uint16_t(p[1]) << 8 : 0));
    };
    
    {
        std::unordered_map<uint16_t, uint32_t> freqMap;
        for (size_t i = 0; i < n; ++i) {
            freqMap[colorAt(i)]++;
        }
        if (freqMap.size() > 0xffff) return {};
        for (auto& kv : freqMap) freq.push_back({kv.first, kv.second});
        std::sort(freq.begin(), freq.end(),
                  [](auto& a, auto& b) {
                      return a.second != b.second ? a.second > b.second : a.first < b.first;
                  });
    }
    
    bool smallIndex = freq.size() <= 0x1000;
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
        out.push_back(kv.first & 0xff);
        if (stride == 2) out.push_back(kv.first >> 8);
        index[kv.first] = j++;
    }
    
    // Encode pixels (GARbro Pack)
    for (size_t i = 0; i < n; ) {
        uint16_t color = colorAt(i);
        uint16_t idx = index.at(color);
        
        uint32_t runLen = 1;
        ++i;
        while (i < n) {
            if (colorAt(i) != color) break;
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
    uint32_t dataSize = static_cast<uint32_t>(out.size() - headerPos - 12 - freq.size() * stride);
    uint8_t* hdr = out.data() + headerPos;
    uint32_t originalSize = static_cast<uint32_t>(n * stride);
    hdr[0] = originalSize & 0xFF; hdr[1] = (originalSize >> 8) & 0xFF;
    hdr[2] = (originalSize >> 16) & 0xFF; hdr[3] = (originalSize >> 24) & 0xFF;
    hdr[4] = dataSize & 0xFF; hdr[5] = (dataSize >> 8) & 0xFF;
    hdr[6] = (dataSize >> 16) & 0xFF; hdr[7] = (dataSize >> 24) & 0xFF;
    
    return out;
}

std::vector<uint8_t> wcgEncode(const uint8_t* rgba, uint32_t width, uint32_t height,
                             bool separateChannels) {
    // rgba is RGBA8888 — convert to BGRA for the encoder
    const size_t imageSize = checkedRgbaSize(width, height);
    if (!rgba) throw std::runtime_error("Missing RGBA pixel buffer");
    const size_t n = imageSize / 4;
    std::vector<uint8_t> bgra(imageSize);
    for (size_t i = 0; i < n; ++i) {
        bgra[i*4+0] = rgba[i*4+2]; // B
        bgra[i*4+1] = rgba[i*4+1]; // G
        bgra[i*4+2] = rgba[i*4+0]; // R
        bgra[i*4+3] = rgba[i*4+3] ^ 0xff; // WCG stores inverted alpha
    }

    auto pass1 = separateChannels ? std::vector<uint8_t>{} :
                                   packPass(bgra.data(), n, 2, 2); // R + inverted alpha
    auto pass2 = pass1.empty() ? std::vector<uint8_t>{} : packPass(bgra.data(), n, 0, 2);
    const bool paired = !pass1.empty() && !pass2.empty();
    
    std::vector<uint8_t> out;
    // Header: magic + version + depth + pad + width + height
    out.push_back('W'); out.push_back('G');
    wU16(out, paired ? 0x0271 : 0x0231); // paired or A/R/G/B streams
    wU16(out, 32);           // depth
    wU16(out, 0x4000);       // pad/skip
    
    wU32(out, width);
    wU32(out, height);
    
    if (paired) {
        out.insert(out.end(), pass1.begin(), pass1.end());
        out.insert(out.end(), pass2.begin(), pass2.end());
    } else {
        for (int channel = 3; channel >= 0; --channel) {
            const auto pass = packPass(bgra.data(), n, channel, 1);
            out.insert(out.end(), pass.begin(), pass.end());
        }
    }
    return out;
}

std::vector<uint8_t> wcgEncode(const std::vector<uint8_t>& rgba, uint32_t width,
                             uint32_t height, bool separateChannels) {
    if (rgba.size() != checkedRgbaSize(width, height))
        throw std::runtime_error("RGBA pixel buffer does not match image dimensions");
    return wcgEncode(rgba.data(), width, height, separateChannels);
}

} // namespace liarsoft
