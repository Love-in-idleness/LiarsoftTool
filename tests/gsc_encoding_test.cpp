#include "conversion.h"
#include "encoding.h"
#include "fileio.h"
#include "gsc_decompiler.h"
#include "resource_preview.h"
#include "transfile.h"
#include "xflarchive.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace liarsoft;
namespace fs = std::filesystem;
static void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> static void rejects(F action) {
    try { action(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Inconclusive or malformed input was accepted");
}
static std::vector<uint8_t> gsc(const std::string& text, const std::string& encoding,
                               const std::string& format = "modern-36") {
    return restoreGscFromTsc(";@gsc-byte-format " + format +
        "\n;@gsc-schema " + (format == "modern-36" ? "modern" : "early") +
        "\n*TXT 0 0 0 0 \"\" \"" + text + "\" 0\n*end\n", encoding);
}
int main(int argc, char** argv) {
    const auto directory = fs::temp_directory_path() / ("liarsoft-detect-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const std::vector<std::pair<std::string, std::string>> samples = {
            {"CP932", "今日はよい天気ですね。私たちは静かな森の中を歩いていました。遠くから鳥の声が聞こえてきます。彼女は窓を開けて青い空を見上げました。"},
            {"GBK", "今天的天气很好，我们一起走进安静的森林。远处传来了鸟儿的声音，她打开窗户望着蓝色的天空。他们正在讨论这个故事里面发生的事情。"},
            {"CP950", "今天的天氣很好，我們一起走進安靜的森林。遠處傳來了鳥兒的聲音，她打開窗戶望著藍色的天空。他們正在討論這個故事裡面發生的事情。"},
            {"CP1251", "Сегодня была хорошая погода. Мы вместе гуляли по тихому лесу и слушали пение птиц. Она открыла окно и посмотрела на голубое небо. История только начинается."},
            {"CP949", "오늘은 날씨가 아주 좋습니다. 우리는 함께 조용한 숲속을 걸어가며 새들의 노랫소리를 들었습니다. 그녀는 창문을 열고 푸른 하늘을 바라보았습니다."},
            {"GB18030", "今天的天气很好，我们一起走进安静的森林。远处传来了鸟儿的声音。这个字符需要四个字节才能正确编码：𠀀。"},
        };
        for (const auto& [encoding, text] : samples) {
#ifdef _WIN32
            // Wine currently lacks code page 54936; native Windows still exercises it.
            if (encoding == "GB18030" && !IsValidCodePage(54936)) continue;
#endif
            for (const auto* format : {"modern-36", "legacy-28"}) {
                const auto data = gsc(text, encoding, format);
                std::vector<std::string> warnings;
                const auto detected = detectGscEncoding(data, &warnings);
                require(detected == encoding, encoding + " detected as " + detected);
                require(!warnings.empty(), "Guesses need a visible notice");
                const auto preview = decodeResourcePreview(data, "scene.gsc");
                require(preview.encoding == encoding && preview.text.find(text) != std::string::npos,
                        "Preview did not use detected encoding");
                require(restoreGscFromTsc(preview.text, encoding) == data, "Detected TSC roundtrip differs");
            }
        }
        const auto ascii = gsc("hello", "CP932");
        rejects([&] { detectGscEncoding(gsc("日本語と中文とРусскийを含む文章です。", "UTF-8")); });
        for (const auto* alias : {"UTF-8", "utf8", "65001", "cp65001"}) {
            rejects([&] { validateResourceEncoding(alias); });
            auto options = conversion::ConversionOptions{}; options.encoding = alias;
            rejects([&] { conversion::convert("unused.gsc", "unused.tsc", options); });
        }
        require(detectGscEncoding(ascii) == "CP932", "ASCII fallback changed");
        require(detectGscEncoding(restoreGscFromTsc(
            ";@gsc-byte-format modern-36\n;@gsc-schema modern\n*end\n")) == "CP932", "Empty script detection");
        const auto shortText = gsc("日本語", "CP932");
        if (argc == 2) {
            const fs::path fixtures(argv[1]);
            fs::create_directories(fixtures);
            writeFileIfChanged((fixtures / "chinese.gsc").string(), gsc(samples[1].second, "GBK"));
            writeFileIfChanged((fixtures / "short.gsc").string(), shortText);
            XflArchive archive;
            archive.encoding = "CP932";
            archive.entries = {{"chinese.gsc", gsc(samples[1].second, "GBK")}, {"empty.gsc", ascii}};
            archive.save((fixtures / "archive.xfl").string());
        }
        rejects([&] { detectGscEncoding(shortText); });
        rejects([] { detectGscEncoding({0, 1, 2}); });
        auto unknown = gsc(samples[0].second, "CP932");
        unknown[36] = 0xfe; unknown[37] = 0x0f;
        require(detectGscEncoding(unknown) == "CP932", "Unknown opcode prevented string detection");
        const auto u32 = [](const std::vector<uint8_t>& bytes, size_t offset) {
            uint32_t value = 0;
            for (size_t i = 0; i < 4; ++i) value |= uint32_t(bytes.at(offset + i)) << (8 * i);
            return value;
        };
        auto invalid = gsc(samples[0].second, "CP932");
        const auto offsets = 36 + u32(invalid, 8);
        std::fill_n(invalid.begin() + offsets, 4, 0xff);
        rejects([&] { detectGscEncoding(invalid); });
        invalid = gsc(samples[0].second, "CP932");
        const auto stringsStart = offsets + u32(invalid, 12);
        std::replace(invalid.begin() + stringsStart,
                     invalid.begin() + stringsStart + u32(invalid, 16), uint8_t(0), uint8_t('x'));
        rejects([&] { detectGscEncoding(invalid); });
        for (size_t i = 0; i < ascii.size(); ++i)
            rejects([&] { detectGscEncoding({ascii.begin(), ascii.begin() + i}); });

        fs::create_directories(directory / "nested");
        const auto input = directory / "nested/scene.gsc";
        const auto output = directory / "nested/scene.tsc";
        const auto data = gsc(samples[1].second, "GBK");
        writeFileIfChanged(input.string(), data);
        const auto time = fs::last_write_time(input);
        conversion::ConversionOptions options;
        options.gscToTsc = true;
        require(options.encoding == "auto", "Default encoding is not auto");
        const auto result = conversion::convert(input.string(), output.string(), options);
        require(!result.warnings.empty(), "Detection result missing from conversion");
        require(readFile(input.string()) == data && fs::last_write_time(input) == time, "Input changed");
        const auto expected = readFile(output.string());
        require(restoreGscFromTsc(std::string(expected.begin(), expected.end()), "GBK") == data, "Conversion result differs");
        require(fs::last_write_time(output) == time, "Output timestamp differs");
        options.encoding = "GBK";
        conversion::convert(input.string(), output.string(), options);
        require(readFile(output.string()) == expected, "Manual and detected output differ");
        options.encoding = "auto"; options.gscToTsc = false;
        conversion::convert(input.string(), (directory / "scene.txt").string(), options);
        require(!TransFile::fromFile((directory / "scene.txt").string()).strings.empty(), "TXT export is empty");
        const auto warnings = unpackDirectoryRecursively(directory.string(), "auto", true, 2);
        require(!warnings.empty() && readFile(output.string()) == expected, "Recursive detection differs");
        writeFileIfChanged(input.string(), shortText);
        options.gscToTsc = true;
        rejects([&] { conversion::convert(input.string(), output.string(), options); });
        require(readFile(output.string()) == expected, "Failed detection overwrote previous output");
        rejects([&] { conversion::convert(output.string(), input.string(), options); });
        require(readFile(input.string()) == shortText, "Auto target encoding overwrote GSC");
        fs::remove_all(directory);
        std::cout << "OK: GSC-aware encoding detection, conversion, preview, roundtrip and failure safety\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; fs::remove_all(directory); return 1;
    }
}
