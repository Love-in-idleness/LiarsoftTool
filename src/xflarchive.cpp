#include "xflarchive.h"
#include "bigendian.h"
#include "gscfile.h"
#include "gsc_decompiler.h"
#include "fileio.h"
#include "lim_decoder.h"
#include "lwg_decoder.h"
#include "stb_image.h"
#include "transfile.h"
#include "wav_ogg.h"
#include "wcg_decoder.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <set>
#include "encoding.h"
#include <regex>

namespace fs = std::filesystem;

namespace liarsoft {

static std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool isPackableFile(const std::string& path) {
    static constexpr std::array<const char*, 8> extensions = {
        ".lim", ".wcg", ".gsc", ".wav", ".xml", ".lwg", ".xfl", ".msk"
    };
    const auto ext = lower(fs::path(path).extension().string());
    return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

static fs::path metaFile(const fs::path& directory) {
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_symlink() && entry.is_regular_file() &&
            lower(entry.path().filename().string()) == ".meta.xml")
            return entry.path();
    }
    return {};
}

bool isLwgDirectory(const std::string& path) {
    return fs::is_directory(path) && !metaFile(path).empty();
}

bool matchesOperationMode(const std::string& path, bool packOnly,
                          bool unpackOnly, bool recursive) {
    if (!packOnly && !unpackOnly) return true;
    if (packOnly && unpackOnly) return false;
    const bool directory = fs::is_directory(path);
    const auto ext = lower(fs::path(path).extension().string());
    const bool packing = directory || ext == ".tsc" || ext == ".txt" || ext == ".ogg" ||
                         ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                         ext == ".bmp";
    const bool unpacking = (directory && recursive) ||
                           (!directory && (ext == ".xfl" || ext == ".lwg" ||
                           ext == ".gsc" || ext == ".wcg" || ext == ".lim" ||
                           ext == ".wav"));
    return (!packOnly || packing) && (!unpackOnly || unpacking);
}

static std::string normalized(const fs::path& path) {
    return fs::absolute(path).lexically_normal().string();
}

static std::vector<uint8_t> readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open: " + path.string());
    in.seekg(0, std::ios::end);
    size_t size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    in.read(reinterpret_cast<char*>(data.data()), size);
    if (!in && size != 0) throw std::runtime_error("Cannot read: " + path.string());
    return data;
}

static fs::path findFile(const fs::path& directory, const std::string& stem,
                         const std::string& extension) {
    const auto wantedStem = lower(stem);
    const auto wantedExt = lower(extension);
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_symlink() && entry.is_regular_file() &&
            lower(entry.path().stem().string()) == wantedStem &&
            lower(entry.path().extension().string()) == wantedExt)
            return entry.path();
    }
    return {};
}

static fs::path outputFile(const fs::path& source, const std::string& extension) {
    auto existing = findFile(source.parent_path(), source.stem().string(), extension);
    return existing.empty()
        ? source.parent_path() / (source.stem().string() + extension)
        : existing;
}

static std::vector<fs::path> editableFiles(const fs::path& directory) {
    static constexpr std::array<const char*, 7> extensions = {
        ".tsc", ".txt", ".ogg", ".png", ".jpg", ".jpeg", ".bmp"
    };
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_symlink() && entry.is_regular_file()) {
            auto ext = lower(entry.path().extension().string());
            if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end())
                files.push_back(entry.path());
        }
    }
    auto rank = [](const fs::path& path) {
        const auto ext = lower(path.extension().string());
        if (ext == ".tsc") return 0;
        if (ext == ".txt") return 1;
        if (ext == ".ogg") return 2;
        if (ext == ".png") return 3;
        if (ext == ".jpg") return 4;
        if (ext == ".jpeg") return 5;
        return 6;
    };
    std::sort(files.begin(), files.end(), [&](const fs::path& a, const fs::path& b) {
        const auto aStem = lower(a.stem().string()), bStem = lower(b.stem().string());
        if (aStem != bStem) return aStem < bStem;
        return rank(a) < rank(b);
    });
    return files;
}

