#include "gui_common.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "exe_patch.h"
#include "fileio.h"
#include "gsc_decompiler.h"
#include "gscfile.h"
#include "lim_decoder.h"
#include "lwg_decoder.h"
#include "stb_image.h"
#include "transfile.h"
#include "wav_ogg.h"
#include "wcg_decoder.h"
#include "xflarchive.h"

namespace fs = std::filesystem;

namespace liarsoft::gui {

std::string extension(const std::string& path) {
    const auto pos = path.rfind('.');
    if (pos == std::string::npos) return "";
    std::string result = path.substr(pos);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::string replaceExtension(const std::string& path,
                             const std::string& newExtension) {
    const auto pos = path.rfind('.');
    if (pos == std::string::npos) return path + newExtension;
    return path.substr(0, pos) + newExtension;
}

std::string guessOutput(const std::string& inputPath,
                        const std::string& outputDirectory,
                        bool gscToTsc,
                        const std::string& encoding) {
    const std::string ext = extension(inputPath);
    const fs::path input(inputPath);
    const fs::path base = outputDirectory.empty()
        ? input.parent_path() : fs::path(outputDirectory);
    const std::string stem = input.stem().string();

    if (fs::is_directory(input))
        return (base / (input.filename().string() +
                        (liarsoft::isLwgDirectory(inputPath) ? ".lwg" : ".xfl"))).string();
    if (ext == ".gsc") return (base / (stem + (gscToTsc ? ".tsc" : ".txt"))).string();
    if (ext == ".tsc" || ext == ".txt") return (base / (stem + ".gsc")).string();
    if (ext == ".xfl" || ext == ".lwg") return (base / stem).string();
    if (ext == ".wcg" || ext == ".lim") return (base / (stem + ".png")).string();
    if (ext == ".wav") return (base / (stem + ".ogg")).string();
    if (ext == ".ogg") return (base / (stem + ".wav")).string();
    if (ext == ".exe") {
        const std::string suffix = encoding == "GBK" ? ".gbk.exe" :
            encoding == "CP1251" ? ".cp1251.exe" : ".sjis.exe";
        return (base / (stem + suffix)).string();
    }
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp")
        return (base / (stem + ".wcg")).string();
    return (base / input.filename()).string();
}

std::string guessType(const std::string& path,
                      bool gscToTsc,
                      const std::string& encoding) {
    const std::string ext = extension(path);
    if (ext == ".gsc") return gscToTsc ? "GSC -> TSC" : "GSC -> TXT";
    if (ext == ".tsc") return "TSC -> GSC";
    if (ext == ".txt") return "TXT -> GSC";
    if (ext == ".xfl") return "XFL -> DIR";
    if (ext == ".lwg") return "LWG -> DIR";
    if (ext == ".wcg") return "WCG -> PNG";
    if (ext == ".lim") return "LIM -> PNG";
    if (ext == ".wav") return "WAV -> OGG";
    if (ext == ".ogg") return "OGG -> WAV";
    if (ext == ".exe") return "EXE -> " + encoding;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp")
        return "IMG -> WCG";
    if (fs::is_directory(path)) return "DIR -> XFL/LWG";
    return "?";
}

bool isSupported(const std::string& path) {
    const std::string ext = extension(path);
    return ext == ".gsc" || ext == ".tsc" || ext == ".txt" ||
           ext == ".xfl" || ext == ".lwg" || ext == ".wcg" ||
           ext == ".lim" || ext == ".wav" || ext == ".ogg" ||
           ext == ".exe" || ext == ".png" || ext == ".jpg" ||
           ext == ".jpeg" || ext == ".bmp" || fs::is_directory(path);
}

static std::vector<uint8_t> readBinary(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Failed to open " + path);
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    if (size < 0) throw std::runtime_error("Failed to read " + path);
    stream.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!data.empty() && !stream.read(reinterpret_cast<char*>(data.data()), size))
        throw std::runtime_error("Failed to read " + path);
    return data;
}

std::vector<std::string> convert(const std::string& inputPath,
                                 const std::string& outputPath,
                                 const ConversionOptions& options) {
    const std::string ext = extension(inputPath);
    std::vector<std::string> warnings;

    if (fs::is_directory(inputPath)) {
        if (options.unpackOnly)
            warnings = liarsoft::unpackDirectoryRecursively(
                inputPath, options.encoding, options.gscToTsc);
        else
            warnings = liarsoft::packDirectoryToFile(
                inputPath, outputPath, options.encoding, options.recursive);
    } else if (ext == ".gsc") {
        if (options.gscToTsc)
            liarsoft::decompileGscToFile(inputPath, outputPath, options.encoding);
        else
            liarsoft::TransFile::fromGsc(
                liarsoft::GscFile::fromFile(inputPath, options.encoding)).save(outputPath);
    } else if (ext == ".tsc") {
        liarsoft::restoreGscFromTscFile(inputPath, outputPath, options.encoding);
    } else if (ext == ".txt") {
        const std::string reference = options.referencePath.empty()
            ? replaceExtension(inputPath, ".gsc") : options.referencePath;
        liarsoft::TransFile::fromFile(inputPath)
            .toGsc(reference, options.encoding).save(outputPath);
    } else if (ext == ".xfl") {
        liarsoft::XflArchive::fromFile(inputPath, options.encoding)
            .extractToDirectory(outputPath);
        if (options.recursive)
            warnings = liarsoft::unpackDirectoryRecursively(
                outputPath, options.encoding, options.gscToTsc);
    } else if (ext == ".lwg") {
        const auto archive = liarsoft::LwgDecoder::decode(
            readBinary(inputPath), options.encoding);
        liarsoft::LwgDecoder::extractToDirectory(
            archive, outputPath, options.encoding);
        if (options.recursive)
            warnings = liarsoft::unpackDirectoryRecursively(
                outputPath, options.encoding, options.gscToTsc);
    } else if (ext == ".wcg") {
        liarsoft::wcgSavePng(liarsoft::wcgDecode(readBinary(inputPath)), outputPath);
    } else if (ext == ".lim") {
        liarsoft::limSavePng(liarsoft::limDecode(readBinary(inputPath)), outputPath);
    } else if (ext == ".wav") {
        liarsoft::WavOggExtractor::extractToFile(inputPath, outputPath);
    } else if (ext == ".ogg") {
        const std::string reference = options.referencePath.empty()
            ? replaceExtension(inputPath, ".wav") : options.referencePath;
        liarsoft::WavOggExtractor::embedToFile(inputPath, reference, outputPath);
    } else if (ext == ".png" || ext == ".jpg" ||
               ext == ".jpeg" || ext == ".bmp") {
        int width, height, channels;
        unsigned char* pixels = stbi_load(
            inputPath.c_str(), &width, &height, &channels, 4);
        if (!pixels) throw std::runtime_error("Failed to load image");
        const auto data = liarsoft::wcgEncode(
            pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        stbi_image_free(pixels);
        liarsoft::writeFileIfChanged(outputPath, data);
    } else if (ext == ".exe") {
        liarsoft::exeConvertFile(inputPath, outputPath, options.encoding);
    } else {
        throw std::runtime_error("Unsupported format");
    }

    return warnings;
}

} // namespace liarsoft::gui
