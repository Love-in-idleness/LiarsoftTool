#pragma once
#include "kinsoku.h"
#include "unicode.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace liarsoft::runtime {
struct KinsokuBlock {
    size_t offset, firstPrefix, firstLength, normal, noStart, noEnd;
};
// Match the complete shared classifier, including all branch destinations.
std::vector<KinsokuBlock> findKinsokuBlocks(const uint8_t* bytes, size_t length);
size_t installNativeKinsoku(void* module, GlyphTokens& tokens, const KinsokuRules& rules);
void removeNativeKinsoku();
}
