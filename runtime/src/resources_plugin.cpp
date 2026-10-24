#include "forest_bridge.h"
#include "resource_provider.h"
#include "wcg_decoder.h"
#include "lim_decoder.h"
#include <algorithm>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>

namespace {
const RScriptHostContext* host = nullptr;
RScriptForestBridge* bridge = nullptr;
thread_local std::string last_error;

void RSCRIPT_CALL release(void* owner) { delete static_cast<std::vector<uint8_t>*>(owner); }

int RSCRIPT_CALL load(const wchar_t* relative, uint32_t kind,
                     RScriptBuffer* output, const char** error) {
    try {
        if (!relative || !output || kind > RScriptComponent) throw std::runtime_error("Invalid resource request");
        const std::filesystem::path path(relative);
        auto result = liarsoft::runtime::loadOverride(host->override_root, path,
            static_cast<liarsoft::runtime::ResourceKind>(kind),
            (host->enabled_features & RSCRIPT_FEATURE_UNICODE) != 0);
        if (!result) return 0;
        if (kind == RScriptScript &&
            (result->bytes.size() < 28 || result->bytes[4] != 28 || result->bytes[5] ||
             result->bytes[6] || result->bytes[7]))
            throw std::runtime_error("Forest requires legacy-28 scripts: " + result->source.u8string());
        // Forest chooses its decoder from the requested suffix before reading bytes.
        auto ext = path.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (kind == RScriptImage && ext == L".lim") {
            auto image = liarsoft::wcgDecode(result->bytes);
            for (size_t i = 0; i < image.pixels.size(); i += 4)
                std::swap(image.pixels[i], image.pixels[i + 2]);
            result->bytes = liarsoft::limEncode({image.width, image.height, std::move(image.pixels)});
        }
        if (result->bytes.size() > UINT32_MAX) throw std::runtime_error("Resource exceeds engine size limit");
        if (host->log) host->log(RSCRIPT_LOG_INFO, ("Override: " + result->source.u8string()).c_str());
        auto bytes = std::make_unique<std::vector<uint8_t>>(std::move(result->bytes));
        *output = {bytes->data(), static_cast<uint32_t>(bytes->size()), bytes.get(), release,
                   result->utf8Script ? 1u : 0u};
        bytes.release();
        return 1;
    } catch (const std::exception& exception) {
        last_error = exception.what();
        if (error) *error = last_error.c_str();
        return -1;
    }
}

int32_t RSCRIPT_CALL initialize(const RScriptHostContext* context) {
    bridge = forestBridge(context);
    if (!bridge) return 1;
    host = context;
    bridge->load_resource = load;
    return 0;
}
void RSCRIPT_CALL shutdown() { if (bridge) bridge->load_resource = nullptr; }
const RScriptPluginDescriptor descriptor{sizeof(descriptor), RSCRIPT_PLUGIN_ABI,
    "rscript.resources", "RScript resource overrides", RSCRIPT_FEATURE_RESOURCES, initialize, shutdown};
}

extern "C" RSCRIPT_EXPORT const RScriptPluginDescriptor* RSCRIPT_CALL
RScriptPlugin_GetDescriptor(uint32_t version) { return version == RSCRIPT_PLUGIN_ABI ? &descriptor : nullptr; }
