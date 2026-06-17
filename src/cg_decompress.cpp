#include "cg_decompress.h"
#include <stdexcept>

namespace liarsoft {

static inline uint16_t readU16(const uint8_t*& p) {
    uint16_t v = p[0] | (p[1]<<8); p += 2; return v;
}
static inline uint32_t readU32(const uint8_t*& p) {
    uint32_t v = p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24); p += 4; return v;
}

// MSB-first bit reader
static int getBits(int n, const uint8_t*& src, int& remaining, int& current, int& bits) {
    int v = 0;
    while (n > 0) {
        if (current == 0) {
            if (remaining == 0) return -1;
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
                    const uint8_t*& src, int& remaining, int& current, int& bits,
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
    std::vector<uint8_t>& m_index)
{
    readU32(src); // size_orig
    int remaining = static_cast<int>(readU32(src)); // size_comp

    int indexCount = static_cast<int>(readU16(src));
    int indexSize  = indexCount * static_cast<int>(inputShift);
    if (static_cast<size_t>(indexSize) > m_index.size())
        m_index.resize(indexSize);

    readU16(src); // skip 2 bytes (junk in WCG, indexed in LIM)

    for (int i = 0; i < indexSize; ++i)
        m_index[i] = *src++;

    // Determine thresholds from actual table size (matching arc_unpacker / GARbro)
    bool small = (indexCount < 0x1002);
    int indexBitLength  = small ? 3 : 4;   // unk2 / index_bit_length
    int indexLengthLimit = small ? 6 : 14;  // unk1 / m_index_length_limit

    int current = 0, bits = 0;
    size_t dst = outputOffset;

    while (dst < output.size()) {
        int seqLen = 1;
        int len = getBits(indexBitLength, src, remaining, current, bits);
        if (len == -1) break;

        if (len == 0) {
            seqLen = getBits(4, src, remaining, current, bits) + 2;
            if (seqLen == -1 + 2) break; // getBits returned -1
            len = getBits(indexBitLength, src, remaining, current, bits);
        }
        if (len == 0 || len == -1) break;

        int idx = getIndex(len, src, remaining, current, bits, indexLengthLimit);
        if (idx < 0) break;

        if (inputShift == 1) {
            for (int k = 0; k < seqLen; ++k) {
                if (dst >= output.size()) break;
                output[dst] = m_index[idx];
                dst += outputShift;
                if (dst >= output.size()) break;
            }
        } else { // inputShift == 2
            int base = idx * 2;
            for (int k = 0; k < seqLen; ++k) {
                if (dst + 1 >= output.size()) break;
                output[dst]   = m_index[base];
                output[dst+1] = m_index[base + 1];
                dst += outputShift;
                if (dst >= output.size()) break;
            }
        }
    }
}

void cg_decompress_16bpp(
    std::vector<uint8_t>& output,
    size_t outputSize,
    const uint8_t*& src,
    int /*card*/,
    std::vector<uint8_t>& m_index)
{
    readU32(src); // imageSize
    int remaining = static_cast<int>(readU32(src));

    int indexCount = static_cast<int>(readU16(src));
    int indexSize  = indexCount * 2;
    if (static_cast<size_t>(indexSize) > m_index.size())
        m_index.resize(indexSize);

    readU16(src); // skip

    for (int i = 0; i < indexSize; ++i)
        m_index[i] = *src++;

    bool small = (indexSize < 0x1002 * 2); // 0x2004 bytes, i.e. indexCount < 0x1002
    int indexBitLength  = small ? 3 : 4;
    int indexLengthLimit = small ? 6 : 14;

    int current = 0, bits = 0;
    size_t dst = 0;

    while (dst < outputSize) {
        int seqLen = 1;
        int len = getBits(indexBitLength, src, remaining, current, bits);
        if (len == -1) break;

        if (len == 0) {
            seqLen = getBits(4, src, remaining, current, bits) + 2;
            if (seqLen == -1 + 2) break;
            len = getBits(indexBitLength, src, remaining, current, bits);
        }
        if (len == 0 || len == -1) break;

        int idx = getIndex(len, src, remaining, current, bits, indexLengthLimit);
        if (idx < 0) break;

        int base = idx * 2;
        for (int k = 0; k < seqLen; ++k) {
            if (dst + 1 >= outputSize) break;
            output[dst++] = m_index[base];
            output[dst++] = m_index[base + 1];
        }
    }
}

} // namespace liarsoft
