#pragma once

#include "rscript_plugin.h"
#include <stdint.h>

// Private interface between the built-in modules, not the public plugin SDK.
struct RScriptBuffer {
    const uint8_t* data;
    uint32_t size;
    void* owner;
    void (RSCRIPT_CALL *release)(void* owner);
    uint32_t utf8_script;
};

enum RScriptResourceKind { RScriptScript, RScriptImage, RScriptAudio, RScriptComponent };

struct RScriptForestBridge {
    uint32_t size;
    uint32_t version;
    void* game_module;
    const RScriptHostContext* host;
    // 1 = handled, 0 = absent/unchanged, -1 = error. Buffers belong to the provider.
    int (RSCRIPT_CALL *load_resource)(const wchar_t* relative, uint32_t kind,
                                    RScriptBuffer* output, const char** error);
    int (RSCRIPT_CALL *transform_script)(const uint8_t* bytes, uint32_t size,
        uint32_t utf8, RScriptBuffer* output, const char** error);
};

inline RScriptForestBridge* forestBridge(const RScriptHostContext* context) {
    if (!context || context->size < sizeof(*context) || !context->get_engine_api) return nullptr;
    auto* bridge = static_cast<RScriptForestBridge*>(context->get_engine_api());
    return bridge && bridge->size == sizeof(*bridge) && bridge->version == 1 ? bridge : nullptr;
}