static void prepareDirectoryForPacking(const fs::path& directory,
                                       const std::string& encoding,
                                       std::set<std::string>& excludedPaths,
                                       std::vector<std::string>& warnings) {
    std::set<std::string> convertedTargets;
    for (const auto& source : editableFiles(directory)) {
        const auto ext = lower(source.extension().string());
        fs::path target;
        if (ext == ".tsc" || ext == ".txt") target = outputFile(source, ".gsc");
        else if (ext == ".ogg") target = outputFile(source, ".wav");
        else target = outputFile(source, ".wcg");

        const auto targetKey = normalized(target);
        if (convertedTargets.count(targetKey)) {
            warnings.push_back("Skipped duplicate source '" + source.string() +
                               "' for '" + target.string() + "'");
            continue;
        }

        try {
            if (ext == ".tsc") {
                restoreGscFromTscFile(source.string(), target.string());
            } else if (ext == ".txt") {
                if (!fs::is_regular_file(target))
                    throw std::runtime_error("same-name reference GSC not found");
                TransFile::fromFile(source.string()).toGsc(target.string(), encoding)
                    .save(target.string());
            } else if (ext == ".ogg") {
                if (!fs::is_regular_file(target))
                    throw std::runtime_error("same-name WAV template not found");
                WavOggExtractor::embedToFile(source.string(), target.string(),
                                             target.string());
            } else {
                int width, height, channels;
                unsigned char* pixels = stbi_load(source.string().c_str(), &width,
                                                  &height, &channels, 4);
                if (!pixels)
                    throw std::runtime_error("failed to load image");
                std::vector<uint8_t> wcg;
                try {
                    wcg = wcgEncode(pixels, static_cast<uint32_t>(width),
                                    static_cast<uint32_t>(height));
                } catch (...) {
                    stbi_image_free(pixels);
                    throw;
                }
                stbi_image_free(pixels);

                auto lim = findFile(directory, source.stem().string(), ".lim");
                fs::path oldLim;
                if (!lim.empty()) {
                    oldLim = lim.string() + ".old";
                    if (fs::exists(oldLim))
                        throw std::runtime_error("backup already exists: " +
                                                 oldLim.string());
                    fs::rename(lim, oldLim);
                }
                try {
                    writeFileIfChanged(target.string(), wcg);
                } catch (...) {
                    if (!oldLim.empty()) {
                        std::error_code ec;
                        fs::rename(oldLim, lim, ec);
                    }
                    throw;
                }
            }
            convertedTargets.insert(targetKey);
            excludedPaths.erase(targetKey);
        } catch (const std::exception& e) {
            excludedPaths.insert(targetKey);
            warnings.push_back("Skipped '" + source.string() + "': " + e.what());
        }
    }
}

static void packOneDirectory(const fs::path& directory, const fs::path& output,
                             const std::string& encoding,
                             const std::set<std::string>& excludedPaths) {
    if (isLwgDirectory(directory.string())) {
        auto data = LwgPacker::pack(directory.string(), encoding, excludedPaths);
        writeFileIfChanged(output.string(), data);
        return;
    }

    XflArchive archive;
    archive.encoding = encoding;
    archive.addDirectory(directory.string());
    archive.entries.erase(
        std::remove_if(archive.entries.begin(), archive.entries.end(),
                       [&](const XflEntry& entry) {
                           return excludedPaths.count(
                               normalized(directory / entry.fileName)) != 0;
                       }),
        archive.entries.end());
    if (archive.entries.empty())
        throw std::runtime_error("No packable files found in directory: " +
                                 directory.string());
    archive.save(output.string());
}

static void packSubdirectories(const fs::path& directory,
                               const std::string& encoding,
                               std::set<std::string>& excludedPaths,
                               std::vector<std::string>& warnings) {
    std::vector<fs::path> subdirectories;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_symlink() && entry.is_directory())
            subdirectories.push_back(entry.path());
    }
    std::sort(subdirectories.begin(), subdirectories.end());

    for (const auto& subdirectory : subdirectories) {
        packSubdirectories(subdirectory, encoding, excludedPaths, warnings);
        prepareDirectoryForPacking(subdirectory, encoding, excludedPaths, warnings);
        const bool isLwg = isLwgDirectory(subdirectory.string());
        fs::path output = subdirectory;
        output += isLwg ? ".lwg" : ".xfl";
        fs::path obsolete = subdirectory;
        obsolete += isLwg ? ".xfl" : ".lwg";
        excludedPaths.insert(normalized(obsolete));
        try {
            packOneDirectory(subdirectory, output, encoding, excludedPaths);
            excludedPaths.erase(normalized(output));
        } catch (const std::exception& e) {
            excludedPaths.insert(normalized(output));
            warnings.push_back("Skipped " + std::string(isLwg ? "LWG" : "XFL") +
                               " directory '" + subdirectory.string() + "': " +
                               e.what());
        }
    }
}

std::vector<std::string> packDirectoryToFile(
    const std::string& dirPath, const std::string& outputPath,
    const std::string& encoding, bool recursive) {
    if (!fs::is_directory(dirPath))
        throw std::runtime_error("Directory not found: " + dirPath);

    std::vector<std::string> warnings;
    std::set<std::string> excludedPaths;
    excludedPaths.insert(normalized(outputPath));
    if (recursive) {
        packSubdirectories(dirPath, encoding, excludedPaths, warnings);
        prepareDirectoryForPacking(dirPath, encoding, excludedPaths, warnings);
    }
    packOneDirectory(dirPath, outputPath, encoding, excludedPaths);
    return warnings;
}

