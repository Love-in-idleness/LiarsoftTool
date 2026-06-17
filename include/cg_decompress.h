#ifndef LIARSOFTTOOL_CG_DECOMPRESS_H
#define LIARSOFTTOOL_CG_DECOMPRESS_H

#include <cstdint>
#include <vector>
#include <cstddef>

namespace liarsoft {

/**
 * CG decompression — used by both WCG and LIM formats.
 *
 * Reads compressed data from `src` (pointer which ADVANCES past consumed bytes)
 * and writes decompressed pixels into `output`.
 *
 * This implements the same algorithm as:
 *   - WcgImage.Decompress (C#) — WCG paired-channel decompressor
 *   - LimDecoder.UnpackChannel (C#) — LIM single-channel decompressor
 *
 * @param output       Destination buffer.
 * @param outputOffset Starting offset in output (for interleaving).
 * @param outputShift  Stride between consecutive writes in output.
 * @param src          Pointer into compressed data; ADVANCES past consumed bytes.
 * @param inputShift   Bytes per palette entry (1 for LIM 32bpp channels, 2 for WCG/BGR565).
 * @param card         Bits for initial table-offset-size (3 for tables < 0x1000, 4 for >= 0x1000).
 * @param m_index      Reusable index/palette buffer (grown as needed).
 */
void cg_decompress(
    std::vector<uint8_t>& output,
    size_t outputOffset,
    size_t outputShift,
    const uint8_t*& src,
    size_t inputShift,
    int card,
    std::vector<uint8_t>& m_index);

/**
 * Decompress a 16bpp BGR565 block (used by LIM 16bpp).
 * `src` advances past consumed bytes.
 */
void cg_decompress_16bpp(
    std::vector<uint8_t>& output,
    size_t outputSize,
    const uint8_t*& src,
    int card,
    std::vector<uint8_t>& m_index);

} // namespace liarsoft

#endif // LIARSOFTTOOL_CG_DECOMPRESS_H
