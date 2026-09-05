#ifndef LIARSOFTTOOL_ENCODING_H
#define LIARSOFTTOOL_ENCODING_H

#include <string>

namespace liarsoft {

/// Convert a string between encodings.
/// `fromEnc` and `toEnc` are encoding names (e.g. "CP932", "GBK", "UTF-8").
std::string convertEncoding(const std::string& input,
                            const std::string& fromEnc,
                            const std::string& toEnc);

/// Canonicalize supported aliases. Japanese aliases map to CP932/Windows-31J,
/// rather than strict Shift-JIS, so Windows NEC/IBM extension bytes work on
/// every supported platform.
std::string normalizeEncodingName(const std::string& encoding);

} // namespace liarsoft

#endif
