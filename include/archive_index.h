#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace liarsoft {
// A checked range in a file; nested archives keep referring to the original file.
class ResourceSource {
public:
    explicit ResourceSource(const std::filesystem::path& file);
    uint64_t size() const { return size_; }
    ResourceSource subrange(uint64_t offset, uint64_t size) const;
    std::vector<uint8_t> read(uint64_t offset, size_t size) const;
    std::vector<uint8_t> readAll() const;
private:
    std::filesystem::path file_;
    uint64_t offset_ = 0, size_ = 0;
};

struct ArchiveEntryInfo {
    std::string name; // UTF-8; entries are identified by index, not by name.
    uint64_t offset = 0, size = 0;
    int32_t x = 0, y = 0;
    uint8_t flag = 0;
};
struct ArchiveIndex {
    std::string format;
    uint32_t width = 0, height = 0;
    std::vector<ArchiveEntryInfo> entries;
};

// File access reads only the header and index, never the entry payloads.
ArchiveIndex readArchiveIndex(const ResourceSource& source, const std::string& encoding = "CP932");
ArchiveIndex readArchiveIndex(const std::vector<uint8_t>& data, const std::string& encoding = "CP932");
}
