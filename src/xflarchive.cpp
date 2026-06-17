#include "xflarchive.h"
#include "bigendian.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include "encoding.h"
#include <regex>

namespace fs = std::filesystem;

namespace liarsoft {


static std::vector<uint8_t> encodeFileName(const std::string& utf8Name,
                                           const std::string& encoding) {
    std::string encoded = convertEncoding(utf8Name, "UTF-8", encoding);
    return std::vector<uint8_t>(encoded.begin(), encoded.end());
}

static std::string decodeFileName(const std::vector<uint8_t>& raw,
                                  const std::string& encoding) {
    // Filter out null padding
    std::vector<uint8_t> filtered;
    for (auto b : raw) {
        if (b == 0) break;
        filtered.push_back(b);
    }
    if (filtered.empty()) return {};
    std::string s(filtered.begin(), filtered.end());
    return convertEncoding(s, encoding, "UTF-8");
}

// ---- Natural sort helper ----

static bool naturalCompare(const std::string& a, const std::string& b) {
    // Try numeric prefix match first (e.g. "0001.gsc")
    std::regex numPattern(R"(^(\d{4})\..*)");
    std::smatch ma, mb;
    bool aNum = std::regex_match(a, ma, numPattern);
    bool bNum = std::regex_match(b, mb, numPattern);
    if (aNum && bNum) {
        return std::stoi(ma[1].str()) < std::stoi(mb[1].str());
    }
    // Fall back to natural sort with embedded numbers
    std::regex re(R"((\d+)|(\D+))");
    auto itA = std::sregex_iterator(a.begin(), a.end(), re);
    auto itB = std::sregex_iterator(b.begin(), b.end(), re);
    auto end = std::sregex_iterator();

    while (itA != end && itB != end) {
        std::string pa = itA->str();
        std::string pb = itB->str();
        ++itA; ++itB;

        if (std::isdigit(pa[0]) && std::isdigit(pb[0])) {
            int na = std::stoi(pa);
            int nb = std::stoi(pb);
            if (na != nb) return na < nb;
        } else if (pa != pb) {
            return pa < pb;
        }
    }
    return itA == end && itB != end;
}

// ---- Factory methods ----

XflArchive XflArchive::fromFile(const std::string& path, const std::string& enc) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    stream.seekg(0, std::ios::end);
    size_t size = stream.tellg();
    stream.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(size);
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
    return fromBytes(bytes, enc);
}

XflArchive XflArchive::fromBytes(const std::vector<uint8_t>& data, const std::string& enc) {
    XflArchive archive;
    archive.encoding = enc;
    BigEndianReader reader(data); // Note: reader class now uses LE (despite name)

    // Read header
    uint32_t magic = static_cast<uint32_t>(reader.readInt32());
    if (magic != 0x0001424C) {
        throw std::runtime_error("Not a valid XFL archive (bad magic number)");
    }

    int32_t tableSize = reader.readInt32();
    int32_t fileCount = reader.readInt32();

    if (fileCount < 0 || tableSize < 0) {
        throw std::runtime_error("Corrupt XFL archive header");
    }

    // Read chunk table
    for (int32_t i = 0; i < fileCount; ++i) {
        XflEntry entry;

        // Read fixed-width filename (0x20 = 32 bytes)
        std::vector<uint8_t> nameBytes = reader.readBytes(0x20);
        entry.fileName = decodeFileName(nameBytes, enc);

        // Read offset and size
        int32_t offset = reader.readInt32();
        int32_t size = reader.readInt32();

        if (size < 0) {
            throw std::runtime_error("Corrupt XFL entry: negative file size");
        }

        // Read file data (offset is relative to start of data section)
        // The data section starts after header + table
        size_t dataStart = 12 + static_cast<size_t>(tableSize); // 12 = magic + tableSize + fileCount
        if (dataStart + offset + size > data.size()) {
            throw std::runtime_error("Corrupt XFL entry: data offset out of bounds");
        }
        entry.data.assign(data.begin() + static_cast<ptrdiff_t>(dataStart + offset),
                          data.begin() + static_cast<ptrdiff_t>(dataStart + offset + size));

        archive.entries.push_back(std::move(entry));
    }

    return archive;
}

// ---- Pack ----

void XflArchive::addDirectory(const std::string& dirPath) {
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
        throw std::runtime_error("Directory not found: " + dirPath);
    }

    // Collect and sort files
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (fs::is_regular_file(entry.status())) {
            files.push_back(entry.path().filename());
        }
    }

    // Sort: numeric (0001) first, then natural
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        return naturalCompare(a.string(), b.string());
    });

    for (const auto& file : files) {
        XflEntry entry;
        entry.fileName = file.string();

        fs::path fullPath = fs::path(dirPath) / file;
        std::ifstream in(fullPath, std::ios::binary);
        if (!in) {
            throw std::runtime_error("Cannot read file: " + fullPath.string());
        }
        in.seekg(0, std::ios::end);
        size_t sz = in.tellg();
        in.seekg(0, std::ios::beg);
        entry.data.resize(sz);
        in.read(reinterpret_cast<char*>(entry.data.data()), sz);

        entries.push_back(std::move(entry));
    }
}

void XflArchive::save(const std::string& path) const {
    auto bytes = toBytes();
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Cannot write file: " + path);
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::vector<uint8_t> XflArchive::toBytes() const {
    BigEndianWriter writer;

    const uint32_t magic = 0x0001424C;
    const uint32_t entrySize = 0x20 + 4 + 4; // 40 bytes per entry
    uint32_t tableSize = static_cast<uint32_t>(entries.size()) * entrySize;
    uint32_t fileCount = static_cast<uint32_t>(entries.size());

    // Header
    writer.writeInt32(static_cast<int32_t>(magic));
    writer.writeInt32(static_cast<int32_t>(tableSize));
    writer.writeInt32(static_cast<int32_t>(fileCount));

    // Compute cumulative offsets for file data
    uint32_t dataOffset = 0;
    std::vector<uint32_t> offsets;
    for (const auto& entry : entries) {
        offsets.push_back(dataOffset);
        dataOffset += static_cast<uint32_t>(entry.data.size());
    }

    // Write chunk table
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];

        // Encode file name in the archive encoding
        auto nameBytes = encodeFileName(entry.fileName, encoding);

        // Write fixed-width name (0x20 bytes, null-padded)
        if (nameBytes.size() > 0x1F) {
            nameBytes.resize(0x1F); // truncate to 31 bytes
        }
        std::vector<uint8_t> paddedName(0x20, 0);
        std::copy(nameBytes.begin(), nameBytes.end(), paddedName.begin());
        writer.writeBytes(paddedName);

        writer.writeInt32(static_cast<int32_t>(offsets[i]));
        writer.writeInt32(static_cast<int32_t>(entry.data.size()));
    }

    // Write file data
    for (const auto& entry : entries) {
        writer.writeBytes(entry.data);
    }

    return writer.data();
}

// ---- Unpack ----

void XflArchive::extractToDirectory(const std::string& dirPath) const {
    createDirectory(dirPath);

    for (const auto& entry : entries) {
        fs::path outPath = fs::path(dirPath) / entry.fileName;
        std::ofstream out(outPath, std::ios::binary);
        if (!out) {
            throw std::runtime_error("Cannot create file: " + outPath.string());
        }
        out.write(reinterpret_cast<const char*>(entry.data.data()), entry.data.size());
    }
}

void XflArchive::createDirectory(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    if (ec) {
        throw std::runtime_error("Cannot create directory: " + path + " (" + ec.message() + ")");
    }
}

} // namespace liarsoft
