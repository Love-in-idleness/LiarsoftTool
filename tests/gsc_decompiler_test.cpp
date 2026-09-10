#include "gsc_decompiler.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
void appendU16(std::vector<uint8_t>& d, uint16_t v) { d.push_back(v); d.push_back(v >> 8); }
void appendU32(std::vector<uint8_t>& d, uint32_t v) { appendU16(d, v); appendU16(d, v >> 16); }
void patchU32(std::vector<uint8_t>& d, size_t p, uint32_t v) {
    d[p] = v; d[p + 1] = v >> 8; d[p + 2] = v >> 16; d[p + 3] = v >> 24;
}
void patchU16(std::vector<uint8_t>& d, size_t p, uint16_t v) { d[p] = v; d[p + 1] = v >> 8; }
uint32_t readU32(const std::vector<uint8_t>& d, size_t p) {
    return static_cast<uint32_t>(d[p]) | (static_cast<uint32_t>(d[p + 1]) << 8) |
           (static_cast<uint32_t>(d[p + 2]) << 16) |
           (static_cast<uint32_t>(d[p + 3]) << 24);
}
void save(const std::filesystem::path& p, const std::vector<uint8_t>& d) {
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(d.data()), d.size());
}
bool contains(const std::string& text, const std::string& expected) {
    if (text.find(expected) != std::string::npos) return true;
    std::cerr << "Missing decompiler output: " << expected << std::endl;
    return false;
}

std::string normalizeListing(const std::string& text) {
    std::string result;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("; generated from ", 0) == 0) continue;
        result += line + '\n';
    }
    return result;
}
}

