#include "conversion.h"
#include "encoding.h"
#include "xflarchive.h"
#include "update_check.h"
#include "gsc_decompiler.h"
#include "fileio.h"
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <charconv>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

static void printUsage(const char* prog) {
    std::cout << "LiarsoftTool " << liarsoft::CurrentVersion << " - Liar-soft visual novel resource converter\n"
              << "Usage: " << prog << " [options] <input...> [output]\n\n"
              << "Options:\n"
              << "  -e, --encoding <enc>  Text encoding (default: cp932 / Windows-31J)\n"
              << "                        Supported: cp932, gbk, cp1251\n"
              << "  -r, --reference <path> Reference GSC file for TXT injection\n"
              << "  -o, --output <path>    Explicit output file or directory\n"
              << "  -R, --recursive        Recursively pack/unpack and convert resources\n"
              << "  -j, --jobs <0..64>    Resource conversion workers (0=auto, 1=serial)\n"
              << "      --pack-only        Only pack/encode inputs\n"
              << "      --unpack-only      Only unpack/decode inputs\n"
              << "      --gsc-to-tsc       Structured GSC -> TSC output (default)\n"
              << "      --gsc-to-txt       Extract translatable GSC text as TXT\n"
              << "      --vorbis-in-wav    Wrap Ogg in WAV instead of decoding to PCM\n"
              << "      --check-update     Check GitHub for a newer release (no conversion)\n"
              << "      --check-tsc        Check TSC inputs without writing GSC\n"
              << "      --diagnostics=json Machine-readable diagnostics for --check-tsc\n"
              << "  -h, --help            Show this help message\n\n"
              << "Conversion modes:\n"
              << "  .gsc  -> .tsc         Export structured TSC (default)\n"
              << "  .gsc  -> .txt         Extract text with --gsc-to-txt\n"
              << "  .tsc  -> .gsc         Compile structured TSC source into GSC\n"
              << "  .txt  -> .gsc         Pack translated strings back into GSC\n"
              << "  .xfl  -> directory    Unpack XFL archive into a folder\n"
              << "  .lwg  -> directory    Unpack LWG archive into a folder\n"
              << "  .wcg  -> .png         Convert WCG image to PNG\n"
              << "  .lim  -> .webp        Convert LIM image to lossless WebP\n"
              << "  .webp -> .lim         Convert WebP image to LIM\n"
              << "  .wav  -> .ogg         Extract embedded Ogg; retain standard PCM WAV\n"
              << "  .ogg  -> .wav         Decode to 16-bit PCM; --vorbis-in-wav keeps compressed audio\n"
              << "  .png/.jpg/.bmp -> .wcg Convert image to WCG\n"
              << "  .bmp --unpack-only    Normalize CodeX inverse alpha in place (BMP V4)\n"
              << "  directory -> .xfl     Pack a folder into an XFL archive\n"
              << "  directory -> .lwg     Pack folder with .meta.xml into LWG\n"
              << "\n"
              << "Supports wildcards: " << prog << " *.png\n\n"
              << "Examples:\n"
              << "  " << prog << " --check-tsc --diagnostics=json scene.tsc\n"
              << "  " << prog << " -e cp932 scenario.gsc\n"
              << "  " << prog << " --gsc-to-txt -e gbk scenario.gsc output.txt\n"
              << "  " << prog << " -e cp932 -r original.gsc translation.txt\n"
              << "  " << prog << " -e gbk archive.xfl\n"
              << "  " << prog << " image.wcg\n"
              << "  " << prog << " cgview.lwg\n"
              << "  " << prog << " audio.ogg              # decode to PCM WAV, no reference needed\n"
              << "  " << prog << " --vorbis-in-wav audio.ogg  # compressed Vorbis-in-WAV\n"
              << "  " << prog << " -R archive.xfl         # recursive unpack + conversion\n"
              << "  " << prog << " -R ./extracted_dir      # conversion + recursive pack\n"
              << "  " << prog << " 0*.png                  # batch convert all matching PNGs\n"
              << "  " << prog << " -e gbk ./extracted_dir\n"
              << std::endl;
}

