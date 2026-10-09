#ifndef LIARSOFTTOOL_LWG_DECODER_H
#define LIARSOFTTOOL_LWG_DECODER_H

#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace liarsoft {

/**
 * LWG archive format used by Liar-soft visual novels.
 *
 * Binary layout (all integers little-endian):
 *   Magic: "LG\x01\x00" (4 bytes)
 *   Height: u32  (NOTE: Height BEFORE Width!)
 *   Width: u32
 *   FileCount: u32
 *   Unknown: u32 (zeros)
 *   TableSize: u32
 *
 *   For each entry (table):
 *     X: i32 (4 bytes)
 *     Y: i32 (4 bytes)
 *     Flag: u8 (1 byte)
 *     Offset: u32 (from start of data section)
 *     Size: u32
 *     NameLen: u8
 *     Name: NameLen bytes (encoded in specified encoding)
 *
 *   DataSize: u32 (total raw payload length, 4 bytes)
 *   Data section: concatenated raw file bytes
 */
struct LwgEntry {
    std::string name;           // UTF-8 filename
    std::vector<uint8_t> data;  // raw file content
    int32_t x = 0;
    int32_t y = 0;
    uint8_t flag = 0;
    uint32_t size = 0;          // original size (for repacking)
};

class LwgDecoder {
public:
    static constexpr const char* MAGIC = "LG\x01\x00";
    static constexpr size_t MAGIC_SIZE = 4;

    static bool isRecognized(const std::vector<uint8_t>& data);

    struct Archive {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<LwgEntry> entries;
    };

    /// Decode LWG archive. `encoding` is the filename encoding.
    static Archive decode(const std::vector<uint8_t>& data, const std::string& encoding);

    /// Extract non-empty payloads and write .meta.xml for every entry.
    /// Zero-data entries use empty="1" so repacking preserves them even when
    /// another image entry has the same name.
    /// Payloads and .meta.xml inherit sourceTime when supplied.
    static void extractToDirectory(const Archive& archive, const std::string& dirPath,
                                   const std::string& encoding,
                                   std::optional<std::filesystem::file_time_type> sourceTime = {});
};

/// Pack a directory (with .meta.xml) into an LWG archive.
class LwgPacker {
public:
    /// Pack a directory into LWG binary data.
    /// The directory must contain a .meta.xml file.
    /// `encoding` is the filename encoding for the archive.
    static std::vector<uint8_t> pack(
        const std::string& dirPath, const std::string& encoding,
        const std::set<std::string>& excludedPaths = {});

    /// Pack and save to file.
    static void packToFile(const std::string& dirPath, const std::string& outputPath,
                           const std::string& encoding);
};

} // namespace liarsoft

#endif // LIARSOFTTOOL_LWG_DECODER_H
