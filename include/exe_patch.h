#ifndef LIARSOFTTOOL_EXE_PATCH_H
#define LIARSOFTTOOL_EXE_PATCH_H

#include <string>
#include <vector>
#include <cstdint>

namespace liarsoft {

/// Convert recognized RScript EXE font charset operands.
/// @param data   Raw EXE bytes.
/// @param fromByte  Source Win32 font charset value in the EXE.
/// @param toByte    Target Win32 font charset value.
std::vector<uint8_t> exeConvertEncoding(const std::vector<uint8_t>& data,
                                        uint8_t fromByte, uint8_t toByte);

/// Convert a file. `encoding` is "CP932", "GBK", or "CP1251".
/// CP1251 provides correct rendering for Cyrillic and English scripts.
/// All recognized charset operands are normalized to the requested target.
/// Throws if the EXE contains no supported RScript charset pattern.
void exeConvertFile(const std::string& inputPath, const std::string& outputPath,
                    const std::string& encoding);

} // namespace liarsoft

#endif
