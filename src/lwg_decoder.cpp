#include "archive_index.h"
#include "lwg_decoder.h"
#include "encoding.h"
#include "fileio.h"
#include "xflarchive.h"
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <limits>

namespace fs = std::filesystem;

namespace liarsoft {

static std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

static std::string normalized(const fs::path& path) {
    return fs::absolute(path).lexically_normal().string();
}

static std::string encodeName(const std::string& utf8, const std::string& enc) {
    return convertEncoding(utf8, "UTF-8", enc);
}

// Layer names are XML text, not markup (e.g. Photoshop's </Layer set>).
// Decode in one pass so a literal "&lt;" survives as "&amp;lt;" on disk.
static std::string xmlName(const std::string& name, bool escape) {
    const std::array<std::pair<char, const char*>, 5> entities = {{
        {'&', "&amp;"}, {'<', "&lt;"}, {'>', "&gt;"},
        {'\"', "&quot;"}, {'\'', "&apos;"}
    }};
    std::string result;
    for (size_t i = 0; i < name.size();) {
        bool matched = false;
        for (const auto& entity : entities) {
            if (escape ? name[i] == entity.first :
                         name.compare(i, std::strlen(entity.second), entity.second) == 0) {
                if (escape) result += entity.second;
                else result += entity.first;
                i += escape ? 1 : std::strlen(entity.second);
                matched = true;
                break;
            }
        }
        if (!matched) result += name[i++];
    }
    return result;
}

// ---- Helper: sanitize filename ----

static std::string sanitizeFilename(const std::string& name) {
    std::string s = name;
    for (auto& ch : s) {
        if (ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
            ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*')
            ch = '_';
    }
    return s;
}

// ---- Helper: guess file extension from magic bytes ----

static std::string guessExt(const std::vector<uint8_t>& data) {
    if (data.size() >= 2) {
        if (data[0] == 0x57 && data[1] == 0x47) return ".wcg"; // "WG"
        if (data[0] == 0x4C && data[1] == 0x4D) return ".lim"; // "LM"
        if (data[0] == 0x42 && data[1] == 0x4D) return ".msk"; // "BM"
    }
    return "";
}

// ---- LWG Decoder ----

bool LwgDecoder::isRecognized(const std::vector<uint8_t>& data) {
    if (data.size() < 28) return false; // 24-byte header and 4-byte table terminator.
    return data[0] == 'L' && data[1] == 'G' && data[2] == 0x01 && data[3] == 0x00;
}

LwgDecoder::Archive LwgDecoder::decode(const std::vector<uint8_t>& data,
                                        const std::string& encoding) {
    const auto index = readArchiveIndex(data, encoding);
    if (index.format != "LWG") throw std::runtime_error("Not a valid LWG archive");
    Archive archive;
    archive.width = index.width;
    archive.height = index.height;
    for (const auto& item : index.entries) {
        LwgEntry entry;
        entry.name = item.name;
        entry.x = item.x; entry.y = item.y; entry.flag = item.flag;
        entry.size = static_cast<uint32_t>(item.size);
        entry.data.assign(data.begin() + static_cast<ptrdiff_t>(item.offset),
                          data.begin() + static_cast<ptrdiff_t>(item.offset + item.size));
        archive.entries.push_back(std::move(entry));
    }
    return archive;
}

void LwgDecoder::extractToDirectory(const Archive& archive, const std::string& dirPath,
                                     const std::string& encoding,
                                     std::optional<fs::file_time_type> sourceTime) {
    std::error_code ec;
    fs::create_directories(dirPath, ec);
    if (ec) throw std::runtime_error("Cannot create directory: " + dirPath);

    // Keep metadata for every entry, including entries without image payloads.
    {
        std::string meta;
        meta += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        meta += "<Canvas>\n";
        meta += "  <Width>" + std::to_string(archive.width) + "</Width>\n";
        meta += "  <Height>" + std::to_string(archive.height) + "</Height>\n";
        meta += "  <Items>\n";
        for (const auto& e : archive.entries) {
            // Empty entries still carry coordinates/flags and may share a
            // name with an image. Mark them so packing cannot reuse that image.
            meta += "    <Item x=\"" + std::to_string(e.x) + "\" y=\""
                  + std::to_string(e.y) + "\" flag=\""
                  + std::to_string(static_cast<int>(e.flag)) + "\""
                  + (e.data.empty() ? " empty=\"1\"" : "") + ">"
                  + xmlName(e.name, true) + "</Item>\n";
        }
        meta += "  </Items>\n";
        meta += "</Canvas>\n";
        writeTextFileIfChanged(dirPath + "/.meta.xml", meta);
        const auto path = fs::path(dirPath) / ".meta.xml";
        if (sourceTime && fs::last_write_time(path) != *sourceTime)
            fs::last_write_time(path, *sourceTime);
    }

    // Skip identical payloads, but still synchronize extraction timestamps.
    for (const auto& entry : archive.entries) {
        if (entry.data.empty()) continue;
        std::string ext = guessExt(entry.data);
        fs::path outPath = fs::path(dirPath) / (sanitizeFilename(entry.name) + ext);
        writeFileIfChanged(outPath.string(), entry.data);
        if (sourceTime && fs::last_write_time(outPath) != *sourceTime)
            fs::last_write_time(outPath, *sourceTime);
    }
}

// ---- LWG Packer ----

// Very simple XML meta parser for the specific .meta.xml format:
//   <Canvas><Width>N</Width><Height>N</Height><Items>
//     <Item x="N" y="N" flag="N">name</Item>
//   </Items></Canvas>
static LwgMetadata parseMetaXml(const fs::path& filePath) {
    const auto bytes = readFile(filePath);
    const std::string xml(bytes.begin(), bytes.end());
    convertEncoding(xml, "UTF-8", "UTF-8", true);
    LwgMetadata info;

    auto number = [](const std::string& value, int64_t minimum, int64_t maximum) {
        size_t consumed = 0;
        const auto parsed = std::stoll(value, &consumed);
        if (value.find_first_not_of(" \t\r\n", consumed) != std::string::npos ||
            parsed < minimum || parsed > maximum)
            throw std::runtime_error("Invalid integer in .meta.xml: " + value);
        return parsed;
    };
    if (xml.find("<Canvas>") == std::string::npos ||
        xml.find("</Canvas>") == std::string::npos || xml.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid .meta.xml: missing Canvas or embedded NUL");

    // Parse Width
    auto extractTag = [&](const std::string& tag) -> std::string {
        std::string open = "<" + tag + ">";
        std::string close = "</" + tag + ">";
        auto start = xml.find(open);
        if (start == std::string::npos) return "";
        start += open.size();
        auto end = xml.find(close, start);
        if (end == std::string::npos) return "";
        return xml.substr(start, end - start);
    };

    info.width  = static_cast<uint32_t>(number(extractTag("Width"), 0, UINT32_MAX));
    info.height = static_cast<uint32_t>(number(extractTag("Height"), 0, UINT32_MAX));

    // Parse Items
    auto itemsStart = xml.find("<Items>");
    auto itemsEnd = xml.find("</Items>");
    if (itemsStart == std::string::npos || itemsEnd == std::string::npos || itemsEnd < itemsStart)
        throw std::runtime_error("Invalid .meta.xml: missing <Items>");

    std::string itemsXml = xml.substr(itemsStart, itemsEnd - itemsStart + 8);

    size_t pos = 0;
    while (true) {
        auto itemStart = itemsXml.find("<Item ", pos);
        if (itemStart == std::string::npos) break;
        auto itemTagEnd = itemsXml.find(">", itemStart);
        if (itemTagEnd == std::string::npos)
            throw std::runtime_error("Invalid .meta.xml: unterminated Item");
        std::string tagContent = itemsXml.substr(itemStart + 6, itemTagEnd - itemStart - 6);

        auto itemClose = itemsXml.find("</Item>", itemTagEnd);
        if (itemClose == std::string::npos)
            throw std::runtime_error("Invalid .meta.xml: unterminated Item");
        std::string name = itemsXml.substr(itemTagEnd + 1, itemClose - itemTagEnd - 1);

        LwgMetadataEntry me;
        me.name = xmlName(name, false);

        auto getAttr = [&](const std::string& attr) -> std::string {
            auto ap = tagContent.find(attr + "=\"");
            if (ap == std::string::npos) return "0";
            ap += attr.size() + 2;
            auto ae = tagContent.find("\"", ap);
            if (ae == std::string::npos)
                throw std::runtime_error("Invalid .meta.xml: unterminated attribute");
            return tagContent.substr(ap, ae - ap);
        };

        me.x    = static_cast<int32_t>(number(getAttr("x"), INT32_MIN, INT32_MAX));
        me.y    = static_cast<int32_t>(number(getAttr("y"), INT32_MIN, INT32_MAX));
        me.flag = static_cast<uint8_t>(number(getAttr("flag"), 0, UINT8_MAX));
        me.empty = number(getAttr("empty"), 0, 1) == 1;

        info.entries.push_back(me);
        pos = itemClose + 7;
    }
    return info;
}

LwgMetadata LwgPacker::readMetadata(const fs::path& directory) {
    const auto metaPath = findLwgMetadata(directory);
    if (metaPath.empty())
        throw std::runtime_error("No .meta.xml found in directory. "
                                 "Extract LWG first to generate one.");
    return parseMetaXml(metaPath);
}

std::vector<uint8_t> LwgPacker::pack(
    const std::string& dirPath, const std::string& encoding,
    const std::set<std::string>& excludedPaths) {
    const auto metaPath = findLwgMetadata(dirPath);
    if (metaPath.empty())
        throw std::runtime_error("No .meta.xml found in directory. "
                                 "Extract LWG first to generate one.");

    const auto meta = readMetadata(dirPath);
    if (meta.entries.empty())
        throw std::runtime_error("No entries found in .meta.xml: " + metaPath.string());

    // Read file data for each entry
    struct PackEntry {
        LwgMetadataEntry meta;
        std::vector<uint8_t> data;
    };
    std::vector<PackEntry> packEntries;

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_symlink() && entry.is_regular_file() &&
            isPackableFile(entry.path().string()) &&
            excludedPaths.count(normalized(entry.path())) == 0)
            files.push_back(entry.path());
    }
    static constexpr std::array<const char*, 8> extensionOrder = {
        ".wcg", ".msk", ".lim", ".gsc", ".wav", ".xml", ".lwg", ".xfl"
    };
    auto extensionRank = [&](const fs::path& path) {
        const auto ext = lower(path.extension().string());
        auto it = std::find(extensionOrder.begin(), extensionOrder.end(), ext);
        return std::distance(extensionOrder.begin(), it);
    };
    std::sort(files.begin(), files.end(), [&](const fs::path& a, const fs::path& b) {
        auto aRank = extensionRank(a), bRank = extensionRank(b);
        if (aRank != bRank) return aRank < bRank;
        return lower(a.filename().string()) < lower(b.filename().string());
    });

    for (const auto& me : meta.entries) {
        PackEntry pe;
        pe.meta = me;

        if (me.empty) {
            packEntries.push_back(std::move(pe));
            continue;
        }

        // Extraction replaces filesystem-reserved characters, while metadata
        // retains the actual engine layer name. Resolve the extracted filename.
        auto wanted = lower(sanitizeFilename(me.name));
        auto file = std::find_if(files.begin(), files.end(), [&](const fs::path& path) {
            return lower(path.filename().string()) == wanted;
        });
        if (file == files.end()) {
            file = std::find_if(files.begin(), files.end(), [&](const fs::path& path) {
                return lower(path.stem().string()) == wanted;
            });
        }

        if (file != files.end()) {
            pe.data = readFile(*file);
        } else {
            // Entry with no file - possibly a string entry, still include with empty data
            pe.data.clear();
        }
        packEntries.push_back(std::move(pe));
    }

    if (std::none_of(packEntries.begin(), packEntries.end(),
                     [](const PackEntry& entry) { return !entry.data.empty(); }))
        throw std::runtime_error("No packable files referenced by .meta.xml: " +
                                 dirPath);

    // ---- Build LWG binary ----
    std::vector<uint8_t> out;

    auto writeU32 = [&](uint32_t v) {
        out.push_back(v & 0xFF); out.push_back((v>>8) & 0xFF);
        out.push_back((v>>16) & 0xFF); out.push_back((v>>24) & 0xFF);
    };
    auto writeI32 = [&](int32_t v) { writeU32(static_cast<uint32_t>(v)); };

    // Magic
    out.push_back('L'); out.push_back('G'); out.push_back(0x01); out.push_back(0x00);

    // Height then Width (matching original format)
    writeU32(meta.height);
    writeU32(meta.width);
    writeU32(static_cast<uint32_t>(packEntries.size()));
    writeU32(0); // unknown, zeros

    // Calculate table size
    uint32_t tableSize = 0;
    for (const auto& pe : packEntries) {
        auto encoded = encodeName(pe.meta.name, encoding);
        if (encoded.size() > 255)
            throw std::runtime_error("LWG filename exceeds 255 encoded bytes: " + pe.meta.name);
        tableSize += static_cast<uint32_t>(9 + 4 + 4 + 1 + encoded.size());
    }
    writeU32(tableSize);

    // Write table entries
    uint32_t dataOffset = 0;
    for (const auto& pe : packEntries) {
        writeI32(pe.meta.x);
        writeI32(pe.meta.y);
        out.push_back(pe.meta.flag);
        writeU32(dataOffset);
        writeU32(static_cast<uint32_t>(pe.data.size()));

        auto encoded = encodeName(pe.meta.name, encoding);
        out.push_back(static_cast<uint8_t>(encoded.size()));
        out.insert(out.end(), encoded.begin(), encoded.end());

        dataOffset += static_cast<uint32_t>(pe.data.size());
    }

    // Total payload length, not padding (e.g. CNMI compane.lwg).
    writeU32(dataOffset);

    // Write file data
    for (const auto& pe : packEntries) {
        out.insert(out.end(), pe.data.begin(), pe.data.end());
    }

    return out;
}

void LwgPacker::packToFile(const std::string& dirPath, const std::string& outputPath,
                            const std::string& encoding) {
    auto data = pack(dirPath, encoding);
    writeFileIfChanged(outputPath, data);
}

} // namespace liarsoft