static std::string normalizeEncoding(const std::string& enc) {
    return liarsoft::normalizeEncodingName(enc);
}

// Simple wildcard match: supports * and ?
static bool wildMatch(const std::string& text, const std::string& pattern) {
    size_t ti = 0, pi = 0;
    size_t star = std::string::npos, match = 0;
    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == '?' ||
            pattern[pi] == text[ti])) {
            ++ti; ++pi;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            star = pi++;
            match = ti;
        } else if (star != std::string::npos) {
            pi = star + 1;
            ti = ++match;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
}

// Cross-platform glob: expand wildcard pattern using <filesystem>.
static std::vector<std::string> globPattern(const std::string& pattern, bool utf8 = false) {
    std::vector<std::string> results;
    bool hasWild = pattern.find_first_of("*?[") != std::string::npos;

    if (!hasWild) {
        // Literal path — return as-is
        results.push_back(pattern);
        return results;
    }

    // Separate directory and filename pattern
    fs::path p = utf8 ? fs::u8path(pattern) : fs::path(pattern);
    fs::path dir = p.parent_path();
    std::string filePat = utf8 ? p.filename().u8string() : p.filename().string();

    if (dir.empty()) dir = ".";

    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        // Try as literal
        results.push_back(pattern);
        return results;
    }

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file() && !entry.is_directory()) continue;
        std::string name = utf8 ? entry.path().filename().u8string() : entry.path().filename().string();
        if (wildMatch(name, filePat))
            results.push_back(utf8 ? entry.path().u8string() : entry.path().string());
    }

    if (results.empty() && !hasWild)
        results.push_back(pattern);
    return results;
}

static std::string jsonString(const std::string& value) {
    std::string result = "\"";
    const char* hex = "0123456789abcdef";
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
        else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += char(c);
    }
    return result + '"';
}

static int checkInputs(const std::vector<std::string>& inputs, const std::string& encoding, bool json) {
    bool failed = false, first = true;
    if (json) std::cout << "{\"diagnostics\":[";
    for (const auto& pattern : inputs) {
        auto paths = globPattern(pattern, true);
        if (paths.empty()) paths.push_back(pattern);
        for (const auto& path : paths) {
            std::vector<liarsoft::TscDiagnostic> diagnostics;
            try {
                if (liarsoft::conversion::extension(path) != ".tsc")
                    throw std::runtime_error("--check-tsc requires a TSC file");
                const auto bytes = liarsoft::readFile(fs::u8path(path));
                diagnostics = liarsoft::checkTsc(std::string(bytes.begin(), bytes.end()), encoding);
            } catch (const std::exception& error) {
                diagnostics.push_back({"error", error.what(), 0});
            }
            for (const auto& diagnostic : diagnostics) {
                failed = true;
                if (json) {
                    if (!first) std::cout << ',';
                    first = false;
                    std::cout << "{\"file\":" << jsonString(path)
                              << ",\"severity\":" << jsonString(diagnostic.severity)
                              << ",\"message\":" << jsonString(diagnostic.message)
                              << ",\"line\":" << (diagnostic.line ? std::to_string(diagnostic.line) : "null")
                              << ",\"column\":null}";
                } else {
                    std::cerr << path << (diagnostic.line ? ":" + std::to_string(diagnostic.line) : "")
                              << ": " << diagnostic.severity << ": " << diagnostic.message << '\n';
                }
            }
        }
    }
    if (json) std::cout << "]}\n";
    return failed ? 1 : 0;
}

