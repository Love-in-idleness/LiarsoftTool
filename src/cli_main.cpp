#include "gscfile.h"
#include "transfile.h"
#include "xflarchive.h"
#include "wcg_decoder.h"
#include "lim_decoder.h"
#include "lwg_decoder.h"
#include "wav_ogg.h"
#include "exe_patch.h"
#include "stb_image.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

static void printUsage(const char* prog) {
    std::cout << "LiarsoftTool - Liar-soft visual novel resource converter\n"
              << "Usage: " << prog << " [options] <input...> [output]\n\n"
              << "Options:\n"
              << "  -e, --encoding <enc>  Text encoding (default: shift_jis)\n"
              << "                        Supported: shift_jis, gbk\n"
              << "  -r, --reference <path> Reference GSC file for TXT->GSC conversion\n"
              << "  -o, --output <path>    Explicit output file or directory\n"
              << "  -h, --help            Show this help message\n\n"
              << "Conversion modes:\n"
              << "  .gsc  -> .txt         Extract translatable strings from GSC\n"
              << "  .txt  -> .gsc         Pack translated strings back into GSC\n"
              << "  .xfl  -> directory    Unpack XFL archive into a folder\n"
              << "  .lwg  -> directory    Unpack LWG archive into a folder\n"
              << "  .wcg  -> .png         Convert WCG image to PNG\n"
              << "  .lim  -> .png         Convert LIM image to PNG\n"
              << "  .wav  -> .ogg         Extract embedded Ogg Vorbis from WAV\n"
              << "  .png/.jpg/.bmp -> .wcg Convert image to WCG\n"
              << "  directory -> .xfl     Pack a folder into an XFL archive\n"
              << "  directory -> .lwg     Pack folder with .meta.xml into LWG\n"
              << "  .exe  -> .gbk.exe     Convert EXE encoding (SJIS⇄GBK)\n\n"
              << "Supports wildcards: " << prog << " *.png\n\n"
              << "Examples:\n"
              << "  " << prog << " -e shift_jis scenario.gsc\n"
              << "  " << prog << " -e gbk scenario.gsc output.txt\n"
              << "  " << prog << " -e shift_jis -r original.gsc translation.txt\n"
              << "  " << prog << " -e gbk archive.xfl\n"
              << "  " << prog << " image.wcg\n"
              << "  " << prog << " cgview.lwg\n"
              << "  " << prog << " -e gbk game.exe          # SJIS→GBK\n"
              << "  " << prog << " -e shift_jis game.exe    # GBK→SJIS\n"
              << "  " << prog << " 0*.png                  # batch convert all matching PNGs\n"
              << "  " << prog << " -e gbk ./extracted_dir\n"
              << std::endl;
}

static std::string getExtension(const std::string& path) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return "";
    std::string ext = path.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

static std::string replaceExtension(const std::string& path, const std::string& newExt) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return path + newExt;
    return path.substr(0, pos) + newExt;
}

static std::string normalizeEncoding(const std::string& enc) {
    std::string lower = enc;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "shift_jis" || lower == "shift-jis" || lower == "sjis" || lower == "shiftjis")
        return "SHIFT_JIS";
    if (lower == "gbk" || lower == "gb2312" || lower == "gb18030")
        return "GBK";
    return enc;
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
static std::vector<std::string> globPattern(const std::string& pattern) {
    std::vector<std::string> results;
    bool hasWild = pattern.find_first_of("*?[") != std::string::npos;

    if (!hasWild) {
        // Literal path — return as-is
        results.push_back(pattern);
        return results;
    }

    // Separate directory and filename pattern
    fs::path p(pattern);
    fs::path dir = p.parent_path();
    std::string filePat = p.filename().string();

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
        std::string name = entry.path().filename().string();
        if (wildMatch(name, filePat))
            results.push_back(entry.path().string());
    }

    if (results.empty() && !hasWild)
        results.push_back(pattern);
    return results;
}

