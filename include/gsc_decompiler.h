#ifndef LIARSOFTTOOL_GSC_DECOMPILER_H
#define LIARSOFTTOOL_GSC_DECOMPILER_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace liarsoft {

/// Guess from validated GSC string sections only. Throws if inconclusive;
/// guesses and indistinguishable encodings are reported through warnings.
std::string detectGscEncoding(const std::vector<uint8_t>& data,
                             std::vector<std::string>* warnings = nullptr);

/// Produce an annotated, UTF-8 TSC listing from a CodeX GSC file.
/// Verified commands are emitted as TSC while unknown semantics remain
/// offset-annotated comments.
std::string decompileGsc(const std::string& inputPath,
                         const std::string& encoding = "CP932",
                         std::vector<std::string>* warnings = nullptr);

/// Read-only, in-memory counterpart; sourceName is used only in diagnostics/listing comments.
std::string decompileGscBytes(const std::vector<uint8_t>& data,
                         const std::string& encoding = "CP932",
                         std::vector<std::string>* warnings = nullptr,
                         const std::string& sourceName = "memory.gsc");

/// Return warnings for recovered noncanonical input after writing the listing.
std::vector<std::string> decompileGscToFile(const std::string& inputPath,
                        const std::string& outputPath,
                        const std::string& encoding = "CP932");

/// Compile structured TSC, rebuilding sections, strings and lengths; raw
/// fallback listings restore their embedded GSC unchanged.
std::vector<uint8_t> restoreGscFromTsc(
    const std::string& tscText,
    const std::string& fallbackEncoding = "CP932");

struct TscDiagnostic {
    std::string severity;
    std::string message;
    size_t line = 0; // One-based source line; zero means a whole-file diagnostic.
};

struct TscToken {
    std::string value, role;
    bool quoted = false;
    size_t start = 0, end = 0; // Half-open UTF-8 byte offsets in the source.
};
struct TscLine {
    std::string kind;
    size_t line = 0, start = 0, end = 0;
    int opcode = -1;
    std::vector<TscToken> tokens;
};
struct TscDocument {
    std::string byteFormat, schema;
    std::vector<TscLine> lines;
    std::vector<TscDiagnostic> diagnostics;
};

/// Compiler-backed syntax inspection, without target-encoding restrictions or writes.
/// Invalid documents return partial records plus a diagnostic; editors must keep the source.
TscDocument inspectTsc(const std::string& text);

/// Run the same in-memory compiler without writing a GSC. Reports its first error.
std::vector<TscDiagnostic> checkTsc(const std::string& tscText,
                                  const std::string& encoding = "CP932");

void restoreGscFromTscFile(const std::string& inputPath,
                           const std::string& outputPath,
                           const std::string& fallbackEncoding = "CP932");

} // namespace liarsoft

#endif
