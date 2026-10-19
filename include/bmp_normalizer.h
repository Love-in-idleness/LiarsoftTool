#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace liarsoft {

// Explicit CodeX unpack operation: legacy BI_RGB32 uses inverse alpha.
// Other BMP encodings, including already normalized V4/V5, remain unchanged.
std::vector<uint8_t> normalizeCodexBmp(const std::vector<uint8_t>& data);
void normalizeCodexBmpFile(const std::string& input, const std::string& output);

} // namespace liarsoft
