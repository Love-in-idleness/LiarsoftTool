#include "fileio.h"
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace liarsoft {

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

    std::vector<uint8_t> existing(size);
    in.read(reinterpret_cast<char*>(existing.data()),
            static_cast<std::streamsize>(size));
    if (in.gcount() != static_cast<std::streamsize>(size)) return false;
    return std::memcmp(existing.data(), data, size) == 0;
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
