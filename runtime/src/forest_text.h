#pragma once

#include "unicode.h"
#include "rscript_plugin.h"

namespace liarsoft::runtime {

void installForestTextHooks(void* gameModule, GlyphTokens& tokens,
                            void (RSCRIPT_CALL *log)(uint32_t, const char*));
void removeForestTextHooks();

} // namespace liarsoft::runtime
