#ifndef LIARSOFTTOOL_ENCODING_H
#define LIARSOFTTOOL_ENCODING_H

#include <string>
#include <vector>

namespace liarsoft {

/// Convert a string between encodings.
/// `fromEnc` and `toEnc` are encoding names (e.g. "CP932", "GBK", "UTF-8").
/// With `strict`, malformed input and characters the target encoding cannot represent raise an
/// error instead of being replaced with '?', so that silently corrupted game
/// text cannot be produced.
std::string convertEncoding(const std::string& input,
                            const std::string& fromEnc,
                            const std::string& toEnc,
                            bool strict = false);

/// Canonicalize supported aliases. Japanese aliases map to CP932/Windows-31J,
/// rather than strict Shift-JIS, so Windows NEC/IBM extension bytes work on
/// every supported platform.
std::string normalizeEncodingName(const std::string& encoding);

/// Encoding choices shared by conversion and resource-preview interfaces.
const std::vector<std::string>& supportedEncodings();

/// Validate a public resource encoding; UTF-8 remains an internal text format.
void validateResourceEncoding(const std::string& encoding);

} // namespace liarsoft

#endif
