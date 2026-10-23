#include "archive_index.h"
#include "encoding.h"
#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace liarsoft {
ResourceSource::ResourceSource(const std::filesystem::path& file) : file_(std::filesystem::absolute(file)) {
    if (!std::filesystem::is_regular_file(file)) throw std::runtime_error("Resource is not a regular file");
    size_ = std::filesystem::file_size(file);
    if (size_ > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max()))
        throw std::runtime_error("Resource exceeds the stream offset range");
}
ResourceSource ResourceSource::subrange(uint64_t offset, uint64_t size) const {
    if (offset > size_ || size > size_ - offset) throw std::runtime_error("Resource range is out of bounds");
    auto result = *this;
    result.offset_ += offset; result.size_ = size;
    return result;
}
std::vector<uint8_t> ResourceSource::read(uint64_t offset, size_t size) const {
    if (offset > size_ || size > size_ - offset ||
        size > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
        throw std::runtime_error("Resource read is out of bounds");
    std::ifstream stream(file_, std::ios::binary);
    if (!stream || !stream.seekg(static_cast<std::streamoff>(offset_ + offset)))
        throw std::runtime_error("Cannot open or seek resource");
    std::vector<uint8_t> bytes(size);
    if (size && !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
        throw std::runtime_error("Resource changed or was truncated during reading");
    return bytes;
}
std::vector<uint8_t> ResourceSource::readAll() const {
    if (size_ > std::numeric_limits<size_t>::max()) throw std::runtime_error("Resource exceeds addressable memory");
    return read(0, static_cast<size_t>(size_));
}

namespace {
uint32_t u32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset > data.size() || data.size() - offset < 4) throw std::runtime_error("Truncated archive field");
    return uint32_t(data[offset]) | (uint32_t(data[offset + 1]) << 8) |
        (uint32_t(data[offset + 2]) << 16) | (uint32_t(data[offset + 3]) << 24);
}
bool xfl(const std::vector<uint8_t>& header) {
    const auto magic = u32(header, 0);
    if (magic == 0x0001424c) return true;
    if (magic == 0x0001474c) return false;
    throw std::runtime_error("Not an XFL or LWG archive");
}
size_t tableLength(const std::vector<uint8_t>& header, uint64_t total) {
    const bool isXfl = xfl(header);
    const uint32_t length = u32(header, isXfl ? 4 : 20);
    const uint64_t start = isXfl ? 12 : 28;
    if (total < start || length > total - start || (isXfl && length > INT32_MAX))
        throw std::runtime_error("Invalid archive table size");
    return length;
}
ArchiveIndex parse(const std::vector<uint8_t>& header, const std::vector<uint8_t>& table,
                   uint64_t total, const std::string& encoding) {
    const bool isXfl = xfl(header);
    const uint32_t count = u32(header, isXfl ? 8 : 12);
    if (table.size() != tableLength(header, total) || count > table.size() / (isXfl ? 40 : 18))
        throw std::runtime_error("Invalid archive entry count");
    const uint64_t dataStart = uint64_t(isXfl ? 12 : 28) + table.size();
    ArchiveIndex result;
    result.format = isXfl ? "XFL" : "LWG";
    if (!isXfl) { result.height = u32(header, 4); result.width = u32(header, 8); }
    size_t pos = 0;
    for (uint32_t i = 0; i < count; ++i) {
        ArchiveEntryInfo entry;
        if (pos > table.size() || table.size() - pos < (isXfl ? 40 : 18))
            throw std::runtime_error("Truncated archive entry");
        if (isXfl) {
            const auto begin = table.begin() + pos;
            const auto end = std::find(begin, begin + 32, 0);
            if (end == begin + 32) throw std::runtime_error("XFL filename is not null-terminated");
            entry.name = convertEncoding(std::string(begin, end), encoding, "UTF-8");
            entry.offset = u32(table, pos + 32); entry.size = u32(table, pos + 36);
            if (entry.offset > INT32_MAX || entry.size > INT32_MAX)
                throw std::runtime_error("Negative XFL entry offset or size");
            pos += 40;
        } else {
            entry.x = static_cast<int32_t>(u32(table, pos));
            entry.y = static_cast<int32_t>(u32(table, pos + 4));
            entry.flag = table[pos + 8];
            entry.offset = u32(table, pos + 9); entry.size = u32(table, pos + 13);
            const size_t length = table[pos + 17]; pos += 18;
            if (length > table.size() - pos) throw std::runtime_error("LWG filename extends past the table");
            entry.name = convertEncoding(std::string(table.begin() + pos, table.begin() + pos + length), encoding, "UTF-8");
            pos += length;
        }
        if (entry.offset > total - dataStart || entry.size > total - dataStart - entry.offset)
            throw std::runtime_error("Archive entry is out of bounds: " + entry.name);
        entry.offset += dataStart;
        result.entries.push_back(std::move(entry));
    }
    return result;
}
}
ArchiveIndex readArchiveIndex(const ResourceSource& source, const std::string& encoding) {
    const bool isXfl = xfl(source.read(0, 4));
    const size_t headerSize = isXfl ? 12 : 24;
    const auto header = source.read(0, headerSize);
    return parse(header, source.read(headerSize, tableLength(header, source.size())), source.size(), encoding);
}
ArchiveIndex readArchiveIndex(const std::vector<uint8_t>& data, const std::string& encoding) {
    const size_t headerSize = xfl(data) ? 12 : 24;
    const auto length = tableLength(data, data.size());
    return parse(data, std::vector<uint8_t>(data.begin() + headerSize, data.begin() + headerSize + length), data.size(), encoding);
}
}
