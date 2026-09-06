#ifndef LIARSOFTTOOL_GSC_DECOMPILER_H
#define LIARSOFTTOOL_GSC_DECOMPILER_H

#include <cstdint>
#include <string>
#include <vector>

namespace liarsoft {

/// Produce an annotated, UTF-8 TSC listing from a CodeX GSC file.
/// This is experimental: verified commands are emitted as TSC while unknown
/// semantics remain offset-annotated comments.
std::string decompileGsc(const std::string& inputPath,
                         const std::string& encoding = "CP932");

void decompileGscToFile(const std::string& inputPath,
                        const std::string& outputPath,
                        const std::string& encoding = "CP932");

/// Restore the exact source GSC embedded by decompileGsc(). Other TSC text and
/// ordinary comments are intentionally ignored in this first-stage round trip.
std::vector<uint8_t> restoreGscFromTsc(const std::string& tscText);

void restoreGscFromTscFile(const std::string& inputPath,
                           const std::string& outputPath);

} // namespace liarsoft

#endif
