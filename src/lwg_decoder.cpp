#include "lwg_decoder.h"
#include "encoding.h"
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace fs = std::filesystem;

namespace liarsoft {


static std::string encodeName(const std::string& utf8, const std::string& enc) {
    return convertEncoding(utf8, "UTF-8", enc);
}

static std::string decodeName(const std::string& raw, const std::string& enc) {
    return convertEncoding(raw, enc, "UTF-8");
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
        if (data[0] == 0x42 && data[1] == 0x4D) return ".msk"; // "BM"
    }
    return "";
}

// ---- LWG Decoder ----

bool LwgDecoder::isRecognized(const std::vector<uint8_t>& data) {
    if (data.size() < 16) return false;
    return data[0] == 'L' && data[1] == 'G' && data[2] == 0x01 && data[3] == 0x00;
}

LwgDecoder::Archive LwgDecoder::decode(const std::vector<uint8_t>& data,
                                        const std::string& encoding) {
    if (!isRecognized(data))
        throw std::runtime_error("Not a valid LWG archive");

    size_t pos = MAGIC_SIZE;
    auto readU32 = [&]() -> uint32_t {
        uint32_t v = data[pos] | (data[pos+1]<<8) | (data[pos+2]<<16) | (data[pos+3]<<24);
        pos += 4; return v;
    };
    auto readI32 = [&]() -> int32_t {
        uint32_t v = readU32(); return static_cast<int32_t>(v);
    };

    Archive archive;
    // NOTE: original format has Height BEFORE Width!
    archive.height = readU32();
    archive.width  = readU32();

    uint32_t fileCount = readU32();
    pos += 4; // skip unknown
    uint32_t tableSize = readU32();
    size_t dataStart = pos + tableSize + 4;

    for (uint32_t i = 0; i < fileCount; ++i) {
        LwgEntry entry;
        entry.x    = readI32();
        entry.y    = readI32();
        entry.flag = data[pos++];
        uint32_t offset = readU32();
        entry.size = readU32();
        uint8_t nameLen = data[pos++];

        std::string rawName(data.begin() + static_cast<ptrdiff_t>(pos),
                            data.begin() + static_cast<ptrdiff_t>(pos + nameLen));
        pos += nameLen;
        entry.name = decodeName(rawName, encoding);

        size_t fileOff = dataStart + offset;
        if (fileOff + entry.size > data.size())
            throw std::runtime_error("LWG: data out of bounds: " + entry.name);
        entry.data.assign(data.begin() + static_cast<ptrdiff_t>(fileOff),
                          data.begin() + static_cast<ptrdiff_t>(fileOff + entry.size));

        archive.entries.push_back(std::move(entry));
    }
    return archive;
}

void LwgDecoder::extractToDirectory(const Archive& archive, const std::string& dirPath,
                                     const std::string& encoding) {
    std::error_code ec;
    fs::create_directories(dirPath, ec);
    if (ec) throw std::runtime_error("Cannot create directory: " + dirPath);

    // Write .meta.xml
    {
        std::ofstream meta(dirPath + "/.meta.xml");
        meta << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        meta << "<Canvas>\n";
        meta << "  <Width>" << archive.width << "</Width>\n";
        meta << "  <Height>" << archive.height << "</Height>\n";
        meta << "  <Items>\n";
        for (const auto& e : archive.entries) {
            // Write metadata for non-empty entries
            if (!e.data.empty()) {
                meta << "    <Item x=\"" << e.x << "\" y=\"" << e.y
                     << "\" flag=\"" << static_cast<int>(e.flag) << "\">"
                     << e.name << "</Item>\n";
            }
        }
        meta << "  </Items>\n";
        meta << "</Canvas>\n";
    }

    // Extract files
    for (const auto& entry : archive.entries) {
        if (entry.data.empty()) continue;
        std::string ext = guessExt(entry.data);
        fs::path outPath = fs::path(dirPath) / (sanitizeFilename(entry.name) + ext);
        std::ofstream out(outPath, std::ios::binary);
        if (!out) throw std::runtime_error("Cannot create file: " + outPath.string());
        out.write(reinterpret_cast<const char*>(entry.data.data()), entry.data.size());
    }
}

// ---- LWG Packer ----

