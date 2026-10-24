#pragma once

#include "lim_decoder.h"
#include <filesystem>
#include <optional>
#include <vector>

namespace liarsoft::runtime {

enum class ResourceKind { Script, Image, Audio, Component };

struct ResourceData {
    std::filesystem::path source;
    std::vector<uint8_t> bytes;
    bool utf8Script = false;
};

// Missing overrides return nullopt. Invalid or inaccessible overrides throw;
// callers must not fall back to the archive after an error.
std::optional<std::filesystem::path> selectResource(
    const std::filesystem::path& root, const std::filesystem::path& relative,
    ResourceKind kind);

// Decode existing codecs to straight-alpha RGBA without modifying the source.
LimImage decodeRuntimeImage(const std::vector<uint8_t>& bytes,
                            const std::string& sourceExtension);

// Engine-consumable GSC, WCG, PCM WAV or LWG, compiled/decoded only on request.
// unicode applies only to TSC; existing binary GSC keeps its original encoding.
std::optional<ResourceData> loadOverride(
    const std::filesystem::path& root, const std::filesystem::path& relative,
    ResourceKind kind, bool unicode = false);

} // namespace liarsoft::runtime
