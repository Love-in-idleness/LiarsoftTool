#include <windows.h>
#include <cstdio>

int wmain() {
    wchar_t path[32768]{};
    if (!GetEnvironmentVariableW(L"RSCRIPT_TEST_RESULT", path, 32768)) return 10;
    FILE* output = _wfopen(path, L"wb");
    if (!output) return 11;
    const bool host = GetModuleHandleW(L"RScriptHost.dll") != nullptr;
    wchar_t marker[32]{};
    GetEnvironmentVariableW(L"RSCRIPT_TEST_PLUGIN", marker, 32);
    std::fprintf(output, "game entered\nhost=%d\nplugin=%ls\n", host ? 1 : 0, marker);
    if (std::fclose(output)) return 12;
    return 0;
}
