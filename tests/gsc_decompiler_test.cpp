#include "gsc_decompiler.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void appendU16(std::vector<uint8_t>& data, uint16_t value) {
    data.push_back(static_cast<uint8_t>(value));
    data.push_back(static_cast<uint8_t>(value >> 8));
}

void appendU32(std::vector<uint8_t>& data, uint32_t value) {
    appendU16(data, static_cast<uint16_t>(value));
    appendU16(data, static_cast<uint16_t>(value >> 16));
}

void patchU32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    data[offset] = static_cast<uint8_t>(value);
    data[offset + 1] = static_cast<uint8_t>(value >> 8);
    data[offset + 2] = static_cast<uint8_t>(value >> 16);
    data[offset + 3] = static_cast<uint8_t>(value >> 24);
}

void patchU16(std::vector<uint8_t>& data, size_t offset, uint16_t value) {
    data[offset] = static_cast<uint8_t>(value);
    data[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void save(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(data.data()), data.size());
}

} // namespace

int main() {
    std::vector<uint8_t> code;
    appendU16(code, 0x4800); appendU16(code, 1); appendU16(code, 1); appendU16(code, 0);
    appendU16(code, 0xf100); appendU16(code, 0); appendU16(code, 1);
    appendU16(code, 3); appendU32(code, 50);
    appendU16(code, 81);
    appendU32(code, 0); appendU32(code, 123); appendU32(code, 0);
    appendU32(code, 0); appendU32(code, 0); appendU32(code, 1); appendU32(code, 0);
    appendU16(code, 13); appendU32(code, 7);
    appendU16(code, 202); appendU32(code, 1); appendU32(code, 2); appendU32(code, 3);
    appendU16(code, 210); appendU32(code, 1000); appendU32(code, 4);
    appendU16(code, 211); appendU32(code, 1001); appendU32(code, 5); appendU32(code, 6); appendU32(code, 7);
    appendU16(code, 212); appendU32(code, 1002);
    appendU16(code, 213); appendU32(code, 8); appendU32(code, 9); appendU32(code, 10);
    appendU16(code, 8);

    std::vector<uint8_t> modern(36, 0);
    patchU32(modern, 4, 36);
    patchU32(modern, 8, static_cast<uint32_t>(code.size()));
    patchU32(modern, 12, 8);
    patchU32(modern, 16, 10);
    modern.insert(modern.end(), code.begin(), code.end());
    appendU32(modern, 0); appendU32(modern, 5);
    const std::string strings("Name\0Text\0", 10);
    modern.insert(modern.end(), strings.begin(), strings.end());
    patchU32(modern, 0, static_cast<uint32_t>(modern.size()));

    const auto temp = std::filesystem::temp_directory_path() /
                      "liarsofttool_gsc_decompiler_test.gsc";
    save(temp, modern);
    const std::string listing = liarsoft::decompileGsc(temp.string());
    if (liarsoft::restoreGscFromTsc(listing) != modern) {
        std::cerr << "Modern GSC did not round-trip byte-for-byte" << std::endl;
        return 1;
    }
    if (liarsoft::restoreGscFromTsc(
            listing + "; ordinary comment\n*edited readable command\n") != modern) {
        std::cerr << "Readable TSC edits changed raw restoration" << std::endl;
        return 1;
    }
    std::string editedListing = listing;
    const std::string originalText = "\\Name\"：\"Text";
    const auto textAt = editedListing.find(originalText);
    if (textAt == std::string::npos) {
        std::cerr << "Missing editable TXT line" << std::endl;
        return 1;
    }
    editedListing.replace(textAt, originalText.size(), "\\Edited\"：\"Changed");
    const auto editedGsc = liarsoft::restoreGscFromTsc(editedListing);
    if (editedGsc == modern) {
        std::cerr << "Edited TXT line did not change the GSC" << std::endl;
        return 1;
    }
    save(temp, editedGsc);
    const auto editedRoundTrip = liarsoft::decompileGsc(temp.string());
    std::remove(temp.string().c_str());
    if (editedRoundTrip.find("\\Edited\"：\"Changed") == std::string::npos ||
        liarsoft::restoreGscFromTsc(editedRoundTrip) != editedGsc) {
        std::cerr << "Edited GSC text did not re-decompile or round-trip" << std::endl;
        return 1;
    }
    std::string commandListing = listing;
    const auto voiceAt = commandListing.find("*voice 123");
    const auto waitAt = commandListing.find("*wait 7");
    if (voiceAt == std::string::npos || waitAt == std::string::npos) {
        std::cerr << "Missing editable command line" << std::endl;
        return 1;
    }
    commandListing.replace(voiceAt, 10, "*voice 456");
    commandListing.replace(waitAt, 7, "*wait 9");
    const auto commandGsc = liarsoft::restoreGscFromTsc(commandListing);
    save(temp, commandGsc);
    const auto commandRoundTrip = liarsoft::decompileGsc(temp.string());
    std::remove(temp.string().c_str());
    if (commandRoundTrip.find("*voice 456") == std::string::npos ||
        commandRoundTrip.find("*wait 9") == std::string::npos ||
        liarsoft::restoreGscFromTsc(commandRoundTrip) != commandGsc) {
        std::cerr << "Edited command operands were not rebuilt" << std::endl;
        return 1;
    }
    std::remove(temp.string().c_str());
    for (const std::string expected : {
             "*if ((@1 == 0)) == 0", "*goto L_000032",
             "*voice 123", "\\Name\"：\"Text", ":L_000032", "*wait 7",
             "*flagset 1 2 3", "*dynsel 1000 4", "*dynans 1001 5 6 7",
             "*dynnext 1002", "*dyndo 8 9 10", "*end"}) {
        if (listing.find(expected) == std::string::npos) {
            std::cerr << "Missing decompiler output: " << expected << std::endl;
            return 1;
        }
    }

    std::vector<uint8_t> legacy(28, 0);
    patchU32(legacy, 0, 42); patchU32(legacy, 4, 28);
    patchU32(legacy, 8, 8); patchU32(legacy, 12, 4); patchU32(legacy, 16, 2);
    appendU16(legacy, 5); appendU32(legacy, 8); appendU16(legacy, 8);
    appendU32(legacy, 0);
    legacy.push_back('x'); legacy.push_back(0);
    save(temp, legacy);
    const std::string legacyListing = liarsoft::decompileGsc(temp.string());
    if (liarsoft::restoreGscFromTsc(legacyListing) != legacy) {
        std::cerr << "Legacy GSC did not round-trip byte-for-byte" << std::endl;
        return 1;
    }
    std::remove(temp.string().c_str());
    if (legacyListing.find("*goto L_000008 ; @000000") == std::string::npos ||
        legacyListing.find("*end ; @000006") == std::string::npos ||
        legacyListing.find(":L_000008") == std::string::npos) {
        std::cerr << "Legacy GSC was not decoded" << std::endl;
        return 1;
    }

    std::vector<uint8_t> earlyModern(36, 0);
    std::vector<uint8_t> earlyCode;
    appendU16(earlyCode, 38);
    appendU32(earlyCode, 1); appendU32(earlyCode, 2); appendU32(earlyCode, 3);
    appendU16(earlyCode, 105); appendU32(earlyCode, 4);
    appendU16(earlyCode, 8);
    patchU32(earlyModern, 4, 36);
    patchU32(earlyModern, 8, static_cast<uint32_t>(earlyCode.size()));
    patchU32(earlyModern, 12, 4);
    patchU32(earlyModern, 16, 2);
    earlyModern.insert(earlyModern.end(), earlyCode.begin(), earlyCode.end());
    appendU32(earlyModern, 0);
    earlyModern.push_back('x'); earlyModern.push_back(0);
    patchU32(earlyModern, 0, static_cast<uint32_t>(earlyModern.size()));
    save(temp, earlyModern);
    const auto earlyListing = liarsoft::decompileGsc(temp.string());
    std::remove(temp.string().c_str());
    if (earlyListing.find(";@gsc-instruction-schema rscript18") == std::string::npos ||
        earlyListing.find("*locmode 1 2 3") == std::string::npos ||
        earlyListing.find("; opcode 105 4") == std::string::npos ||
        liarsoft::restoreGscFromTsc(earlyListing) != earlyModern) {
        std::cerr << "RScript 1.8 instruction schema was not detected"
                  << std::endl;
        return 1;
    }
    auto editedEarlyListing = earlyListing;
    editedEarlyListing.replace(editedEarlyListing.find("*locmode 1 2 3"), 14,
                               "*locmode 1 2 9");
    const auto editedEarlyGsc = liarsoft::restoreGscFromTsc(editedEarlyListing);
    save(temp, editedEarlyGsc);
    const auto editedEarlyRoundTrip = liarsoft::decompileGsc(temp.string());
    std::remove(temp.string().c_str());
    if (editedEarlyRoundTrip.find("*locmode 1 2 9") == std::string::npos) {
        std::cerr << "RScript 1.8 command edit was not rebuilt" << std::endl;
        return 1;
    }

    std::vector<uint8_t> rscript19(36, 0);
    std::vector<uint8_t> rscript19Code;
    appendU16(rscript19Code, 38);
    appendU32(rscript19Code, 1); appendU32(rscript19Code, 2);
    appendU32(rscript19Code, 3); appendU32(rscript19Code, 4);
    appendU16(rscript19Code, 64); appendU32(rscript19Code, 7);
    appendU16(rscript19Code, 8);
    patchU32(rscript19, 4, 36);
    patchU32(rscript19, 8, static_cast<uint32_t>(rscript19Code.size()));
    patchU32(rscript19, 12, 4);
    patchU32(rscript19, 16, 2);
    rscript19.insert(rscript19.end(), rscript19Code.begin(), rscript19Code.end());
    appendU32(rscript19, 0);
    rscript19.push_back('x'); rscript19.push_back(0);
    patchU32(rscript19, 0, static_cast<uint32_t>(rscript19.size()));
    save(temp, rscript19);
    auto rscript19Listing = liarsoft::decompileGsc(temp.string());
    std::remove(temp.string().c_str());
    if (rscript19Listing.find(";@gsc-instruction-schema rscript19") == std::string::npos ||
        rscript19Listing.find("*locmode 1 2 3 4") == std::string::npos ||
        rscript19Listing.find("*se_off 7") == std::string::npos ||
        liarsoft::restoreGscFromTsc(rscript19Listing) != rscript19) {
        std::cerr << "RScript 1.9 instruction schema was not detected" << std::endl;
        return 1;
    }
    rscript19Listing.replace(rscript19Listing.find("*se_off 7"), 9, "*se_off 9");
    const auto editedRscript19 = liarsoft::restoreGscFromTsc(rscript19Listing);
    save(temp, editedRscript19);
    const auto editedRscript19Listing = liarsoft::decompileGsc(temp.string());
    std::remove(temp.string().c_str());
    if (editedRscript19Listing.find("*se_off 9") == std::string::npos) {
        std::cerr << "RScript 1.9 command edit was not rebuilt" << std::endl;
        return 1;
    }

    auto unknown = modern;
    patchU16(unknown, 36, 0);
    save(temp, unknown);
    const std::string unknownListing = liarsoft::decompileGsc(temp.string());
    std::remove(temp.string().c_str());
    if (unknownListing.find("; decompilation unavailable:") == std::string::npos ||
        liarsoft::restoreGscFromTsc(unknownListing) != unknown) {
        std::cerr << "Unknown dialect did not fall back to exact restoration" << std::endl;
        return 1;
    }

    std::string damaged = listing;
    const auto chunk = damaged.find(";@gsc-raw ");
    damaged[chunk + 10] = damaged[chunk + 10] == '0' ? '1' : '0';
    try {
        liarsoft::restoreGscFromTsc(damaged);
        std::cerr << "Damaged raw metadata was accepted" << std::endl;
        return 1;
    } catch (const std::runtime_error&) {
    }
    return 0;
}
