#pragma once

#include "unicode.h"
#include "rscript_plugin.h"

namespace liarsoft::runtime {

void installEngineTextHooks(void* gameModule, GlyphTokens& tokens,
                            void (RSCRIPT_CALL *log)(uint32_t, const char*));
void removeEngineTextHooks();

} // namespace liarsoft::runtime
