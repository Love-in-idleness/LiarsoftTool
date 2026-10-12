#include "fileio.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace liarsoft {

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("Cannot open: " + path.string());
    const std::streamoff size = in.tellg();
    if (size < 0 || static_cast<uintmax_t>(size) > std::numeric_limits<size_t>::max() ||
        static_cast<uintmax_t>(size) > std::numeric_limits<std::streamsize>::max())
        throw std::runtime_error("Cannot read size: " + path.string());
    in.seekg(0, std::ios::beg);
    if (!in) throw std::runtime_error("Cannot read: " + path.string());
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!data.empty() && !in.read(reinterpret_cast<char*>(data.data()),
                                  static_cast<std::streamsize>(size)))
        throw std::runtime_error("Cannot read: " + path.string());
    return data;
}

void copyModificationTime(const std::string& source, const std::string& output) {
    const auto time = std::filesystem::last_write_time(source);
    if (std::filesystem::last_write_time(output) != time)
        std::filesystem::last_write_time(output, time);
}

namespace {

// Returns true when the file at `path` exists and its binary content is
// exactly `data[0..size)`.
bool fileMatches(const std::string& path, const uint8_t* data, size_t size) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    in.seekg(0, std::ios::end);
    std::streamoff fileSize = in.tellg();
    if (fileSize < 0 || static_cast<uint64_t>(fileSize) != size) return false;
    in.seekg(0, std::ios::beg);
    if (!in) return false;

    // Avoid allocating a second full-size buffer when comparing large archives.
    std::array<uint8_t, 64 * 1024> buffer;
    for (size_t offset = 0; offset < size;) {
        const auto count = std::min(buffer.size(), size - offset);
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(count));
        if (in.gcount() != static_cast<std::streamsize>(count) ||
            std::memcmp(buffer.data(), data + offset, count) != 0)
            return false;
        offset += count;
    }
    return true;
}

} // namespace

bool writeFileIfChanged(const std::string& path, const uint8_t* data, size_t size) {
    // Identical content already on disk → leave the file untouched so its
    // modification time does not change.
    if (fileMatches(path, data, size)) return false;

    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write file: " + path);
    if (size > 0) {
        out.write(reinterpret_cast<const char*>(data),
                  static_cast<std::streamsize>(size));
        if (!out) throw std::runtime_error("Failed to write file: " + path);
    }
    return true;
}

bool writeFileIfChanged(const std::string& path, const std::vector<uint8_t>& data) {
    return writeFileIfChanged(path, data.data(), data.size());
}

bool writeTextFileIfChanged(const std::string& path, const std::string& text) {
    // A plain (text-mode) ofstream translates every '\n' to "\r\n" on
    // Windows; simulate that here so the comparison sees exactly the bytes a
    // text-mode write would produce.
    std::string native;
#ifdef _WIN32
    native.reserve(text.size() + 16);
    for (char c : text) {
        if (c == '\n') {
            native += '\r';
            native += '\n';
        } else {
            native += c;
        }
    }
#else
    native = text;
#endif

    if (fileMatches(path, reinterpret_cast<const uint8_t*>(native.data()),
                    native.size()))
        return false;

    std::ofstream out(path); // text mode, matching the original writers
    if (!out) throw std::runtime_error("Cannot write file: " + path);
    out << text;
    if (!out) throw std::runtime_error("Failed to write file: " + path);
    return true;
}

} // namespace liarsoft
