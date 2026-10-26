#pragma once

#include <vector>

namespace liarsoft::runtime {

struct ImportPatch {
    void** slot;
    void* original;
};

// Only the supplied module's named imports are changed; dependencies are not hooked.
void* hookImport(void* module, const char* name, void* replacement,
                 std::vector<ImportPatch>& patches);
void restoreImports(std::vector<ImportPatch>& patches);

} // namespace liarsoft::runtime
