#include "fileio.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace liarsoft {

std::vector<uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("Cannot open: " + path.string());
    try { return readStream(in); }
    catch (const std::runtime_error& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}

std::vector<uint8_t> readStream(std::ifstream& in) {
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0 || static_cast<uintmax_t>(size) > std::numeric_limits<size_t>::max() ||
        static_cast<uintmax_t>(size) > std::numeric_limits<std::streamsize>::max())
        throw std::runtime_error("Cannot read file size");
    in.seekg(0, std::ios::beg);
    if (!in) throw std::runtime_error("Cannot seek file");
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!data.empty() && !in.read(reinterpret_cast<char*>(data.data()),
                                  static_cast<std::streamsize>(size)))
        throw std::runtime_error("Failed to read entire file");
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

void installFile(const std::string& path, const uint8_t* data, size_t size) {
    namespace fs = std::filesystem;
    fs::path target(path);
    // Follow existing symlinks like the previous stream writer.
    if (fs::is_symlink(fs::symlink_status(target))) target = fs::canonical(target);
    const auto status = fs::status(target);
    if (fs::exists(status)) {
        if (!fs::is_regular_file(status) ||
            (status.permissions() & (fs::perms::owner_write | fs::perms::group_write |
                                     fs::perms::others_write)) == fs::perms::none)
            throw std::runtime_error("Cannot write file: " + path);
        // Replacement must not bypass the target's existing write permissions/ACL.
#ifdef _WIN32
        const auto probe = CreateFileW(target.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (probe == INVALID_HANDLE_VALUE)
            throw std::system_error(GetLastError(), std::system_category(), "Cannot write: " + path);
        if (!CloseHandle(probe))
            throw std::system_error(GetLastError(), std::system_category(), "Cannot close: " + path);
#else
        const int probe = ::open(target.c_str(), O_WRONLY);
        if (probe < 0)
            throw std::system_error(errno, std::generic_category(), "Cannot write: " + path);
        if (::close(probe) != 0)
            throw std::system_error(errno, std::generic_category(), "Cannot close: " + path);
#endif
    }
    static std::atomic<uint64_t> sequence{0};
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path temporary;
    int fd = -1;
    for (int attempt = 0; attempt < 100; ++attempt) {
        temporary = target.parent_path() / (".liarsoft-" + std::to_string(nonce) +
                    "-" + std::to_string(sequence++) + ".tmp");
#ifdef _WIN32
        fd = _wopen(temporary.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                    _S_IREAD | _S_IWRITE);
#else
        fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
#endif
        if (fd >= 0) break;
        if (errno != EEXIST)
            throw std::system_error(errno, std::generic_category(), "Cannot create temporary file for: " + path);
    }
    if (fd < 0) throw std::runtime_error("Cannot create temporary file for: " + path);
    FILE* output = nullptr;
    try {
#ifdef _WIN32
        output = _fdopen(fd, "wb");
#else
        output = fdopen(fd, "wb");
#endif
        if (!output) throw std::system_error(errno, std::generic_category(), "Cannot open temporary stream: " + path);
        fd = -1; // The stream owns the descriptor.
        if ((size && std::fwrite(data, 1, size, output) != size) || std::fflush(output) != 0)
            throw std::system_error(errno, std::generic_category(), "Failed to write: " + path);
        const auto closed = std::fclose(output);
        output = nullptr;
        if (closed != 0)
            throw std::system_error(errno, std::generic_category(), "Failed to close: " + path);
        if (fs::exists(status)) fs::permissions(temporary, status.permissions());
#ifdef _WIN32
        // Same-volume replacement only: never fall back to copy/delete.
        if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING))
            throw std::system_error(GetLastError(), std::system_category(), "Failed to replace: " + path);
#else
        fs::rename(temporary, target);
#endif
    } catch (...) {
        if (output) std::fclose(output);
        if (fd >= 0) {
#ifdef _WIN32
            _close(fd);
#else
            ::close(fd);
#endif
        }
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}

} // namespace

bool writeFileIfChanged(const std::string& path, const uint8_t* data, size_t size) {
    // Identical content already on disk → leave the file untouched so its
    // modification time does not change.
    if (fileMatches(path, data, size)) return false;

    installFile(path, data, size);
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

    return writeFileIfChanged(path, reinterpret_cast<const uint8_t*>(native.data()), native.size());
}

} // namespace liarsoft