int main(int argc, char** argv) {
    std::vector<uint8_t> code;
    appendU16(code, 0x4800); appendU16(code, 1); appendU16(code, 1); appendU16(code, 0);
    appendU16(code, 0xf100); appendU16(code, 0); appendU16(code, 1);
    appendU16(code, 3); appendU32(code, 50);
    appendU16(code, 81);
    appendU32(code, 0); appendU32(code, 123); appendU32(code, 0); appendU32(code, 0);
    appendU32(code, 1); appendU32(code, 2); appendU32(code, 0);
    appendU16(code, 32);
    appendU32(code, 40); appendU32(code, 400); appendU32(code, 250);
    appendU32(code, 0); appendU32(code, 0); appendU32(code, 3);
    appendU16(code, 13); appendU32(code, 7);
    appendU16(code, 202); appendU32(code, 1); appendU32(code, 2); appendU32(code, 3);
    appendU16(code, 210); appendU32(code, 1000); appendU32(code, 4);
    appendU16(code, 211); appendU32(code, 1001); appendU32(code, 5); appendU32(code, 6); appendU32(code, 7);
    appendU16(code, 212); appendU32(code, 1002);
    appendU16(code, 213); appendU32(code, 8); appendU32(code, 9); appendU32(code, 10);
    appendU16(code, 136); appendU32(code, 11); appendU32(code, 12); appendU32(code, 13);
    appendU16(code, 113); appendU32(code, 14); appendU32(code, 15);
    appendU16(code, 140); appendU32(code, 16); appendU32(code, 17);
    appendU16(code, 141); appendU32(code, 18); appendU32(code, 19);
    appendU16(code, 142); appendU32(code, 20); appendU32(code, 21);
    appendU16(code, 220); appendU32(code, 22); appendU32(code, 23); appendU32(code, 24);
    appendU16(code, 8);

    std::vector<uint8_t> modern(36, 0);
    patchU32(modern, 4, 36); patchU32(modern, 8, code.size()); patchU32(modern, 12, 16);
    const std::string strings("\0Name\0Text\0Font Text\0", 21);
    patchU32(modern, 16, strings.size());
    patchU32(modern, 28, 4); patchU32(modern, 32, 1);
    modern.insert(modern.end(), code.begin(), code.end());
    appendU32(modern, 0); appendU32(modern, 1); appendU32(modern, 6); appendU32(modern, 11);
    modern.insert(modern.end(), strings.begin(), strings.end());
    modern.insert(modern.end(), 9, 0);
    patchU32(modern, 0, modern.size());

    const auto temp = std::filesystem::temp_directory_path() / "liarsofttool_gsc_decompiler_test.gsc";
    save(temp, modern);
    const auto listing = liarsoft::decompileGsc(temp.string());
    if (listing.find(";@gsc-code") != std::string::npos ||
        listing.find(";@gsc-string") != std::string::npos ||
        listing.find(";@gsc-instruction") != std::string::npos ||
        listing.find(";@gsc-section") != std::string::npos ||
        listing.find(";@gsc-raw-v1") != std::string::npos ||
        !contains(listing, ";@gsc-byte-format modern-36") ||
        !contains(listing, ";@gsc-schema modern") ||
        !contains(listing, "*vm 0x4800 1 1 0") ||
        !contains(listing, "*jz L_000032") ||
        !contains(listing, "*TXT 0 123 0 0 \"Name\" \"Text\" 0") ||
        !contains(listing, "*font 40 400 250 0 0 \"Font Text\"") ||
        !contains(listing, "*flagset 1 2 3") || !contains(listing, "*dynsel 1000 4") ||
        !contains(listing, "*map 22 23 24") || !contains(listing, "*end")) return 1;
    if (liarsoft::restoreGscFromTsc(listing) != modern) {
        std::cerr << "Canonical modern GSC did not round-trip" << std::endl; return 1;
    }

    auto edited = listing;
    const std::string originalTxt = "*TXT 0 123 0 0 \"Name\" \"Text\" 0";
    const std::string changedTxt = "*TXT 0 456 0 0 \"Edited\" \"Changed\" 0";
    edited.replace(edited.find(originalTxt), originalTxt.size(), changedTxt);
    edited.replace(edited.find("\"Font Text\""), 11, "\"Edited Font\"");
    const auto editedGsc = liarsoft::restoreGscFromTsc(edited);
    save(temp, editedGsc);
    const auto editedListing = liarsoft::decompileGsc(temp.string());
    if (!contains(editedListing, changedTxt) || !contains(editedListing, "\"Edited Font\"") ||
        editedGsc == modern) {
        std::cerr << "TSC body edits were not compiled into GSC" << std::endl; return 1;
    }

    const std::string dataSource =
        ";@gsc-byte-format modern-36\n;@gsc-text-encoding CP932\n;@gsc-schema modern\n"
        "*datablock 0 3 -1 2 32767\n"
        ":start\n"
        "*select 1 \"Question\" choice choice choice choice choice "
        "\"Answer\" \"\" \"\" \"\" \"\" 0 0 0\n"
        "*data @7 0\n:choice\n*end\n";
    const auto dataGsc = liarsoft::restoreGscFromTsc(dataSource);
    save(temp, dataGsc);
    const auto dataListing = liarsoft::decompileGsc(temp.string());
    if (!contains(dataListing, "\"Question\"") || !contains(dataListing, "\"Answer\"") ||
        !contains(dataListing, "*datablock 0 3 -1 2 32767") ||
        !contains(dataListing, "*data @7 0") ||
        liarsoft::restoreGscFromTsc(dataListing) != dataGsc) {
        std::cerr << "Select strings or data blocks did not round-trip" << std::endl; return 1;
    }

    const std::string legacyDataSource =
        ";@gsc-byte-format legacy-28\n;@gsc-text-encoding CP932\n;@gsc-schema early\n"
        "*datablock 0 3 -1 2 32767\n*data 0 0\n*end\n";
    const auto legacyDataGsc = liarsoft::restoreGscFromTsc(legacyDataSource);
    if (legacyDataGsc.size() - readU32(legacyDataGsc, 0) !=
            readU32(legacyDataGsc, 24)) {
        std::cerr << "Legacy declared size did not count Section D in words" << std::endl;
        return 1;
    }

    std::vector<uint8_t> legacy(28, 0);
    patchU32(legacy, 4, 28); patchU32(legacy, 8, 8); patchU32(legacy, 12, 4); patchU32(legacy, 16, 1);
    appendU16(legacy, 5); appendU32(legacy, 8); appendU16(legacy, 8);
    appendU32(legacy, 0); legacy.push_back(0); patchU32(legacy, 0, legacy.size());
    save(temp, legacy);
    const auto legacyListing = liarsoft::decompileGsc(temp.string());
    if (!contains(legacyListing, ";@gsc-byte-format legacy-28") ||
        !contains(legacyListing, "*goto L_000008") ||
        liarsoft::restoreGscFromTsc(legacyListing) != legacy) {
        std::cerr << "Canonical legacy GSC did not round-trip" << std::endl; return 1;
    }

    std::vector<uint8_t> earlyModern(36, 0), earlyCode;
    appendU16(earlyCode, 38); appendU32(earlyCode, 1); appendU32(earlyCode, 2); appendU32(earlyCode, 3);
    appendU16(earlyCode, 105); appendU32(earlyCode, 4); appendU16(earlyCode, 8);
    patchU32(earlyModern, 4, 36); patchU32(earlyModern, 8, earlyCode.size());
    patchU32(earlyModern, 12, 4); patchU32(earlyModern, 16, 1);
    patchU32(earlyModern, 28, 4); patchU32(earlyModern, 32, 1);
    earlyModern.insert(earlyModern.end(), earlyCode.begin(), earlyCode.end());
    appendU32(earlyModern, 0); earlyModern.push_back(0); earlyModern.insert(earlyModern.end(), 9, 0);
    patchU32(earlyModern, 0, earlyModern.size());
    save(temp, earlyModern);
    const auto earlyListing = liarsoft::decompileGsc(temp.string());
    if (!contains(earlyListing, ";@gsc-schema rscript18") ||
        !contains(earlyListing, "*locmode 1 2 3") || !contains(earlyListing, "*facedep 4") ||
        liarsoft::restoreGscFromTsc(earlyListing) != earlyModern) {
        std::cerr << "RScript 1.8 schema did not round-trip" << std::endl; return 1;
    }

    auto unknown = modern; patchU16(unknown, 36, 0); save(temp, unknown);
    const auto unknownListing = liarsoft::decompileGsc(temp.string());
    if (unknownListing.find("; decompilation unavailable:") == std::string::npos ||
        unknownListing.find(";@gsc-raw-v1 ") == std::string::npos ||
        liarsoft::restoreGscFromTsc(unknownListing) != unknown) {
        std::cerr << "Unknown dialect did not use exact raw fallback" << std::endl; return 1;
    }
    if (argc == 2) {
        size_t checked = 0;
        for (const auto& item : std::filesystem::recursive_directory_iterator(argv[1])) {
            if (!item.is_regular_file() || item.path().extension() != ".gsc") continue;
            const auto first = liarsoft::decompileGsc(item.path().string());
            if (first.find(";@gsc-raw-v1") != std::string::npos ||
                first.find(";@gsc-code") != std::string::npos ||
                first.find(";@gsc-string") != std::string::npos) {
                std::cerr << "Non-structured corpus result: " << item.path() << std::endl;
                return 1;
            }
            const auto rebuilt = liarsoft::restoreGscFromTsc(first);
            const auto headerSize = readU32(rebuilt, 4);
            if ((headerSize == 28 && rebuilt.size() - readU32(rebuilt, 0) !=
                                      readU32(rebuilt, 24)) ||
                (headerSize == 36 && rebuilt.size() != readU32(rebuilt, 0))) {
                std::cerr << "Invalid rebuilt declared size: " << item.path() << std::endl;
                return 1;
            }
            save(temp, rebuilt);
            const auto second = liarsoft::decompileGsc(temp.string());
            if (normalizeListing(first) != normalizeListing(second)) {
                std::cerr << "Semantic round-trip mismatch: " << item.path() << std::endl;
                return 1;
            }
            ++checked;
        }
        std::cout << "Checked " << checked << " corpus GSC files" << std::endl;
    }
    std::remove(temp.string().c_str());
    return 0;
}