static void unpackDirectory(const fs::path& directory, const std::string& encoding,
                            unsigned depth, std::vector<std::string>& warnings,
                            bool gscToTsc) {
    // ponytail: depth cap prevents malicious self-nesting; raise it if real
    // archives are ever observed deeper than 32 levels.
    if (depth > 32) {
        warnings.push_back("Stopped unpacking below '" + directory.string() +
                           "': nesting exceeds 32 levels");
        return;
    }

    std::vector<fs::path> archives;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_symlink() || !entry.is_regular_file()) continue;
        auto ext = lower(entry.path().extension().string());
        if (ext == ".xfl" || ext == ".lwg") archives.push_back(entry.path());
    }
    std::sort(archives.begin(), archives.end());

    std::set<std::string> extractedTargets;
    for (const auto& archivePath : archives) {
        auto target = archivePath.parent_path() / archivePath.stem();
        if (!extractedTargets.insert(normalized(target)).second) {
            warnings.push_back("Skipped duplicate archive target: " +
                               archivePath.string());
            continue;
        }
        try {
            if (lower(archivePath.extension().string()) == ".xfl") {
                XflArchive::fromFile(archivePath.string(), encoding)
                    .extractToDirectory(target.string());
            } else {
                auto archive = LwgDecoder::decode(readFile(archivePath), encoding);
                LwgDecoder::extractToDirectory(archive, target.string(), encoding);
            }
        } catch (const std::exception& e) {
            warnings.push_back("Skipped archive '" + archivePath.string() +
                               "': " + e.what());
        }
    }

    std::vector<fs::path> subdirectories;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_symlink() && entry.is_directory())
            subdirectories.push_back(entry.path());
    }
    std::sort(subdirectories.begin(), subdirectories.end());
    for (const auto& subdirectory : subdirectories)
        unpackDirectory(subdirectory, encoding, depth + 1, warnings, gscToTsc);

    struct Conversion { fs::path source; int rank; };
    std::vector<Conversion> conversions;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.is_symlink() || !entry.is_regular_file()) continue;
        const auto ext = lower(entry.path().extension().string());
        int rank = ext == ".gsc" ? 0 : ext == ".wcg" ? 1 :
                   ext == ".lim" ? 2 : ext == ".wav" ? 3 : -1;
        if (rank >= 0) conversions.push_back({entry.path(), rank});
    }
    std::sort(conversions.begin(), conversions.end(), [](const Conversion& a,
                                                          const Conversion& b) {
        auto aStem = lower(a.source.stem().string());
        auto bStem = lower(b.source.stem().string());
        if (aStem != bStem) return aStem < bStem;
        return a.rank < b.rank;
    });

    std::set<std::string> convertedTargets;
    for (const auto& conversion : conversions) {
        const auto& source = conversion.source;
        const auto ext = lower(source.extension().string());
        auto target = outputFile(source, ext == ".gsc" ?
                                         (gscToTsc ? ".tsc" : ".txt") :
                                         ext == ".wav" ? ".ogg" : ".png");
        if (!convertedTargets.insert(normalized(target)).second) {
            warnings.push_back("Skipped duplicate conversion target: " +
                               source.string());
            continue;
        }
        try {
            if (ext == ".gsc") {
                if (gscToTsc) {
                    decompileGscToFile(source.string(), target.string(), encoding);
                } else {
                    auto gsc = GscFile::fromFile(source.string(), encoding);
                    TransFile::fromGsc(gsc).save(target.string());
                }
            } else if (ext == ".wcg") {
                wcgSavePng(wcgDecode(readFile(source)), target.string());
            } else if (ext == ".lim") {
                limSavePng(limDecode(readFile(source)), target.string());
            } else {
                WavOggExtractor::extractToFile(source.string(), target.string());
            }
        } catch (const std::exception& e) {
            warnings.push_back("Skipped '" + source.string() + "': " + e.what());
        }
    }
}

std::vector<std::string> unpackDirectoryRecursively(
    const std::string& dirPath, const std::string& encoding, bool gscToTsc) {
    if (!fs::is_directory(dirPath))
        throw std::runtime_error("Directory not found: " + dirPath);
    std::vector<std::string> warnings;
    unpackDirectory(dirPath, encoding, 0, warnings, gscToTsc);
    return warnings;
}


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
        if (!entry.is_symlink() && entry.is_regular_file() &&
            isPackableFile(entry.path().string())) {
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
    writeFileIfChanged(path, toBytes());
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
        // Identical content already on disk is left untouched (mtime preserved).
        writeFileIfChanged(outPath.string(), entry.data);
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
