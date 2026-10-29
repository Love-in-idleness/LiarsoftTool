#pragma once
#include "kinsoku.h"
#include "kinsoku_match.h"
#include "unicode.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace liarsoft::runtime {
size_t installNativeKinsoku(void* module, GlyphTokens& tokens, const KinsokuRules& rules);
void removeNativeKinsoku();
}
