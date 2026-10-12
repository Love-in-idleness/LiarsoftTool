#include "gui_common.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

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

std::string nativePathFromUtf8(const std::string& path) {
#ifdef _WIN32
    if (path.empty()) return path;
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        path.c_str(), -1, nullptr, 0);
    if (!size) throw std::runtime_error("Invalid UTF-8 file path");
    std::vector<wchar_t> wide(size);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1,
        wide.data(), size);
    const bool utf8 = GetACP() == CP_UTF8;
    BOOL substituted = FALSE;
    const auto bytes = WideCharToMultiByte(CP_ACP,
        utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS,
        wide.data(), -1, nullptr, 0, nullptr, utf8 ? nullptr : &substituted);
    if (!bytes || substituted)
        throw std::runtime_error("File path is not representable in the Windows system code page; move it to an ASCII path");
    std::string result(bytes, '\0');
    if (!WideCharToMultiByte(CP_ACP,
            utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS,
            wide.data(), -1, &result[0], bytes, nullptr,
            utf8 ? nullptr : &substituted) || substituted)
        throw std::runtime_error("Cannot convert file path to the Windows system code page");
    result.pop_back();
    return result;
#else
    return path;
#endif
}

std::string nativeTextToUtf8(const std::string& text) {
#ifdef _WIN32
    if (text.empty()) return text;
    const auto size = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
    if (!size) throw std::runtime_error("Cannot decode Windows file path or diagnostic");
    std::vector<wchar_t> wide(size);
    MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, wide.data(), size);
    const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        wide.data(), -1, nullptr, 0, nullptr, nullptr);
    if (!bytes) throw std::runtime_error("Cannot encode Windows file path or diagnostic as UTF-8");
    std::string result(bytes, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), -1,
            &result[0], bytes, nullptr, nullptr))
        throw std::runtime_error("Cannot encode Windows file path or diagnostic as UTF-8");
    result.pop_back();
    return result;
#else
    return text;
#endif
}

std::string formatDiagnostics(const std::vector<ConversionDiagnostic>& diagnostics) {
    if (diagnostics.empty()) return "";
    const auto errors = std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const ConversionDiagnostic& diagnostic) { return diagnostic.error; });
    std::string report = "LiarsoftTool conversion diagnostics\nErrors: " +
        std::to_string(errors) + "\nWarnings: " +
        std::to_string(diagnostics.size() - errors) + "\n";
    for (const auto& diagnostic : diagnostics) {
        report += diagnostic.error ? "\n[Error]\n" : "\n[Warning]\n";
        report += "Input: " + diagnostic.inputPath + "\nOutput: " +
            diagnostic.outputPath + "\n" + diagnostic.message + "\n";
    }
    return report;
}

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
    if (ext == ".wcg") return (base / (stem + ".png")).string();
    if (ext == ".lim") return (base / (stem + ".webp")).string();
    if (ext == ".webp") return (base / (stem + ".lim")).string();
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
    if (ext == ".lim") return "LIM -> WebP";
    if (ext == ".webp") return "WebP -> LIM";
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
           ext == ".jpeg" || ext == ".bmp" || ext == ".webp" || fs::is_directory(path);
}

std::vector<std::string> convert(const std::string& inputPath,
                                 const std::string& outputPath,
                                 const ConversionOptions& options) {
    if (options.workers > liarsoft::MaxConversionWorkers)
        throw std::runtime_error("Worker count must be between 0 and 64");
    const std::string ext = extension(inputPath);
    std::vector<std::string> warnings;

    if (fs::is_directory(inputPath)) {
        if (options.unpackOnly)
            warnings = liarsoft::unpackDirectoryRecursively(
                inputPath, options.encoding, options.gscToTsc, options.workers);
        else
            warnings = liarsoft::packDirectoryToFile(
                inputPath, outputPath, options.encoding, options.recursive,
                options.vorbisInWav, options.workers);
    } else if (ext == ".gsc") {
        if (options.gscToTsc)
            warnings = liarsoft::decompileGscToFile(inputPath, outputPath, options.encoding);
        else {
            liarsoft::TransFile::fromGsc(
                liarsoft::GscFile::fromFile(inputPath, options.encoding)).save(outputPath);
            liarsoft::copyModificationTime(inputPath, outputPath);
        }
    } else if (ext == ".tsc") {
        liarsoft::restoreGscFromTscFile(inputPath, outputPath, options.encoding);
    } else if (ext == ".txt") {
        const std::string reference = options.referencePath.empty()
            ? replaceExtension(inputPath, ".gsc") : options.referencePath;
        liarsoft::TransFile::fromFile(inputPath)
            .toGsc(reference, options.encoding).save(outputPath);
    } else if (ext == ".xfl") {
        liarsoft::XflArchive::fromFile(inputPath, options.encoding)
            .extractToDirectory(outputPath, fs::last_write_time(inputPath));
        if (options.recursive)
            warnings = liarsoft::unpackDirectoryRecursively(
                outputPath, options.encoding, options.gscToTsc, options.workers);
    } else if (ext == ".lwg") {
        const auto archive = liarsoft::LwgDecoder::decode(
            liarsoft::readFile(inputPath), options.encoding);
        liarsoft::LwgDecoder::extractToDirectory(
            archive, outputPath, options.encoding, fs::last_write_time(inputPath));
        if (options.recursive)
            warnings = liarsoft::unpackDirectoryRecursively(
                outputPath, options.encoding, options.gscToTsc, options.workers);
    } else if (ext == ".wcg") {
        liarsoft::wcgSavePng(liarsoft::wcgDecode(liarsoft::readFile(inputPath)), outputPath);
        liarsoft::copyModificationTime(inputPath, outputPath);
    } else if (ext == ".lim") {
        liarsoft::limSaveWebp(liarsoft::limDecode(liarsoft::readFile(inputPath)), outputPath);
        liarsoft::copyModificationTime(inputPath, outputPath);
    } else if (ext == ".webp") {
        liarsoft::writeFileIfChanged(outputPath,
            liarsoft::limEncode(liarsoft::webpDecode(liarsoft::readFile(inputPath))));
    } else if (ext == ".wav") {
        liarsoft::WavOggExtractor::extractToFile(inputPath, outputPath);
    } else if (ext == ".ogg") {
        liarsoft::WavOggExtractor::convertToFile(inputPath, outputPath, options.vorbisInWav);
    } else if (ext == ".png" || ext == ".jpg" ||
               ext == ".jpeg" || ext == ".bmp") {
        int width, height, channels;
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
            stbi_load(inputPath.c_str(), &width, &height, &channels, 4), stbi_image_free);
        if (!pixels) throw std::runtime_error("Failed to load image");
        const auto data = liarsoft::wcgEncode(
            pixels.get(), static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        liarsoft::writeFileIfChanged(outputPath, data);
    } else if (ext == ".exe") {
        liarsoft::exeConvertFile(inputPath, outputPath, options.encoding);
    } else {
        throw std::runtime_error("Unsupported format");
    }

    return warnings;
}

} // namespace liarsoft::gui
