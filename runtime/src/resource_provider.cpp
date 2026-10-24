#include "resource_provider.h"
#include "encoding.h"
#include "fileio.h"
#include "gsc_decompiler.h"
#include "gscfile.h"
#include "lwg_decoder.h"
#include "resource_preview.h"
#include "wav_ogg.h"
#include "wcg_decoder.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <stdexcept>

namespace liarsoft::runtime {
namespace fs = std::filesystem;
namespace {
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

fs::path relativePath(const fs::path& input) {
    auto text = input.u8string();
    std::replace(text.begin(), text.end(), '\\', '/');
    const auto path = fs::u8path(text);
    if (text.empty() || text.find('\0') != std::string::npos ||
        text.find(':') != std::string::npos || path.is_absolute() || path.has_root_path())
        throw std::runtime_error("Override path must be relative to its resource directory");
    for (const auto& item : path)
        if (item == "..") throw std::runtime_error("Override path escapes its resource directory");
    return path.lexically_normal();
}

bool inside(const fs::path& root, const fs::path& candidate) {
    auto part = candidate.begin();
    for (const auto& item : root) {
        if (part == candidate.end() ||
#ifdef _WIN32
            lower(item.u8string()) != lower(part->u8string())
#else
            item != *part
#endif
        )
            return false;
        ++part;
    }
    return true;
}

// Emulate Windows filename lookup on Linux too, while rejecting ambiguous fixtures.
std::optional<fs::path> lookup(const fs::path& root, const fs::path& relative) {
    fs::path current = root;
    for (const auto& part : relative) {
        if (part == "." || part.empty()) continue;
        std::optional<fs::path> match;
        for (const auto& entry : fs::directory_iterator(current)) {
            if (lower(entry.path().filename().u8string()) != lower(part.u8string())) continue;
            if (match) throw std::runtime_error("Ambiguous resource filename: " + relative.u8string());
            match = entry.path();
        }
        if (!match) return {};
        const auto resolved = fs::canonical(*match);
        if (!inside(root, resolved))
            throw std::runtime_error("Override symlink escapes its resource directory: " + match->u8string());
        current = resolved;
    }
    return current;
}

std::optional<fs::path> regular(const fs::path& root, const fs::path& relative) {
    auto found = lookup(root, relative);
    if (found && !fs::is_regular_file(*found))
        throw std::runtime_error("Override is not a regular file: " + found->u8string());
    return found;
}

fs::path stemPath(fs::path relative) {
    const auto ext = lower(relative.extension().u8string());
    for (const auto* known : {".gsc", ".tsc", ".wav", ".ogg", ".webp", ".png", ".lim", ".wcg", ".lwg"})
        if (ext == known) { relative.replace_extension(); break; }
    return relative;
}

std::optional<fs::path> select(const fs::path& root, const fs::path& relative, ResourceKind kind) {
    auto stem = stemPath(relative);
    if (kind == ResourceKind::Component) {
        if (const auto directory = lookup(root, stem)) {
            if (fs::is_directory(*directory)) {
                if (const auto meta = regular(root, stem / ".meta.xml")) return meta;
            } else if (relative == stem) {
                throw std::runtime_error("Component override is not a directory: " + directory->u8string());
            }
        }
    }
    const std::vector<const char*> extensions = kind == ResourceKind::Script
        ? std::vector<const char*>{".tsc", ".gsc"}
        : kind == ResourceKind::Audio ? std::vector<const char*>{".ogg", ".wav"}
        : kind == ResourceKind::Image ? std::vector<const char*>{".webp", ".png", ".lim", ".wcg"}
        : std::vector<const char*>{".lwg"};
    for (const auto* extension : extensions) {
        auto candidate = stem; candidate += extension;
        if (auto found = regular(root, candidate)) return found;
    }
    return {};
}

std::vector<uint8_t> imageBytes(const std::vector<uint8_t>& bytes, const fs::path& path) {
    const auto image = decodeRuntimeImage(bytes, path.extension().u8string());
    if (lower(path.extension().u8string()) == ".wcg") return bytes;
    return wcgEncode(image.pixels, image.width, image.height);
}

std::vector<uint8_t> component(const fs::path& root, const fs::path& meta) {
    const auto directory = meta.parent_path();
    const auto layout = LwgPacker::readMetadata(directory);
    if (!layout.width || !layout.height || layout.entries.empty())
        throw std::runtime_error("Component layout has no canvas or entries: " + meta.u8string());
    struct Entry { std::string name; std::vector<uint8_t> bytes; };
    std::vector<Entry> entries;
    uint64_t tableSize = 0, dataSize = 0;
    for (const auto& item : layout.entries) {
        Entry entry;
        entry.name = convertEncoding(item.name, "UTF-8", "CP932", true);
        if (entry.name.empty() || entry.name.size() > UINT8_MAX || entry.name.find('\0') != std::string::npos)
            throw std::runtime_error("Invalid component layer name: " + item.name);
        if (!item.empty) {
            auto filename = item.name;
            for (auto& ch : filename)
                if (std::string("<>:\"/\\|?*").find(ch) != std::string::npos) ch = '_';
            const auto logical = directory.lexically_relative(root) / fs::u8path(filename);
            // Extracted layers append a format extension even if the layer name contains one.
            std::optional<fs::path> file;
            for (const auto* extension : {".webp", ".png", ".lim", ".wcg"}) {
                auto candidate = logical; candidate += extension;
                if ((file = regular(root, candidate))) break;
            }
            if (!file) file = select(root, logical, ResourceKind::Image);
            if (file) entry.bytes = imageBytes(readFile(*file), *file);
            else {
                auto exact = regular(root, logical);
                auto mask = logical; mask += ".msk";
                if (!exact) exact = regular(root, mask);
                if (!exact)
                    throw std::runtime_error("Component layer is missing: " + item.name);
                entry.bytes = readFile(*exact);
                if (lower(exact->extension().u8string()) == ".msk")
                    decodeRuntimeImage(entry.bytes, ".msk");
                else entry.bytes = imageBytes(entry.bytes, *exact);
            }
        }
        tableSize += 18 + entry.name.size();
        dataSize += entry.bytes.size();
        if (tableSize > UINT32_MAX || dataSize > UINT32_MAX || entries.size() >= UINT32_MAX)
            throw std::runtime_error("Component exceeds LWG size limits");
        entries.push_back(std::move(entry));
    }
    std::vector<uint8_t> output;
    if (tableSize + dataSize + 28 > output.max_size())
        throw std::runtime_error("Component exceeds addressable memory");
    output.reserve(static_cast<size_t>(tableSize + dataSize + 28));
    auto append = [&](uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) output.push_back(static_cast<uint8_t>(value >> shift));
    };
    output.insert(output.end(), {'L', 'G', 1, 0});
    append(layout.height); append(layout.width); append(static_cast<uint32_t>(entries.size()));
    append(0); append(static_cast<uint32_t>(tableSize));
    uint32_t offset = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        append(static_cast<uint32_t>(layout.entries[i].x));
        append(static_cast<uint32_t>(layout.entries[i].y));
        output.push_back(layout.entries[i].flag);
        append(offset); append(static_cast<uint32_t>(entries[i].bytes.size()));
        output.push_back(static_cast<uint8_t>(entries[i].name.size()));
        output.insert(output.end(), entries[i].name.begin(), entries[i].name.end());
        offset += static_cast<uint32_t>(entries[i].bytes.size());
    }
    append(static_cast<uint32_t>(dataSize));
    for (const auto& entry : entries) output.insert(output.end(), entry.bytes.begin(), entry.bytes.end());
    return output;
}
} // namespace