// ---- Core processing of a single input ----
// Returns true on success.
static bool processOne(const std::string& inputPath,
                       const std::string& outputPath,
                       const std::string& encoding,
                       const std::string& referencePath,
                       bool recursive, bool unpackOnly, bool gscToTsc,
                       bool vorbisInWav, unsigned workers)
{
    if (!liarsoft::conversion::isSupported(inputPath)) {
        std::cerr << "Error: unsupported file extension '"
                  << liarsoft::conversion::extension(inputPath)
                  << "' for: " << inputPath << std::endl;
        return false;
    }
    const auto out = outputPath.empty()
        ? liarsoft::conversion::guessOutput(inputPath, "", gscToTsc, encoding, unpackOnly)
        : outputPath;
    const liarsoft::conversion::ConversionOptions options{
        encoding, referencePath, recursive, gscToTsc, unpackOnly, vorbisInWav, workers};
    const auto result = liarsoft::conversion::convert(inputPath, out, options);
    for (const auto& warning : result.warnings)
        std::cerr << "Warning: " << warning << std::endl;
    const auto operation = fs::is_directory(inputPath) && unpackOnly
        ? "DIR -> recursive unpack"
        : liarsoft::conversion::guessType(inputPath, gscToTsc, encoding, unpackOnly);
    std::cout << operation << ": " << inputPath << " -> "
              << (result.retainedPcm || (fs::is_directory(inputPath) && unpackOnly)
                  ? inputPath : out)
              << (result.retainedPcm ? " (PCM retained unchanged)" : "") << std::endl;
    return true;
}

