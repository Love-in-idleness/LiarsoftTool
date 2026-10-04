#ifndef LIARSOFTTOOL_CG_DECOMPRESS_H
#define LIARSOFTTOOL_CG_DECOMPRESS_H

#include <cstdint>
#include <vector>
#include <cstddef>

namespace liarsoft {

/**
 * CG decompression — used by both WCG and LIM formats.
 *
 * Reads compressed data from `src` (pointer which ADVANCES past the full block,
 * including any padding within the declared compressed size)
 * and writes decompressed pixels into `output`.
 *
 * This implements the same algorithm as:
 *   - WcgImage.Decompress (C#) — WCG paired-channel decompressor
 *   - LimDecoder.UnpackChannel (C#) — LIM single-channel decompressor
 *
 * @param output       Destination buffer.
 * @param outputOffset Starting offset in output (for interleaving).
 * @param outputShift  Stride between consecutive writes in output.
 * @param src          Pointer into compressed data; ADVANCES to the next block.
 * @param inputShift   Bytes per palette entry (1 for LIM 32bpp channels, 2 for WCG/BGR565).
 * @param card         Legacy parameter; coding is inferred from the palette size.
 * @param m_index      Reusable index/palette buffer (grown as needed).
 * @param end          End of the source buffer (exclusive).
 */
void cg_decompress(
    std::vector<uint8_t>& output,
    size_t outputOffset,
    size_t outputShift,
    const uint8_t*& src,
    size_t inputShift,
    int card,
    std::vector<uint8_t>& m_index,
    const uint8_t* end);

/**
 * Decompress a 16bpp BGR565 block (used by LIM 16bpp).
 * `src` advances past the full declared block, including padding.
 */
void cg_decompress_16bpp(
    std::vector<uint8_t>& output,
    size_t outputSize,
    const uint8_t*& src,
    int card,
    std::vector<uint8_t>& m_index,
    const uint8_t* end);

} // namespace liarsoft

#endif // LIARSOFTTOOL_CG_DECOMPRESS_H