std::optional<fs::path> selectResource(const fs::path& root, const fs::path& relative, ResourceKind kind) {
    const auto canonicalRoot = fs::canonical(root);
    if (!fs::is_directory(canonicalRoot)) throw std::runtime_error("Resource root is not a directory");
    return select(canonicalRoot, relativePath(relative), kind);
}

LimImage decodeRuntimeImage(const std::vector<uint8_t>& bytes, const std::string& extension) {
    auto decoded = decodeResourcePreview(bytes, "image" + extension);
    if (decoded.kind != PreviewKind::Image)
        throw std::runtime_error("Unsupported runtime image: " + extension);
    return std::move(decoded.image);
}

std::optional<ResourceData> loadOverride(const fs::path& root, const fs::path& relative,
                                        ResourceKind kind, bool unicode) {
    const auto path = selectResource(root, relative, kind);
    if (!path) return {};
    ResourceData result{*path, {}, false};
    try {
        if (kind == ResourceKind::Component && lower(path->filename().u8string()) == ".meta.xml") {
            result.bytes = component(fs::canonical(root), *path);
            return result;
        }
        auto bytes = readFile(*path);
        const auto extension = lower(path->extension().u8string());
        if (kind == ResourceKind::Script) {
            if (extension == ".tsc") {
                const std::string text(bytes.begin(), bytes.end());
                convertEncoding(text, "UTF-8", "UTF-8", true);
                if (text.rfind(";@gsc-raw-v1 ", 0) == 0 ||
                    text.find("\n;@gsc-raw-v1 ") != std::string::npos)
                    throw std::runtime_error("Runtime requires current structured TSC, not raw GSC metadata");
                result.bytes = restoreGscFromTsc(text, unicode ? "UTF-8" : "CP932");
                result.utf8Script = unicode;
            } else {
                GscFile::fromBytes(bytes, "CP932");
                result.bytes = std::move(bytes);
            }
        } else if (kind == ResourceKind::Image) result.bytes = imageBytes(bytes, *path);
        else if (kind == ResourceKind::Audio) {
            if (extension == ".ogg") result.bytes = WavOggExtractor::decodeToPcm(bytes);
            else if (WavOggExtractor::hasEmbeddedOgg(bytes))
                result.bytes = WavOggExtractor::decodeToPcm(WavOggExtractor::extract(bytes));
            else if (WavOggExtractor::isStandardPcmWav(bytes)) result.bytes = std::move(bytes);
            else throw std::runtime_error("Unsupported or damaged WAV audio");
        } else {
            LwgDecoder::decode(bytes, "CP932");
            result.bytes = std::move(bytes);
        }
        return result;
    } catch (const std::exception& error) {
        throw std::runtime_error(path->u8string() + ": " + error.what());
    }
}

} // namespace liarsoft::runtime
