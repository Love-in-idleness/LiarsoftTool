#include "rscript_plugin.h"

namespace {
int32_t RSCRIPT_CALL initialize(const RScriptHostContext* context) {
    if (!context || context->size < sizeof(*context) || context->abi_version != RSCRIPT_PLUGIN_ABI)
        return 1;
    context->log(RSCRIPT_LOG_INFO, "Example DLL loaded before the game entry point");
    return 0;
}

const RScriptPluginDescriptor descriptor{
    sizeof(descriptor), RSCRIPT_PLUGIN_ABI, "example.log", "Example log DLL", 0, initialize, nullptr
};
}

extern "C" RSCRIPT_EXPORT const RScriptPluginDescriptor* RSCRIPT_CALL
RScriptPlugin_GetDescriptor(uint32_t host_abi) {
    return host_abi == RSCRIPT_PLUGIN_ABI ? &descriptor : nullptr;
}
