#ifndef LIARSOFTTOOL_EXE_PATCH_H
#define LIARSOFTTOOL_EXE_PATCH_H

#include <string>
#include <vector>
#include <cstdint>

namespace liarsoft {

/// Convert EXE between encoding modes (SJIS=0x80, GBK=0x86, CP1251=0xCC).
/// @param data   Raw EXE bytes.
/// @param fromByte  Source code-page byte in the EXE.
/// @param toByte    Target code-page byte.
std::vector<uint8_t> exeConvertEncoding(const std::vector<uint8_t>& data,
                                        uint8_t fromByte, uint8_t toByte);

/// Convert a file. `encoding` is "SHIFT_JIS", "GBK", or "CP1251".
/// CP1251 provides correct rendering for Cyrillic and English scripts.
/// If encoding is SHIFT_JIS, auto-detect source and convert back to 0x80.
void exeConvertFile(const std::string& inputPath, const std::string& outputPath,
                    const std::string& encoding);

} // namespace liarsoft

#endif
