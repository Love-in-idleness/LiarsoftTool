#include "rscript_plugin.h"
#include <windows.h>
#include <string>

#ifndef FIXTURE_FAILURE
#define FIXTURE_FAILURE 0
#endif
#ifndef FIXTURE_ABI
#define FIXTURE_ABI RSCRIPT_PLUGIN_ABI
#endif

namespace {
int32_t RSCRIPT_CALL initialize(const RScriptHostContext* context) {
    if (!context || context->size != sizeof(*context) || context->abi_version != RSCRIPT_PLUGIN_ABI ||
        !context->game_module || !context->host_module || !context->game_exe || !context->override_root ||
        !context->log || !context->get_engine_api || !context->set_engine_api)
        return 41;
    if (FIXTURE_FAILURE) return 42;
    wchar_t expected[1024]{};
    if (GetEnvironmentVariableW(L"RSCRIPT_TEST_KINSOKU", expected, 1024)) {
        if (!context->kinsoku_json) return 44;
        wchar_t actual[1024]{};
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, context->kinsoku_json, -1, actual, 1024) ||
            std::wstring(expected) != actual) return 45;
    }
    return SetEnvironmentVariableW(L"RSCRIPT_TEST_PLUGIN", L"initialized") ? 0 : 43;
}

void RSCRIPT_CALL shutdown() {
    wchar_t path[32768]{};
    if (!GetEnvironmentVariableW(L"RSCRIPT_TEST_SHUTDOWN", path, 32768)) return;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, "shutdown", 8, &written, nullptr);
        CloseHandle(file);
    }
}

const RScriptPluginDescriptor descriptor{
    sizeof(descriptor), FIXTURE_ABI, FIXTURE_FAILURE ? "fixture.failure" : "fixture.loader",
    "Loader fixture", 0, initialize, shutdown
};
}

extern "C" RSCRIPT_EXPORT const RScriptPluginDescriptor* RSCRIPT_CALL
RScriptPlugin_GetDescriptor(uint32_t host_abi) {
    return host_abi == RSCRIPT_PLUGIN_ABI ? &descriptor : nullptr;
}
