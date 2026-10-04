#include "cg_decompress.h"
#include <algorithm>
#include <stdexcept>

namespace liarsoft {

static inline uint16_t readU16(const uint8_t*& p) {
    uint16_t v = p[0] | (p[1]<<8); p += 2; return v;
}
static inline uint32_t readU32(const uint8_t*& p) {
    uint32_t v = uint32_t(p[0]) | (uint32_t(p[1])<<8) |
                 (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24);
    p += 4; return v;
}

// MSB-first bit reader
static int getBits(int n, const uint8_t*& src, size_t& remaining, int& current, int& bits) {
    int v = 0;
    while (n > 0) {
        if (current == 0) {
            if (remaining == 0)
                throw std::runtime_error("Truncated bit stream in cg_decompress");
            bits = static_cast<int>(*src++);
            --remaining;
            current = 8;
        }
        v <<= 1;
        bits <<= 1;
        v |= (bits >> 8) & 1;
        --current;
        --n;
    }
    return v;
}

// Variable-length index decoder with dynamic thresholds.
// Matches GARbro ImageWCG.GetIndex and arc_unpacker cg_decompress.
// `index_bit_length` = 3 for small tables, 4 for large.
// `index_length_limit` = 6 for small tables, 14 for large.
static int getIndex(int indexLength,
                    const uint8_t*& src, size_t& remaining, int& current, int& bits,
                    int indexLengthLimit)
{
    int count = indexLength - 1;
    if (count == 0)
        return getBits(1, src, remaining, current, bits);
    if (count < indexLengthLimit)
        return (1 << count) | getBits(count, src, remaining, current, bits);
    while (getBits(1, src, remaining, current, bits) != 0) {
        if (count >= 0x10)
            throw std::runtime_error("Invalid index count in cg_decompress");
        ++count;
    }
    return (1 << count) | getBits(count, src, remaining, current, bits);
}

// ---- Single/paired channel decompressor ----
// `card` and index thresholds are determined internally from table_size.

void cg_decompress(
    std::vector<uint8_t>& output,
    size_t outputOffset,
    size_t outputShift,
    const uint8_t*& src,
    size_t inputShift,
    int /*card*/,
    std::vector<uint8_t>& m_index,
    const uint8_t* end)
{
    if ((inputShift != 1 && inputShift != 2) || outputShift < inputShift ||
        outputOffset >= outputShift || inputShift > outputShift - outputOffset ||
        output.size() % outputShift != 0)
        throw std::runtime_error("Invalid output layout in cg_decompress");
    if (src > end || static_cast<size_t>(end - src) < 12)
        throw std::runtime_error("Truncated block header in cg_decompress");
    const uint32_t originalSize = readU32(src);
    size_t remaining = readU32(src); // size_comp, including block padding
    if (originalSize != output.size() / outputShift * inputShift)
        throw std::runtime_error("Invalid image size in cg_decompress");

    int indexCount = static_cast<int>(readU16(src));
    int indexSize  = indexCount * static_cast<int>(inputShift);
    readU16(src); // skip 2 bytes (junk in WCG, indexed in LIM)

    if (indexCount == 0)
        throw std::runtime_error("Empty palette in cg_decompress");
    const size_t available = static_cast<size_t>(end - src);
    if (static_cast<size_t>(indexSize) > available ||
        remaining > available - static_cast<size_t>(indexSize))
        throw std::runtime_error("Truncated palette or compressed block in cg_decompress");
    if (static_cast<size_t>(indexSize) > m_index.size())
        m_index.resize(indexSize);
    for (int i = 0; i < indexSize; ++i)
        m_index[i] = *src++;
    const uint8_t* nextBlock = src + remaining;

    // Cannonball.exe 0x4404dd uses unsigned JA: 4096 still uses 3-bit coding.
    bool small = (inputShift == 1 || indexCount <= 0x1000);
    int indexBitLength  = small ? 3 : 4;   // unk2 / index_bit_length
    int indexLengthLimit = small ? 6 : 14;  // unk1 / m_index_length_limit

    int current = 0, bits = 0;
    size_t dst = outputOffset;
    size_t pixelsRemaining = output.size() / outputShift;

    while (pixelsRemaining > 0) {
        int seqLen = 1;
        int len = getBits(indexBitLength, src, remaining, current, bits);
        if (len == 0) {
            seqLen = getBits(4, src, remaining, current, bits) + 2;
            len = getBits(indexBitLength, src, remaining, current, bits);
        }
        if (len == 0)
            throw std::runtime_error("Invalid index length in cg_decompress");

        int idx = getIndex(len, src, remaining, current, bits, indexLengthLimit);
        if (idx < 0 || idx >= indexCount)
            throw std::runtime_error("Palette index out of range in cg_decompress");
        if (static_cast<size_t>(seqLen) > pixelsRemaining)
            throw std::runtime_error("Pixel run exceeds image size in cg_decompress");
        pixelsRemaining -= seqLen;
        for (int k = 0; k < seqLen; ++k) {
            std::copy_n(m_index.data() + idx * inputShift, inputShift,
                        output.data() + dst);
            dst += outputShift;
        }
    }
    // Finishing the pixels may leave unused bytes (e.g. Khime's one-byte pad).
    // The next channel starts at the declared end, not the last byte read.
    src = nextBlock;
}

void cg_decompress_16bpp(
    std::vector<uint8_t>& output,
    size_t outputSize,
    const uint8_t*& src,
    int card,
    std::vector<uint8_t>& m_index,
    const uint8_t* end)
{
    if (outputSize != output.size())
        throw std::runtime_error("Invalid 16bpp output size in cg_decompress");
    cg_decompress(output, 0, 2, src, 2, card, m_index, end);
}

} // namespace liarsoft