// Very simple XML meta parser for the specific .meta.xml format:
//   <Canvas><Width>N</Width><Height>N</Height><Items>
//     <Item x="N" y="N" flag="N">name</Item>
//   </Items></Canvas>
struct MetaEntry {
    std::string name;
    int32_t x = 0, y = 0;
    uint8_t flag = 40; // default LWGFlags.Image2
};
struct MetaInfo {
    uint32_t width = 0, height = 0;
    std::vector<MetaEntry> entries;
};

static MetaInfo parseMetaXml(const std::string& filePath) {
    std::ifstream in(filePath);
    if (!in) throw std::runtime_error("Cannot open .meta.xml: " + filePath);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string xml = ss.str();

    MetaInfo info;

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

    info.width  = static_cast<uint32_t>(std::stoul(extractTag("Width")));
    info.height = static_cast<uint32_t>(std::stoul(extractTag("Height")));

    // Parse Items
    auto itemsStart = xml.find("<Items>");
    auto itemsEnd = xml.find("</Items>");
    if (itemsStart == std::string::npos || itemsEnd == std::string::npos)
        throw std::runtime_error("Invalid .meta.xml: missing <Items>");

    std::string itemsXml = xml.substr(itemsStart, itemsEnd - itemsStart + 8);

    size_t pos = 0;
    while (true) {
        auto itemStart = itemsXml.find("<Item ", pos);
        if (itemStart == std::string::npos) break;
        auto itemTagEnd = itemsXml.find(">", itemStart);
        if (itemTagEnd == std::string::npos) break;
        std::string tagContent = itemsXml.substr(itemStart + 6, itemTagEnd - itemStart - 6);

        auto itemClose = itemsXml.find("</Item>", itemTagEnd);
        if (itemClose == std::string::npos) break;
        std::string name = itemsXml.substr(itemTagEnd + 1, itemClose - itemTagEnd - 1);

        MetaEntry me;
        me.name = name;

        auto getAttr = [&](const std::string& attr) -> std::string {
            auto ap = tagContent.find(attr + "=\"");
            if (ap == std::string::npos) return "0";
            ap += attr.size() + 2;
            auto ae = tagContent.find("\"", ap);
            return tagContent.substr(ap, ae - ap);
        };

        me.x    = static_cast<int32_t>(std::stol(getAttr("x")));
        me.y    = static_cast<int32_t>(std::stol(getAttr("y")));
        me.flag = static_cast<uint8_t>(std::stoul(getAttr("flag")));

        info.entries.push_back(me);
        pos = itemClose + 7;
    }
    return info;
}

std::vector<uint8_t> LwgPacker::pack(const std::string& dirPath, const std::string& encoding) {
    std::string metaPath = dirPath + "/.meta.xml";
    if (!fs::exists(metaPath))
        throw std::runtime_error("No .meta.xml found in directory. "
                                 "Extract LWG first to generate one.");

    MetaInfo meta = parseMetaXml(metaPath);

    // Read file data for each entry
    struct PackEntry {
        MetaEntry meta;
        std::vector<uint8_t> data;
    };
    std::vector<PackEntry> packEntries;

    for (const auto& me : meta.entries) {
        PackEntry pe;
        pe.meta = me;

        // Try extensions: .wcg, .msk, .png
        bool found = false;
        for (const auto& ext : {".wcg", ".msk", ".png"}) {
            fs::path fp = fs::path(dirPath) / (me.name + ext);
            if (fs::exists(fp)) {
                std::ifstream in(fp, std::ios::binary);
                in.seekg(0, std::ios::end);
                size_t sz = in.tellg();
                in.seekg(0, std::ios::beg);
                pe.data.resize(sz);
                in.read(reinterpret_cast<char*>(pe.data.data()), sz);
                found = true;
                break;
            }
        }
        if (!found) {
            // Entry with no file - possibly a string entry, still include with empty data
            pe.data.clear();
        }
        packEntries.push_back(std::move(pe));
    }

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

    // Padding u32 (zeros)
    writeU32(0);

    // Write file data
    for (const auto& pe : packEntries) {
        out.insert(out.end(), pe.data.begin(), pe.data.end());
    }

    return out;
}

void LwgPacker::packToFile(const std::string& dirPath, const std::string& outputPath,
                            const std::string& encoding) {
    auto data = pack(dirPath, encoding);
    std::ofstream f(outputPath, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot write: " + outputPath);
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
}

} // namespace liarsoft

