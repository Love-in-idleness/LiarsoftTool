#ifndef LIARSOFTTOOL_EXE_PATCH_H
#define LIARSOFTTOOL_EXE_PATCH_H

#include <string>
#include <vector>
#include <cstdint>

namespace liarsoft {

/// Convert EXE between Shift-JIS and GBK encoding modes.
/// @param data      Raw EXE bytes.
/// @param toGBK     true = SJIS→GBK (68 80→68 86), false = GBK→SJIS (68 86→68 80).
/// @return          Patched EXE bytes.
std::vector<uint8_t> exeConvertEncoding(const std::vector<uint8_t>& data, bool toGBK);

/// Convert a file and save to output path.
/// @param encoding  "SHIFT_JIS" or "GBK" — if GBK, convert SJIS→GBK; if SHIFT_JIS, convert GBK→SJIS.
void exeConvertFile(const std::string& inputPath, const std::string& outputPath,
                    const std::string& encoding);

} // namespace liarsoft

#endif
