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
                         const std::string& encoding = "CP932",
                         std::vector<std::string>* warnings = nullptr);

/// Return warnings for recovered noncanonical input after writing the listing.
std::vector<std::string> decompileGscToFile(const std::string& inputPath,
                        const std::string& outputPath,
                        const std::string& encoding = "CP932");

/// Compile structured TSC, rebuilding sections, strings and lengths; raw
/// fallback listings restore their embedded GSC unchanged.
std::vector<uint8_t> restoreGscFromTsc(
    const std::string& tscText,
    const std::string& fallbackEncoding = "CP932");

void restoreGscFromTscFile(const std::string& inputPath,
                           const std::string& outputPath,
                           const std::string& fallbackEncoding = "CP932");

} // namespace liarsoft

#endif
