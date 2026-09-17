#include "gsc_decompiler.h"

#include <cstdio>
#include <cstddef>
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
std::vector<uint8_t> load(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), size);
    return data;
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
    appendU16(code, 15);
    appendU32(code, 99); appendU32(code, 4);
    for (int i = 0; i < 10; ++i) appendU32(code, i == 0 ? 8031 : (i == 1 ? 8032 : 0));
    appendU16(code, 8);

    std::vector<uint8_t> modern(36, 0);
    patchU32(modern, 4, 36); patchU32(modern, 8, code.size()); patchU32(modern, 12, 20);
    const std::string strings("\0Name\0Text\0Font Text\0select\0", 28);
    patchU32(modern, 16, strings.size());
    patchU32(modern, 28, 4); patchU32(modern, 32, 1);
    modern.insert(modern.end(), code.begin(), code.end());
    appendU32(modern, 0); appendU32(modern, 1); appendU32(modern, 6); appendU32(modern, 11);
    appendU32(modern, 21);
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
        !contains(listing, "*gosub 99 \"select\" 8031 8032 0 0 0 0 0 0 0 0") ||
        !contains(listing, "*flagset 1 2 3") || !contains(listing, "*dynsel 1000 4") ||
        !contains(listing, "*map 22 23 24") || !contains(listing, "*end") ||
        listing.find(";@gsc-trailer") != std::string::npos ||
        listing.find(";@gsc-text-encoding") != std::string::npos) return 1;
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

    // The output encoding is chosen by the caller. A stale
    // `;@gsc-text-encoding` line left over from an older TSC must be ignored,
    // and text the requested encoding cannot represent must fail loudly rather
    // than being written as '?'.
    const std::string staleEncoding =
        ";@gsc-byte-format modern-36\n;@gsc-text-encoding GBK\n;@gsc-schema modern\n"
        "*TXT 0 0 0 0 \"\" \"\xe8\xb6\x8a\xe8\xbf\x87\xe5\x89\x8d\xe6\x96\xb9\" 1\n*end\n";
    const auto gbkGsc = liarsoft::restoreGscFromTsc(staleEncoding, "GBK");
    save(temp, gbkGsc);
    const auto gbkListing = liarsoft::decompileGsc(temp.string(), "GBK");
    if (gbkListing.find(";@gsc-text-encoding") != std::string::npos ||
        !contains(gbkListing, "\"\xe8\xb6\x8a\xe8\xbf\x87\xe5\x89\x8d\xe6\x96\xb9\"")) {
        std::cerr << "Encoding metadata was emitted or the text was not preserved"
                  << std::endl; return 1;
    }
    bool encodingRejected = false;
    try {
        liarsoft::restoreGscFromTsc(staleEncoding, "CP932");
    } catch (const std::exception&) {
        encodingRejected = true;
    }
    if (!encodingRejected) {
        std::cerr << "Unencodable text was silently written instead of failing" << std::endl;
        return 1;
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

    // The trailer (two debug tables plus a names blob) and the two header words
    // that size it must be carried into the TSC and restored verbatim.
    const auto makeEndOnlyGsc = [](uint32_t tableSize, uint32_t nameSize,
                                   const std::vector<uint8_t>& trailer) {
        std::vector<uint8_t> gsc(36, 0), code;
        appendU16(code, 8);
        patchU32(gsc, 4, 36); patchU32(gsc, 8, code.size());
        patchU32(gsc, 12, 4); patchU32(gsc, 16, 1);
        patchU32(gsc, 28, tableSize); patchU32(gsc, 32, nameSize);
        gsc.insert(gsc.end(), code.begin(), code.end());
        appendU32(gsc, 0); gsc.push_back(0);
        gsc.insert(gsc.end(), trailer.begin(), trailer.end());
        patchU32(gsc, 0, gsc.size());
        return gsc;
    };
    const std::vector<uint8_t> namesTrailer = {
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x10, 0x0b, 0x00, 0x00, 0x00, 's', 'c', 'm', 'o', 'd', 'e', 0x00};
    const auto trailered = makeEndOnlyGsc(8, 8, namesTrailer);
    save(temp, trailered);
    const auto traileredListing = liarsoft::decompileGsc(temp.string());
    if (!contains(traileredListing, ";@gsc-trailer-header 8 8") ||
        !contains(traileredListing,
                  ";@gsc-trailer 000000000100000000000000100b00000073636d6f646500") ||
        liarsoft::restoreGscFromTsc(traileredListing) != trailered) {
        std::cerr << "Debug tables and names were not preserved verbatim" << std::endl;
        return 1;
    }

    const std::vector<uint8_t> oddTrailer = {0x00, 0x00, 0x00, 0x00, 0xe0, 0x66,
                                             0xa7, 0x00, 0x00};
    const auto oddTrailered = makeEndOnlyGsc(4, 1, oddTrailer);
    save(temp, oddTrailered);
    const auto oddListing = liarsoft::decompileGsc(temp.string());
    if (!contains(oddListing, ";@gsc-trailer 00000000e066a70000") ||
        oddListing.find(";@gsc-trailer-header") != std::string::npos ||
        liarsoft::restoreGscFromTsc(oddListing) != oddTrailered) {
        std::cerr << "Non-standard trailer was not preserved verbatim" << std::endl;
        return 1;
    }

    bool trailerRejected = false;
    try {
        liarsoft::restoreGscFromTsc(
            ";@gsc-byte-format legacy-28\n;@gsc-text-encoding CP932\n"
            ";@gsc-schema early\n;@gsc-trailer 0000\n*end\n");
    } catch (const std::exception&) {
        trailerRejected = true;
    }
    if (!trailerRejected) {
        std::cerr << "Legacy GSC accepted a trailer region" << std::endl; return 1;
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

    std::vector<uint8_t> legacyTxt(28, 0), legacyTxtCode;
    appendU16(legacyTxtCode, 81);
    for (const uint32_t value : {0u, 123u, 0u, 0u, 0u, 1u, 1u})
        appendU32(legacyTxtCode, value);
    appendU16(legacyTxtCode, 8);
    patchU32(legacyTxt, 4, 28); patchU32(legacyTxt, 8, legacyTxtCode.size());
    patchU32(legacyTxt, 12, 8); patchU32(legacyTxt, 16, 16);
    legacyTxt.insert(legacyTxt.end(), legacyTxtCode.begin(), legacyTxtCode.end());
    appendU32(legacyTxt, 0); appendU32(legacyTxt, 11);
    const std::string legacyTxtStrings("Wrong Name\0Text\0", 16);
    legacyTxt.insert(legacyTxt.end(), legacyTxtStrings.begin(), legacyTxtStrings.end());
    patchU32(legacyTxt, 0, legacyTxt.size());
    save(temp, legacyTxt);
    const auto legacyTxtListing = liarsoft::decompileGsc(temp.string());
    if (!contains(legacyTxtListing, "*TXT 0 123 0 0 \"\" \"Text\" 1")) {
        std::cerr << "Legacy TXT null name was decoded as string index zero" << std::endl;
        return 1;
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
    if (argc >= 2) {
        // The requested encoding is authoritative, so the corpus mode takes it
        // as an optional second argument (CP932 when omitted).
        const std::string corpusEncoding = argc >= 3 ? argv[2] : "CP932";
        size_t checked = 0;
        for (const auto& item : std::filesystem::recursive_directory_iterator(argv[1])) {
            if (!item.is_regular_file() || item.path().extension() != ".gsc") continue;
            const auto first = liarsoft::decompileGsc(item.path().string(), corpusEncoding);
            if (first.find(";@gsc-raw-v1") != std::string::npos ||
                first.find(";@gsc-code") != std::string::npos ||
                first.find(";@gsc-string") != std::string::npos) {
                std::cerr << "Non-structured corpus result: " << item.path() << std::endl;
                return 1;
            }
            const auto rebuilt = liarsoft::restoreGscFromTsc(first, corpusEncoding);
            const auto headerSize = readU32(rebuilt, 4);
            if ((headerSize == 28 && rebuilt.size() - readU32(rebuilt, 0) !=
                                      readU32(rebuilt, 24)) ||
                (headerSize == 36 && rebuilt.size() != readU32(rebuilt, 0))) {
                std::cerr << "Invalid rebuilt declared size: " << item.path() << std::endl;
                return 1;
            }
            const auto original = load(item.path());
            if (headerSize == 36) {
                // The trailer, and the two header words that size it, describe
                // compiler-emitted data that the TSC carries verbatim.
                const auto trailerOf = [](const std::vector<uint8_t>& data) {
                    const size_t core = 36ull + readU32(data, 8) + readU32(data, 12) +
                                        readU32(data, 16) + readU32(data, 20) +
                                        readU32(data, 24) * 2ull;
                    const size_t end = readU32(data, 0);
                    return std::vector<uint8_t>(data.begin() + static_cast<ptrdiff_t>(core),
                                                data.begin() + static_cast<ptrdiff_t>(end));
                };
                if (readU32(original, 28) != readU32(rebuilt, 28) ||
                    readU32(original, 32) != readU32(rebuilt, 32) ||
                    trailerOf(original) != trailerOf(rebuilt)) {
                    std::cerr << "Trailer was not preserved: " << item.path() << std::endl;
                    return 1;
                }
            }
            save(temp, rebuilt);
            const auto second = liarsoft::decompileGsc(temp.string(), corpusEncoding);
            if (normalizeListing(first) != normalizeListing(second)) {
                std::cerr << "Semantic round-trip mismatch: " << item.path() << std::endl;
                return 1;
            }
            ++checked;
        }
        std::cout << "Checked " << checked << " corpus GSC files (" << corpusEncoding
                  << ")" << std::endl;
    }
    std::remove(temp.string().c_str());
    return 0;
}
