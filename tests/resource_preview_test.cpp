#include "resource_preview.h"
#include "encoding.h"
#include "fileio.h"
#include "gsc_decompiler.h"
#include "wcg_decoder.h"
#include "wav_ogg.h"
#include "vorbis_fixture.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace liarsoft;
#define require(condition) do { if (!(condition)) throw std::runtime_error("Preview check failed: " #condition); } while (false)
template<class F> static void rejected(F action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Damaged preview was accepted");
}
static std::vector<uint8_t> bytes(const std::string& text) { return {text.begin(), text.end()}; }
static void u32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) data.at(offset + i) = value >> (8 * i);
}
int main() {
    const LimImage image{2, 1, {1, 2, 3, 255, 50, 60, 70, 128}};
    for (const auto& data : {wcgEncode(image.pixels, 2, 1), limEncode(image), webpEncode(image)}) {
        const auto preview = decodeResourcePreview(data, "body");
        require(preview.kind == PreviewKind::Image && preview.image.width == 2 && preview.image.height == 1);
        require(preview.image.pixels == image.pixels);
    }
    std::vector<uint8_t> bmp(58, 0);
    bmp[0] = 'B'; bmp[1] = 'M'; u32(bmp, 2, 58); u32(bmp, 10, 54);
    u32(bmp, 14, 40); u32(bmp, 18, 1); u32(bmp, 22, 1); bmp[26] = 1; bmp[28] = 32;
    bmp[54] = 3; bmp[55] = 2; bmp[56] = 1; bmp[57] = 128;
    require(decodeResourcePreview(bmp, "back.msk").image.pixels == std::vector<uint8_t>({1, 2, 3, 127}));
    const std::string japanese = "日本語";
    require(decodeResourcePreview(bytes(convertEncoding(japanese, "UTF-8", "CP932", true)), "body.ini").text == japanese);
    require(decodeResourcePreview(bytes(japanese), "body.tsc", "CP1251").text == japanese);
    rejected([] { decodeResourcePreview({0x81}, "body.ini"); });
    rejected([] { decodeResourcePreview({'W', 'G'}, "body"); });
    const std::string tsc = ";@gsc-byte-format modern-36\n;@gsc-schema modern\n*end\n";
    const auto gsc = restoreGscFromTsc(tsc);
    rejected([] { decodeResourcePreview({'B', 'M', 0, 0}, "scene.gsc"); });
    require(decodeResourcePreview({'B', 'M', 0, 0}, "scene.gsc", "CP932").kind == PreviewKind::Text);
    require(decodeResourcePreview(gsc, "scene.gsc").text == decompileGscBytes(gsc, "CP932", nullptr, "scene.gsc"));
    std::vector<uint8_t> wav(48, 0);
    std::copy_n("RIFF", 4, wav.begin()); u32(wav, 4, 40);
    std::copy_n("WAVEfmt ", 8, wav.begin() + 8); u32(wav, 16, 16);
    wav[20] = 1; wav[22] = 1; u32(wav, 24, 8000); u32(wav, 28, 16000);
    wav[32] = 2; wav[34] = 16;
    std::copy_n("data", 4, wav.begin() + 36); u32(wav, 40, 4);
    require(decodeResourcePreview(wav, "voice").audio == wav);
    const auto ogg = testVorbis();
    require(decodeResourcePreview(ogg, "voice").audio == ogg);
    require(decodeResourcePreview(WavOggExtractor::embed(ogg), "voice").audio == ogg);
    wav.resize(13); rejected([&] { decodeResourcePreview(wav, "voice"); });
    const auto file = std::filesystem::temp_directory_path() / ("liarsoft-preview-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
    writeFileIfChanged(file.string(), {0, 1, 2, 3});
    try {
        std::filesystem::resize_file(file, 1024ULL * 1024 * 1024);
        const auto time = std::filesystem::last_write_time(file);
        const auto preview = previewResource(ResourceSource(file), "unknown.bin");
        require(preview.kind == PreviewKind::Unsupported);
        require(ResourceSource(file).read(0, 4) == std::vector<uint8_t>({0, 1, 2, 3}));
        require(std::filesystem::last_write_time(file) == time);
        std::filesystem::remove(file);
    } catch (...) { std::filesystem::remove(file); throw; }
    std::cout << "OK: in-memory image/alpha, encoded text, GSC and PCM audio; lazy unknown input\n";
}
