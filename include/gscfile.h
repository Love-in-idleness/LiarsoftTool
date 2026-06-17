#ifndef LIARSOFTTOOL_GSCFILE_H
#define LIARSOFTTOOL_GSCFILE_H

#include "bigendian.h"
#include <cstdint>
#include <vector>
#include <string>
#include <fstream>

namespace liarsoft {

/**
 * Represents a GSC (Game Script) file used by Liar-soft visual novels.
 *
 * GSC binary layout (all integers big-endian):
 *   HEADER (9 x int32):
 *     [0] FileLength
 *     [1] HeaderLength
 *     [2] CommandLength
 *     [3] StringDeclarationLength
 *     [4] StringDefinitionLength
 *     [5..8] Unknown1..4
 *
 *   CommandSection: raw bytes of length CommandLength
 *
 *   StringDeclaration:
 *     - 8 bytes padding
 *     - (StringDeclarationLength / 4 - 2) x int32 — cumulative string offsets (including \0)
 *     - 1 byte (0x00)
 *     - Strings: each null-terminated in the given encoding
 *
 *   EndSequence: remaining bytes after strings
 */
class GscFile {
public:
    /// The encoding identifier used internally (e.g. "SHIFT_JIS", "GBK").
    /// This is the encoding of the *strings* inside the GSC file.
    std::string encoding;

    /// Full file length (patched on save).
    int32_t fileLength = 0;
    int32_t headerLength = 0;
    int32_t commandLength = 0;
    int32_t stringDeclarationLength = 0;
    int32_t stringDefinitionLength = 0;
    int32_t unknown1 = 0;
    int32_t unknown2 = 0;
    int32_t unknown3 = 0;
    int32_t unknown4 = 0;

    /// Raw command section bytes.
    std::vector<uint8_t> commandSection;

    /// String offsets (cumulative, from start of string data; 1-based).
    std::vector<int32_t> stringLengths;

    /// Extracted strings (in the encoding specified by `encoding`).
    /// Before calling save(), set these to the desired content.
    std::vector<std::string> strings;

    /// Trailing bytes after the string section.
    std::vector<uint8_t> endSequence;

    // ---- Factory methods ----

    /// Read a GSC file from disk. `enc` is the encoding of the strings (e.g. "SHIFT_JIS", "GBK").
    static GscFile fromFile(const std::string& path, const std::string& enc);

    /// Read a GSC file from an in-memory buffer.
    static GscFile fromBytes(const std::vector<uint8_t>& bytes, const std::string& enc);

    /// Read a GSC file from a raw file stream.
    static GscFile fromStream(std::ifstream& stream, const std::string& enc);

    // ---- Save ----

    /// Save to a file. The FileLength field is automatically patched.
    void save(const std::string& path) const;

    /// Save to an in-memory buffer.
    std::vector<uint8_t> toBytes() const;

private:
    GscFile() = default;

    /// Calculate the actual string definition length (sum of encoded byte counts + null terminators).
    int32_t calcStringDefinitionLength() const;
};

} // namespace liarsoft

#endif // LIARSOFTTOOL_GSCFILE_H
