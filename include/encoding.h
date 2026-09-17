#ifndef LIARSOFTTOOL_ENCODING_H
#define LIARSOFTTOOL_ENCODING_H

#include <string>

namespace liarsoft {

/// Convert a string between encodings.
/// `fromEnc` and `toEnc` are encoding names (e.g. "CP932", "GBK", "UTF-8").
/// With `strict`, characters the target encoding cannot represent raise an
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

} // namespace liarsoft

#endif
