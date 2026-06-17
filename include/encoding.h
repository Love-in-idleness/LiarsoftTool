#ifndef LIARSOFTTOOL_ENCODING_H
#define LIARSOFTTOOL_ENCODING_H

#include <string>

namespace liarsoft {

/// Convert a string between encodings.
/// `fromEnc` and `toEnc` are encoding names (e.g. "SHIFT_JIS", "GBK", "UTF-8").
std::string convertEncoding(const std::string& input,
                            const std::string& fromEnc,
                            const std::string& toEnc);

} // namespace liarsoft

#endif
