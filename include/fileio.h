#ifndef LIARSOFTTOOL_FILEIO_H
#define LIARSOFTTOOL_FILEIO_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace liarsoft {

/// Write `data` to `path`, but only when the file does not exist yet or its
/// current content differs from `data`. When the existing file is
/// byte-identical, it is left completely untouched so its modification time
/// is preserved. Returns true when the file was written, false when it was
/// skipped as unchanged. Throws std::runtime_error when the file cannot be
/// written.
bool writeFileIfChanged(const std::string& path, const uint8_t* data, size_t size);

/// Convenience overload for byte buffers.
bool writeFileIfChanged(const std::string& path, const std::vector<uint8_t>& data);

/// Text-mode variant: like writeFileIfChanged, but the content is written in
/// text mode (on Windows every '\n' becomes "\r\n", matching a plain
/// std::ofstream) and the comparison accounts for that translation, so
/// regenerating an identical text file still preserves its modification time.
bool writeTextFileIfChanged(const std::string& path, const std::string& text);

} // namespace liarsoft

#endif // LIARSOFTTOOL_FILEIO_H
