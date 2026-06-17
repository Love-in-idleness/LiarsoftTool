#ifndef LIARSOFTTOOL_BIGENDIAN_H
#define LIARSOFTTOOL_BIGENDIAN_H

#include <cstdint>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace liarsoft {

/// Read a little-endian int32 from a byte buffer at the given offset.
/// (Despite the "BE" suffix, GSC files use little-endian byte order.)
inline int32_t read_int32_be(const uint8_t* buf, size_t offset) {
    return (static_cast<int32_t>(buf[offset])) |
           (static_cast<int32_t>(buf[offset + 1]) << 8) |
           (static_cast<int32_t>(buf[offset + 2]) << 16) |
           (static_cast<int32_t>(buf[offset + 3]) << 24);
}

/// Write a little-endian int32 to a byte buffer at the given offset.
inline void write_int32_be(uint8_t* buf, size_t offset, int32_t value) {
    buf[offset]     = static_cast<uint8_t>(value & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    buf[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buf[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

/// Simple big-endian binary reader over an in-memory buffer.
class BigEndianReader {
public:
    explicit BigEndianReader(const std::vector<uint8_t>& data)
        : data_(data), pos_(0) {}

    int32_t readInt32() {
        if (pos_ + 4 > data_.size())
            throw std::runtime_error("BigEndianReader: unexpected EOF");
        int32_t v = read_int32_be(data_.data(), pos_);
        pos_ += 4;
        return v;
    }

    std::vector<uint8_t> readBytes(size_t count) {
        if (pos_ + count > data_.size())
            throw std::runtime_error("BigEndianReader: unexpected EOF");
        auto begin = data_.begin() + static_cast<ptrdiff_t>(pos_);
        auto end = begin + static_cast<ptrdiff_t>(count);
        pos_ += count;
        return std::vector<uint8_t>(begin, end);
    }

    uint8_t readByte() {
        if (pos_ >= data_.size())
            throw std::runtime_error("BigEndianReader: unexpected EOF");
        return data_[pos_++];
    }

    size_t position() const { return pos_; }
    size_t remaining() const { return data_.size() - pos_; }

private:
    const std::vector<uint8_t>& data_;
    size_t pos_;
};

/// Simple big-endian binary writer into an in-memory buffer.
class BigEndianWriter {
public:
    BigEndianWriter() = default;

    void writeInt32(int32_t value) {
        uint8_t buf[4];
        write_int32_be(buf, 0, value);
        data_.insert(data_.end(), buf, buf + 4);
    }

    void writeBytes(const std::vector<uint8_t>& bytes) {
        data_.insert(data_.end(), bytes.begin(), bytes.end());
    }

    void writeBytes(const uint8_t* bytes, size_t count) {
        data_.insert(data_.end(), bytes, bytes + count);
    }

    void writeByte(uint8_t value) {
        data_.push_back(value);
    }

    size_t position() const { return data_.size(); }

    void seek(size_t pos) {
        if (pos > data_.size())
            data_.resize(pos, 0);
    }

    const std::vector<uint8_t>& data() const { return data_; }

    /// Replace int32 at given offset (for patching file length at position 0).
    void patchInt32(size_t offset, int32_t value) {
        if (offset + 4 > data_.size())
            throw std::runtime_error("BigEndianWriter: patch out of bounds");
        write_int32_be(data_.data(), offset, value);
    }

private:
    std::vector<uint8_t> data_;
};

} // namespace liarsoft

#endif // LIARSOFTTOOL_BIGENDIAN_H
