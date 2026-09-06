#ifndef LIARSOFTTOOL_XFLARCHIVE_H
#define LIARSOFTTOOL_XFLARCHIVE_H

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>

namespace liarsoft {

/// Whether a regular file may be included while packing a directory.
/// Supported extensions are matched case-insensitively.
bool isPackableFile(const std::string& path);

/// Whether a directory contains a .meta.xml file (case-insensitive).
bool isLwgDirectory(const std::string& path);

/// Whether an input belongs to the selected packing/unpacking direction.
/// Enabling both filters intentionally matches no input.
bool matchesOperationMode(const std::string& path, bool packOnly,
                          bool unpackOnly, bool recursive = false);

/// Pack `dirPath`. With `recursive`, editable files are converted first and
/// subdirectories are packed from the deepest level upward.
/// Recoverable per-file failures are returned as warnings.
std::vector<std::string> packDirectoryToFile(
    const std::string& dirPath, const std::string& outputPath,
    const std::string& encoding, bool recursive = false);

/// Recursively unpack nested XFL/LWG files and convert extracted resources to
/// editable formats. Recoverable failures are returned as warnings.
std::vector<std::string> unpackDirectoryRecursively(
    const std::string& dirPath, const std::string& encoding,
    bool gscToTsc = false);

/// Represents a single file entry inside an XFL archive.
struct XflEntry {
    std::string fileName;  ///< File name (up to 31 chars in legacy encoding).
    std::vector<uint8_t> data;  ///< Raw file content.
};

/**
 * XFL archive format used by Liar-soft visual novels.
 *
 * Binary layout (all integers little-endian):
 *   Header:
 *     Magic   : uint32 = 0x0001424C ("LB\x01\x00")
 *     TableSize : uint32 = fileCount * 40 (size of chunk table in bytes)
 *     FileCount : uint32
 *
 *   Chunk table (FileCount entries of 40 bytes each):
 *     FileName[0x20] : fixed-width name (encoded in `encoding`, null-padded)
 *     Offset         : uint32, offset from start of data section
 *     Size           : uint32, file content size
 *
 *   Data section:
 *     Concatenated raw file bytes.
 */
class XflArchive {
public:
    /// Encoding used for file names inside the archive (e.g. "CP932", "GBK").
    std::string encoding;

    /// List of entries.
    std::vector<XflEntry> entries;

    /// Create an empty archive.
    XflArchive() = default;

    // ---- Factory methods ----

    /// Read an XFL archive from disk.
    static XflArchive fromFile(const std::string& path, const std::string& enc = "CP932");

    /// Read an XFL archive from an in-memory buffer.
    static XflArchive fromBytes(const std::vector<uint8_t>& data, const std::string& enc = "CP932");

    // ---- Pack ----

    /// Add supported resource files from a directory (flat, non-recursive).
    /// Files are sorted naturally (human sort).
    void addDirectory(const std::string& dirPath);

    /// Save to a file.
    void save(const std::string& path) const;

    /// Save to an in-memory buffer.
    std::vector<uint8_t> toBytes() const;

    // ---- Unpack ----

    /// Extract all entries to a directory.
    void extractToDirectory(const std::string& dirPath) const;

private:
    static void createDirectory(const std::string& path);
};

} // namespace liarsoft

#endif // LIARSOFTTOOL_XFLARCHIVE_H
