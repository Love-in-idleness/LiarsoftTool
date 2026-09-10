#include "exe_patch.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

const std::vector<uint8_t> kPattern1Prefix = {
    0x6A,0x00,0x6A,0x00,0x6A,0x00,0x6A,0x00,0x68
};

void appendPattern1(std::vector<uint8_t>& data, uint8_t charset) {
    data.insert(data.end(), kPattern1Prefix.begin(), kPattern1Prefix.end());
    const uint8_t tail[] = {charset,0x00,0x00,0x00,0x6A,0x00,0x6A,0x00};
    data.insert(data.end(), std::begin(tail), std::end(tail));
}

void appendPattern2(std::vector<uint8_t>& data, uint8_t charset) {
    const uint8_t pattern[] = {0xDA, 0x68, charset};
    data.insert(data.end(), std::begin(pattern), std::end(pattern));
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
}

} // namespace

int main() {
    std::vector<uint8_t> mixed = {0x4D, 0x5A, 0x00};
    appendPattern1(mixed, 0x80);
    mixed.push_back(0x90);
    appendPattern2(mixed, 0x86);
    mixed.push_back(0x90);
    appendPattern1(mixed, 0xCC);

    auto normalized = mixed;
    for (uint8_t source : {0x80, 0x86, 0xCC})
        normalized = liarsoft::exeConvertEncoding(normalized, source, 0xCC);
    auto expected = std::vector<uint8_t>{0x4D, 0x5A, 0x00};
    appendPattern1(expected, 0xCC);
    expected.push_back(0x90);
    appendPattern2(expected, 0xCC);
    expected.push_back(0x90);
    appendPattern1(expected, 0xCC);
    assert(normalized == expected);

    const std::string input = "exe_patch_test_input.bin";
    const std::string output = "exe_patch_test_output.bin";
    const std::string unsupported = "exe_patch_test_unsupported.bin";
    std::remove(input.c_str());
    std::remove(output.c_str());
    std::remove(unsupported.c_str());

    writeFile(input, mixed);
    liarsoft::exeConvertFile(input, output, "CP1251");
    assert(readFile(output) == expected);

    writeFile(unsupported, {0x4D, 0x5A, 0x00});
    bool rejected = false;
    try {
        liarsoft::exeConvertFile(unsupported, output, "GBK");
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);

    std::remove(input.c_str());
    std::remove(output.c_str());
    std::remove(unsupported.c_str());
    return 0;
}
