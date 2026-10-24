#pragma once
#include "archive_index.h"
#include "lim_decoder.h"

namespace liarsoft {
enum class PreviewKind { Unsupported, Image, Audio, Text };
struct ResourcePreview {
    PreviewKind kind = PreviewKind::Unsupported;
    std::string format, encoding, text; // Encoding describes the source; text is UTF-8.
    LimImage image; // Straight-alpha RGBA, including normalized CodeX BMP alpha.
    std::vector<uint8_t> audio;
    std::vector<std::string> warnings;
};
// Decode only a selected entry. Unknown data stays available through ResourceSource::read.
ResourcePreview previewResource(const ResourceSource& source, const std::string& name,
                                const std::string& encoding = "auto");
ResourcePreview decodeResourcePreview(const std::vector<uint8_t>& data, const std::string& name,
                                     const std::string& encoding = "auto");
}
