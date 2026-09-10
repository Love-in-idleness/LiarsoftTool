#ifndef LIARSOFTTOOL_GSC_DECOMPILER_H
#define LIARSOFTTOOL_GSC_DECOMPILER_H

#include <cstdint>
#include <string>
#include <vector>

namespace liarsoft {

/// Produce an annotated, UTF-8 TSC listing from a CodeX GSC file.
/// Verified commands are emitted as TSC while unknown semantics remain
/// offset-annotated comments.
std::string decompileGsc(const std::string& inputPath,
                         const std::string& encoding = "CP932");

void decompileGscToFile(const std::string& inputPath,
                        const std::string& outputPath,
                        const std::string& encoding = "CP932");

/// Restore the source GSC embedded by decompileGsc(). Unchanged input is exact;
/// edited TXT/TXA dialogue lines rebuild the modern GSC string table, and
/// edited operands of known fixed-size commands patch the original code.
std::vector<uint8_t> restoreGscFromTsc(
    const std::string& tscText,
    const std::string& fallbackEncoding = "CP932");

void restoreGscFromTscFile(const std::string& inputPath,
                           const std::string& outputPath,
                           const std::string& fallbackEncoding = "CP932");

} // namespace liarsoft

#endif
