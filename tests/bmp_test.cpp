#include "bmp_normalizer.h"
#include "conversion.h"
#include "fileio.h"
#include "wcg_decoder.h"
#include "xflarchive.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void put(Bytes& data, size_t pos, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data[pos + i] = uint8_t(value >> (8 * i));
}
static Bytes fixture(uint8_t a, uint8_t b, uint8_t c) {
    Bytes data(66, 0);
    data[0] = 'B'; data[1] = 'M';
    put(data, 2, 66); put(data, 10, 54); put(data, 14, 40);
    put(data, 18, 3); put(data, 22, 1); data[26] = 1; data[28] = 32;
    put(data, 34, 12); put(data, 38, 3780); put(data, 42, 3780);
    for (size_t i = 54; i < data.size(); i += 4) {
        data[i] = 30; data[i + 1] = 20; data[i + 2] = 10;
    }
    data[57] = a; data[61] = b; data[65] = c;
    return data;
}
static void invalid(const Bytes& data) {
    try { liarsoft::normalizeCodexBmp(data); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Malformed BMP accepted");
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected fixture directory");
        const auto root = fs::path(argv[1]);
        fs::create_directories(root / "recursive/sub");
        fs::remove(root / "recursive/sub/broken.bmp"); // This test's previous malformed fixture.
        for (auto source : {fixture(0, 128, 255), fixture(0, 0, 0), fixture(255, 255, 255)}) {
            const auto normalized = liarsoft::normalizeCodexBmp(source);
            require(normalized.size() == source.size() + 68 && normalized[14] == 108 &&
                    normalized[10] == 122 && normalized[30] == 3, "Expected explicit BMP V4 alpha");
            for (size_t i = 54; i < source.size(); ++i)
                require(normalized[i + 68] == (i % 4 == 1 ? 255 - source[i] : source[i]),
                        "RGB/pixel order changed or alpha not inverted");
            require(normalized[38] == source[38] && normalized[39] == source[39], "DPI changed");
            require(liarsoft::normalizeCodexBmp(normalized) == normalized, "Double inversion");
        }
        auto opaque = fixture(0, 0, 0);
        opaque[28] = 24;
        require(liarsoft::normalizeCodexBmp(opaque) == opaque, "Opaque BMP modified");
        auto bad = fixture(0, 128, 255);
        invalid({}); invalid(Bytes(bad.begin(), bad.begin() + 53));
        for (const auto& item : {std::pair<size_t, uint32_t>{18, 0}, {18, 0xffffffff},
                                {18, 0x7fffffff}, {22, 0x80000000}, {10, 53},
                                {10, 0xffffffff}, {14, 0xffffffff}, {2, 65}, {2, 1000}}) {
            auto broken = bad; put(broken, item.first, item.second); invalid(broken);
        }
        auto gap = bad;
        gap.insert(gap.begin() + 54, {1, 2, 3, 4});
        gap.push_back(42); put(gap, 10, 58); put(gap, 2, gap.size());
        const auto expanded = liarsoft::normalizeCodexBmp(gap);
        require(expanded[122] == 1 && expanded[125] == 4 && expanded.back() == 42,
                "BMP gap/trailer lost");

        const auto input = (root / "input.bmp").string();
        const auto output = (root / "output.bmp").string();
        liarsoft::writeFileIfChanged(input, bad);
        fs::last_write_time(input, fs::file_time_type::clock::now() - std::chrono::hours(24));
        const auto time = fs::last_write_time(input);
        liarsoft::conversion::ConversionOptions options;
        options.unpackOnly = true;
        const auto result = liarsoft::conversion::convert(input, output, options);
        require(result.warnings.empty() && !result.retainedPcm, "Unexpected diagnostics");
        require(fs::last_write_time(output) == time, "Output time changed");
        const auto expected = liarsoft::normalizeCodexBmp(bad);
        require(liarsoft::readFile(output) == expected, "Public conversion differs");
        require(liarsoft::conversion::guessOutput(input, "", false, "CP932", true) == input,
                "Unpack output not BMP");
        require(liarsoft::conversion::guessType(input, false, "CP932", true) == "CodeX BMP -> BMP",
                "Unpack type incorrect");
        require(liarsoft::matchesOperationMode(input, false, true) &&
                !liarsoft::matchesOperationMode(input, true, true), "Direction filtering changed");
        liarsoft::conversion::convert(output, output, options);
        require(liarsoft::readFile(output) == expected && fs::last_write_time(output) == time,
                "Repeated normalization changed content/time");
        options.unpackOnly = false;
        const auto wcg = (root / "packed.wcg").string();
        liarsoft::conversion::convert(output, wcg, options);
        const auto packed = liarsoft::wcgDecode(liarsoft::readFile(wcg));
        require(packed.width == 3 && packed.pixels == Bytes({30, 20, 10, 255,
                    30, 20, 10, 127, 30, 20, 10, 0}), "BMP -> WCG pixels changed");
        const auto recursive = root / "recursive/sub/IMAGE.BMP";
        liarsoft::writeFileIfChanged(recursive.string(), bad);
        require(liarsoft::unpackDirectoryRecursively((root / "recursive").string(), "CP932").empty(),
                "Recursive normalization failed");
        require(liarsoft::readFile(recursive) == expected, "Recursive BMP differs");
        const auto broken = root / "recursive/sub/broken.bmp";
        liarsoft::writeFileIfChanged(broken.string(), Bytes{1, 2, 3});
        require(liarsoft::unpackDirectoryRecursively((root / "recursive").string(), "CP932").size() == 1 &&
                liarsoft::readFile(broken) == Bytes({1, 2, 3}), "Failed input not preserved/warned");
        liarsoft::writeFileIfChanged((root / "cli.bmp").string(), bad);
        fs::last_write_time(root / "cli.bmp", time);
        std::cout << "OK: BMP normalization, bounds, idempotence, timestamp, dispatch and WCG\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
