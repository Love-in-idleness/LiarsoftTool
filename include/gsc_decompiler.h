#ifndef LIARSOFTTOOL_GSC_DECOMPILER_H
#define LIARSOFTTOOL_GSC_DECOMPILER_H

#include <string>

namespace liarsoft {

/// Produce an annotated, UTF-8 TSC listing from a CodeX GSC file.
/// This is experimental: verified commands are emitted as TSC while unknown
/// semantics remain offset-annotated comments.
std::string decompileGsc(const std::string& inputPath,
                         const std::string& encoding = "CP932");

void decompileGscToFile(const std::string& inputPath,
                        const std::string& outputPath,
                        const std::string& encoding = "CP932");

} // namespace liarsoft

#endif
