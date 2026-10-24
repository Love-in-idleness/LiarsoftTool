#ifndef RSCRIPT_PLUGIN_H
#define RSCRIPT_PLUGIN_H

#include <stdint.h>
#include <wchar.h>

#ifdef _WIN32
#define RSCRIPT_CALL __cdecl
#define RSCRIPT_EXPORT __declspec(dllexport)
#else
#define RSCRIPT_CALL
#define RSCRIPT_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define RSCRIPT_PLUGIN_ABI 1u
#define RSCRIPT_PLUGIN_ENTRY "RScriptPlugin_GetDescriptor"

enum RScriptPluginFeature {
    RSCRIPT_FEATURE_ENGINE = 1u,
    RSCRIPT_FEATURE_RESOURCES = 2u,
    RSCRIPT_FEATURE_UNICODE = 4u
};

enum RScriptLogLevel {
    RSCRIPT_LOG_INFO = 0,
    RSCRIPT_LOG_WARNING = 1,
    RSCRIPT_LOG_ERROR = 2
};

/* All strings and callback arguments are borrowed for the duration of the call.
 * Paths use Windows UTF-16; IDs, names and log messages use UTF-8. */
typedef struct RScriptHostContext {
    uint32_t size;
    uint32_t abi_version;
    void* game_module;
    void* host_module;
    const wchar_t* game_exe;
    const wchar_t* override_root;
    uint32_t enabled_features;
    void (RSCRIPT_CALL *log)(uint32_t level, const char* message);
    /* Opaque, private bridge between the selected built-in engine modules. */
    void (RSCRIPT_CALL *set_engine_api)(void* api);
    void* (RSCRIPT_CALL *get_engine_api)(void);
    /* Optional validated UTF-8 JSON. Check size before accessing appended fields. */
    const char* kinsoku_json;
} RScriptHostContext;

typedef struct RScriptPluginDescriptor {
    uint32_t size;
    uint32_t abi_version;
    const char* id;
    const char* name;
    uint32_t features;
    int32_t (RSCRIPT_CALL *initialize)(const RScriptHostContext* context);
    /* Called during explicit shutdown or initialization rollback, not DllMain.
     * Normal process termination releases modules without this callback. */
    void (RSCRIPT_CALL *shutdown)(void);
} RScriptPluginDescriptor;

typedef const RScriptPluginDescriptor* (RSCRIPT_CALL *RScriptGetDescriptor)(uint32_t host_abi);

/* Export RSCRIPT_PLUGIN_ENTRY with this signature. Return NULL for unsupported
 * ABI versions. Keep the descriptor alive until the process exits. */
RSCRIPT_EXPORT const RScriptPluginDescriptor* RSCRIPT_CALL
RScriptPlugin_GetDescriptor(uint32_t host_abi);

#ifdef __cplusplus
}
#endif
#endif