static int runCli(int argc, char* argv[]) {
    std::string encoding = "CP932";
    std::string referencePath;
    std::vector<std::string> inputs;
    std::string explicitOutput;
    bool recursive = false;
    bool packOnly = false;
    bool unpackOnly = false;
    bool gscToTsc = true;
    bool vorbisInWav = false;
    unsigned workers = 0;
    bool checkUpdate = false;
    bool checkTsc = false, jsonDiagnostics = false;

    // Parse arguments
    int i = 1;
    while (i < argc) {
        std::string arg(argv[i]);
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-e" || arg == "--encoding") {
            if (i + 1 < argc) encoding = normalizeEncoding(argv[++i]);
            else { std::cerr << "Error: --encoding requires a value" << std::endl; return 1; }
        } else if (arg == "-r" || arg == "--reference") {
            if (i + 1 < argc) referencePath = argv[++i];
            else { std::cerr << "Error: --reference requires a value" << std::endl; return 1; }
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) explicitOutput = argv[++i];
            else { std::cerr << "Error: --output requires a value" << std::endl; return 1; }
        } else if (arg == "-R" || arg == "--recursive") {
            recursive = true;
        } else if (arg == "-j" || arg == "--jobs") {
            if (++i == argc) {
                std::cerr << "Error: --jobs requires a value (0..64)\n";
                return 1;
            }
            const std::string value(argv[i]);
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), workers);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                workers > liarsoft::MaxConversionWorkers) {
                std::cerr << "Error: --jobs must be an integer between 0 and 64\n";
                return 1;
            }
        } else if (arg == "--pack-only") {
            packOnly = true;
        } else if (arg == "--unpack-only") {
            unpackOnly = true;
        } else if (arg == "--gsc-to-tsc") {
            gscToTsc = true;
        } else if (arg == "--gsc-to-txt") {
            gscToTsc = false;
        } else if (arg == "--vorbis-in-wav") {
            vorbisInWav = true;
        } else if (arg == "--check-update") {
            checkUpdate = true;
        } else if (arg == "--check-tsc") {
            checkTsc = true;
        } else if (arg == "--diagnostics=json") {
            jsonDiagnostics = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Error: unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        } else {
            inputs.push_back(arg);
        }
        ++i;
    }

    if (jsonDiagnostics && !checkTsc) {
        std::cerr << "Error: --diagnostics=json requires --check-tsc\n";
        return 1;
    }
    if (checkTsc) {
        if (inputs.empty() || checkUpdate || !explicitOutput.empty() || !referencePath.empty() ||
            recursive || packOnly || unpackOnly || vorbisInWav || !gscToTsc || workers) {
            std::cerr << "Error: --check-tsc requires inputs and cannot be combined with conversion or update options\n";
            return 1;
        }
        return checkInputs(inputs, encoding, jsonDiagnostics);
    }
    if (checkUpdate) {
        if (!inputs.empty() || !explicitOutput.empty()) {
            std::cerr << "Error: --check-update cannot be combined with input/output paths\n";
            return 1;
        }
        try {
            const auto info = liarsoft::checkForUpdate();
            std::cout << "Current: " << liarsoft::CurrentVersion << "\nLatest: " << info.latestVersion
                      << (info.available ? "\nUpdate available: " : "\nNo newer release: ")
                      << liarsoft::LatestReleaseUrl << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "Error: " << error.what() << '\n';
            return 1;
        }
    }
    if (inputs.empty()) {
        std::cerr << "Error: no input specified" << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    // Determine batch mode and output path
    std::string outputPath = explicitOutput;
    std::vector<std::string> allFiles;

    if (inputs.size() == 2 && explicitOutput.empty()) {
        // Backward compat: "tool in out" — if the two args have different extensions,
        // treat the second as output; otherwise both are inputs.
        std::string e1 = liarsoft::conversion::extension(inputs[0]);
        std::string e2 = liarsoft::conversion::extension(inputs[1]);
        if (!e1.empty() && !e2.empty() && e1 != e2) {
            outputPath = inputs[1];
            allFiles = globPattern(inputs[0]);
        } else {
            // Same extension or no extension — both are inputs
            for (const auto& pat : inputs) {
                auto matches = globPattern(pat);
                allFiles.insert(allFiles.end(), matches.begin(), matches.end());
            }
        }
    } else {
        // One or many inputs — all are input patterns/files
        for (const auto& pat : inputs) {
            auto matches = globPattern(pat);
            allFiles.insert(allFiles.end(), matches.begin(), matches.end());
        }
    }

    if (allFiles.empty()) {
        std::cerr << "Error: no files matched" << std::endl;
        return 1;
    }

    int errors = 0;
    try {
        for (size_t fi = 0; fi < allFiles.size(); ++fi) {
            const auto& f = allFiles[fi];
            if (!liarsoft::matchesOperationMode(f, packOnly, unpackOnly,
                                                recursive)) continue;
            std::string out;
            if (allFiles.size() == 1 && !outputPath.empty()) {
                out = outputPath;  // explicit output for single file
            } else if (!outputPath.empty() && allFiles.size() > 1) {
                // Output path is a directory for batch mode
                out = liarsoft::conversion::guessOutput(
                    f, outputPath, gscToTsc, encoding, unpackOnly);
            }
            // else: out stays empty → auto-derived

            if (!processOne(f, out, encoding, referencePath, recursive,
                            unpackOnly, gscToTsc, vorbisInWav, workers))
                ++errors;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    if (errors > 0)
        std::cerr << errors << " file(s) failed." << std::endl;

    return errors > 0 ? 1 : 0;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // The new check API uses UTF-8 paths; existing conversion paths remain native.
    bool checking = false;
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--check-tsc") checking = true;
    if (checking) {
        int count = 0;
        auto** wide = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!wide) { std::cerr << "Cannot read Unicode command line\n"; return 1; }
        std::vector<std::string> arguments;
        for (int i = 0; i < count; ++i) {
            const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, nullptr, 0, nullptr, nullptr);
            if (!size) { LocalFree(wide); std::cerr << "Invalid Unicode argument\n"; return 1; }
            std::string value(size, '\0');
            if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, value.data(), size, nullptr, nullptr)) {
                LocalFree(wide); std::cerr << "Cannot decode Unicode argument\n"; return 1;
            }
            value.pop_back(); arguments.push_back(std::move(value));
        }
        LocalFree(wide);
        std::vector<char*> pointers;
        for (auto& value : arguments) pointers.push_back(value.data());
        pointers.push_back(nullptr);
        return runCli(count, pointers.data());
    }
#endif
    return runCli(argc, argv);
}
