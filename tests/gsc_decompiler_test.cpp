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
    std::remove(temp.string().c_str());
    for (const std::string expected : {
             "*if ((@1 == 0)) == 0", "*goto L_000032",
             "*voice 123", "\\Name\"：\"Text", ":L_000032", "*end"}) {
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