// ---- Core processing of a single input ----
// Returns true on success.
static bool processOne(const std::string& inputPath,
                       const std::string& outputPath,
                       const std::string& encoding,
                       const std::string& referencePath)
{
    // Resolve absolute path
    std::error_code ec;
    std::string resolved = fs::absolute(inputPath, ec).string();
    if (ec) resolved = inputPath;

    // ---- Directory → XFL or LWG ----
    if (fs::is_directory(resolved)) {
        // Strip trailing slash for clean extension appending
        while (!resolved.empty() && (resolved.back() == '/' || resolved.back() == '\\'))
            resolved.pop_back();

        bool hasMeta = fs::exists(resolved + "/.meta.xml");
        std::string out = outputPath;
        if (hasMeta) {
            std::cout << "Packing directory to LWG: " << resolved
                      << " (encoding: " << encoding << ")" << std::endl;
            if (out.empty()) out = resolved + ".lwg";
            liarsoft::LwgPacker::packToFile(resolved, out, encoding);
        } else {
            std::cout << "Packing directory to XFL: " << resolved
                      << " (encoding: " << encoding << ")" << std::endl;
            liarsoft::XflArchive archive;
            archive.encoding = encoding;
            archive.addDirectory(resolved);
            if (out.empty()) out = resolved + ".xfl";
            archive.save(out);
        }
        std::cout << "Packed to: " << out << std::endl;
        return true;
    }

    std::string ext = getExtension(inputPath);
    std::string out = outputPath;

    // Helper: read entire file
    auto readFile = [](const std::string& path) -> std::vector<uint8_t> {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("Cannot open: " + path);
        in.seekg(0, std::ios::end);
        size_t sz = in.tellg();
        in.seekg(0, std::ios::beg);
        std::vector<uint8_t> data(sz);
        in.read(reinterpret_cast<char*>(data.data()), sz);
        return data;
    };

    if (ext == ".gsc") {
        std::cout << "Reading GSC: " << inputPath << " (encoding: " << encoding << ")" << std::endl;
        auto gsc = liarsoft::GscFile::fromFile(inputPath, encoding);
        auto trans = liarsoft::TransFile::fromGsc(gsc);
        if (out.empty()) out = replaceExtension(inputPath, ".txt");
        trans.save(out);
        std::cout << "Extracted " << trans.strings.size() << " strings to: " << out << std::endl;

    } else if (ext == ".txt") {
        std::string ref = referencePath;
        if (ref.empty()) ref = replaceExtension(inputPath, ".gsc");
        std::cout << "Reading TXT: " << inputPath << std::endl;
        std::cout << "Using reference GSC: " << ref << " (encoding: " << encoding << ")" << std::endl;
        auto trans = liarsoft::TransFile::fromFile(inputPath);
        auto gsc = trans.toGsc(ref, encoding);
        if (out.empty()) out = replaceExtension(inputPath, ".gsc");
        gsc.save(out);
        std::cout << "Packed " << trans.strings.size() << " strings to: " << out << std::endl;

    } else if (ext == ".xfl") {
        std::cout << "Reading XFL: " << inputPath << " (encoding: " << encoding << ")" << std::endl;
        auto archive = liarsoft::XflArchive::fromFile(inputPath, encoding);
        if (out.empty()) out = replaceExtension(inputPath, "");
        archive.extractToDirectory(out);
        std::cout << "Extracted " << archive.entries.size() << " files to: " << out << std::endl;

    } else if (ext == ".lwg") {
        std::cout << "Reading LWG: " << inputPath << " (encoding: " << encoding << ")" << std::endl;
        auto raw = readFile(inputPath);
        auto archive = liarsoft::LwgDecoder::decode(raw, encoding);
        if (out.empty()) out = replaceExtension(inputPath, "");
        liarsoft::LwgDecoder::extractToDirectory(archive, out, encoding);
        std::cout << "Extracted " << archive.entries.size() << " files to: " << out << std::endl;

    } else if (ext == ".wav") {
        std::cout << "Processing WAV: " << inputPath << std::endl;
        if (out.empty()) out = replaceExtension(inputPath, ".ogg");
        liarsoft::WavOggExtractor::extractToFile(inputPath, out);
        std::cout << "Extracted OGG to: " << out << std::endl;

    } else if (ext == ".wcg") {
        std::cout << "Converting WCG: " << inputPath << std::endl;
        auto img = liarsoft::wcgDecode(readFile(inputPath));
        if (out.empty()) out = replaceExtension(inputPath, ".png");
        liarsoft::wcgSavePng(img, out);
        std::cout << "Saved " << img.width << "x" << img.height << " PNG to: " << out << std::endl;

    } else if (ext == ".lim") {
        std::cout << "Converting LIM: " << inputPath << std::endl;
        auto img = liarsoft::limDecode(readFile(inputPath));
        if (out.empty()) out = replaceExtension(inputPath, ".png");
        liarsoft::limSavePng(img, out);
        std::cout << "Saved " << img.width << "x" << img.height << " PNG to: " << out << std::endl;

    } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
        std::cout << "Converting to WCG: " << inputPath << std::endl;
        int w, h, ch;
        unsigned char* px = stbi_load(inputPath.c_str(), &w, &h, &ch, 4);
        if (!px) throw std::runtime_error("Failed to load image: " + inputPath);
        auto wcgData = liarsoft::wcgEncode(px, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        stbi_image_free(px);
        if (out.empty()) out = replaceExtension(inputPath, ".wcg");
        std::ofstream fout(out, std::ios::binary);
        fout.write(reinterpret_cast<const char*>(wcgData.data()), wcgData.size());
        std::cout << "Saved " << w << "x" << h << " WCG to: " << out << std::endl;

    } else if (ext == ".exe") {
        std::string dir = (encoding == "GBK") ? "SJIS→GBK" : "GBK→SJIS";
        std::cout << "Converting EXE (" << dir << "): " << inputPath << std::endl;
        if (out.empty()) {
            out = (encoding == "GBK") ? replaceExtension(inputPath, ".gbk.exe")
                                      : replaceExtension(inputPath, ".sjis.exe");
        }
        liarsoft::exeConvertFile(inputPath, out, encoding);
        std::cout << "Saved patched EXE to: " << out << std::endl;

    } else {
        std::cerr << "Error: unsupported file extension '" << ext
                  << "' for: " << inputPath << std::endl;
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    std::string encoding = "SHIFT_JIS";
    std::string referencePath;
    std::vector<std::string> inputs;
    std::string explicitOutput;

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
        } else if (arg[0] == '-') {
            std::cerr << "Error: unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        } else {
            inputs.push_back(arg);
        }
        ++i;
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
        std::string e1 = getExtension(inputs[0]);
        std::string e2 = getExtension(inputs[1]);
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
            std::string out;
            if (allFiles.size() == 1 && !outputPath.empty()) {
                out = outputPath;  // explicit output for single file
            } else if (!outputPath.empty() && allFiles.size() > 1) {
                // Output path is a directory for batch mode
                fs::path name = fs::path(f).filename();
                out = (fs::path(outputPath) / name).string();
            }
            // else: out stays empty → auto-derived

            if (allFiles.size() > 1)
                std::cout << "\n[" << (fi + 1) << "/" << allFiles.size() << "] ";
            if (!processOne(f, out, encoding, referencePath))
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
