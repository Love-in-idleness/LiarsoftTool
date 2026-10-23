#include "resource_preview.h"
#include "bmp_normalizer.h"
#include "cg_decompress.h"
#include "encoding.h"
#include "gsc_decompiler.h"
#include "stb_image.h"
#include "wav_ogg.h"
#include "wcg_decoder.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace liarsoft {
namespace {
bool magic(const std::vector<uint8_t>& bytes, size_t offset, const char* value, size_t length) {
    return offset <= bytes.size() && length <= bytes.size() - offset &&
        std::memcmp(bytes.data() + offset, value, length) == 0;
}
std::string format(const std::vector<uint8_t>& data, const std::string& name) {
    auto extension = std::filesystem::u8path(name).extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) { return std::tolower(ch); });
    // GSC starts with a size, whose low bytes can accidentally spell an image magic.
    if (extension == ".gsc") return "GSC";
    if (extension == ".tsc") return "TSC";
    if (extension == ".txt" || extension == ".xml" || extension == ".ini" || extension == ".csv") return "Text";
    if (extension == ".wcg") return "WCG";
    if (extension == ".lim") return "LIM";
    if (extension == ".wav") return "WAV";
    if (extension == ".ogg") return "OGG";
    if (extension == ".bmp" || extension == ".msk") return "BMP";
    if (extension == ".png") return "PNG";
    if (extension == ".jpg" || extension == ".jpeg") return "JPEG";
    if (extension == ".webp") return "WebP";
    if (magic(data, 0, "WG", 2)) return "WCG";
    if (magic(data, 0, "LM", 2)) return "LIM";
    if (magic(data, 0, "BM", 2)) return "BMP";
    if (magic(data, 0, "\x89PNG\r\n\x1a\n", 8)) return "PNG";
    if (magic(data, 0, "\xff\xd8", 2)) return "JPEG";
    if (magic(data, 0, "RIFF", 4) && magic(data, 8, "WEBP", 4)) return "WebP";
    if (magic(data, 0, "RIFF", 4) && magic(data, 8, "WAVE", 4)) return "WAV";
    if (magic(data, 0, "OggS", 4)) return "OGG";
    return "";
}
}
ResourcePreview decodeResourcePreview(const std::vector<uint8_t>& data, const std::string& name,
                                     const std::string& encoding) {
    ResourcePreview result;
    result.format = format(data, name);
    if (result.format.empty()) return result;
    if (result.format == "GSC") {
        result.kind = PreviewKind::Text; result.encoding = "UTF-8";
        result.text = decompileGscBytes(data, encoding, &result.warnings, name);
    } else if (result.format == "Text" || result.format == "TSC") {
        result.kind = PreviewKind::Text;
        const std::string input(data.begin(), data.end());
        result.encoding = result.format == "TSC" ? "UTF-8" : encoding;
        result.text = convertEncoding(input, result.encoding, "UTF-8", true);
    } else if (result.format == "OGG" || result.format == "WAV") {
        result.kind = PreviewKind::Audio;
        if (result.format == "OGG") result.audio = data;
        else if (WavOggExtractor::hasEmbeddedOgg(data)) {
            result.audio = WavOggExtractor::extract(data);
            result.format = "WAV / OGG";
        } else if (WavOggExtractor::isStandardPcmWav(data)) result.audio = data;
        else throw std::runtime_error("Unsupported or damaged WAV audio");
    } else {
        result.kind = PreviewKind::Image;
        if (result.format == "WCG") {
            auto image = wcgDecode(data);
            for (size_t i = 0; i < image.pixels.size(); i += 4)
                std::swap(image.pixels[i], image.pixels[i + 2]);
            result.image = {image.width, image.height, std::move(image.pixels)};
        } else if (result.format == "LIM") result.image = limDecode(data);
        else if (result.format == "WebP") result.image = webpDecode(data);
        else {
            const auto normalized = result.format == "BMP" ? normalizeCodexBmp(data) : std::vector<uint8_t>{};
            const auto& bytes = result.format == "BMP" ? normalized : data;
            if (bytes.size() > INT32_MAX) throw std::runtime_error("Image exceeds decoder input size");
            int width, height, channels;
            if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels))
                throw std::runtime_error("Invalid image header");
            checkedRgbaSize(width, height);
            std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
                stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4), stbi_image_free);
            if (!pixels) throw std::runtime_error("Image decoding failed");
            result.image.width = width; result.image.height = height;
            const auto size = checkedRgbaSize(width, height);
            result.image.pixels.assign(pixels.get(), pixels.get() + size);
        }
    }
    return result;
}
ResourcePreview previewResource(const ResourceSource& source, const std::string& name,
                                const std::string& encoding) {
    if (format(source.read(0, static_cast<size_t>(std::min<uint64_t>(16, source.size()))), name).empty())
        return {};
    return decodeResourcePreview(source.readAll(), name, encoding);
}
}
