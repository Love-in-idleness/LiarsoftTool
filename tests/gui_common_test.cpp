#include "gui_common.h"
#include "xflarchive.h"
#include "lim_decoder.h"
#include "fileio.h"
#include "wav_ogg.h"
#include "vorbis_fixture.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    using namespace liarsoft::gui;
    namespace fs = std::filesystem;

    assert(extension("VOICE.WAV") == ".wav");
    assert(replaceExtension("scene.gsc", ".tsc") == "scene.tsc");
    assert(guessOutput("/game/scene.gsc", "/out", true, "CP932") ==
           "/out/scene.tsc");
    assert(guessOutput("/game/voice.ogg", "", false, "CP932") ==
           "/game/voice.wav");
    assert(guessOutput("/game/start.exe", "", false, "CP1251") ==
           "/game/start.cp1251.exe");
    assert(guessType("VOICE.OGG", false, "CP932") == "OGG -> WAV");
    assert(isSupported("IMAGE.JPEG"));
    assert(!isSupported("README.md"));

    if (guessOutput("/game/image.LIM", "", false, "CP932") !=
            (fs::path("/game") / "image.webp").string() ||
        guessOutput("/game/image.WEBP", "/out", false, "CP932") !=
            (fs::path("/out") / "image.lim").string() ||
        guessType("image.lim", false, "CP932") != "LIM -> WebP" ||
        guessType("image.webp", false, "CP932") != "WebP -> LIM" ||
        !isSupported("image.WEBP") ||
        !liarsoft::matchesOperationMode("image.webp", true, false, false) ||
        liarsoft::matchesOperationMode("image.webp", false, true, false) ||
        liarsoft::matchesOperationMode("image.webp", true, true, false)) {
        std::cerr << "GUI WebP detection, routing or operation modes are incorrect\n";
        return 1;
    }
    const auto directory = fs::temp_directory_path() / ("liarsoft-webp-gui-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(directory);
    try {
        const auto original = directory / "original.lim";
        const auto webp = directory / "image.webp";
        const auto rebuilt = directory / "rebuilt.lim";
        const liarsoft::LimImage image{2, 1, {17, 34, 51, 0, 67, 89, 123, 127}};
        liarsoft::writeFileIfChanged(original.string(), liarsoft::limEncode(image));
        const ConversionOptions options;
        convert(original.string(), webp.string(), options);
        const auto time = fs::last_write_time(webp);
        convert(original.string(), webp.string(), options);
        if (fs::last_write_time(webp) != time)
            throw std::runtime_error("Identical WebP was rewritten");
        convert(webp.string(), rebuilt.string(), options);
        std::ifstream input(rebuilt, std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
        if (liarsoft::limDecode(bytes).pixels != image.pixels)
            throw std::runtime_error("GUI LIM/WebP round trip changed pixels");

        const auto ogg = directory / "voice.ogg";
        const auto wav = directory / "voice.wav";
        const std::vector<uint8_t> untouched{'o','r','i','g','i','n','a','l'};
        liarsoft::writeFileIfChanged(ogg.string(), std::vector<uint8_t>{'b','a','d'});
        liarsoft::writeFileIfChanged(wav.string(), untouched);
        const auto wavTime = fs::last_write_time(wav);
        const auto absent = directory / "absent.wav";
        for (bool wrapped : {false, true}) {
            ConversionOptions mode = options;
            mode.vorbisInWav = wrapped;
            for (const auto& output : {wav, absent}) {
                bool rejected = false;
                try { convert(ogg.string(), output.string(), mode); }
                catch (const std::runtime_error&) { rejected = true; }
                if (!rejected || fs::exists(absent) || fs::last_write_time(wav) != wavTime)
                    throw std::runtime_error("Invalid GUI audio modified an output file");
            }
        }
        const auto clean = testVorbis();
        liarsoft::writeFileIfChanged(ogg.string(), clean);
        ConversionOptions mode = options;
        mode.referencePath = (directory / "nonexistent-reference.wav").string();
        convert(ogg.string(), wav.string(), mode);
        std::ifstream pcmInput(wav, std::ios::binary);
        const std::vector<uint8_t> pcmBytes((std::istreambuf_iterator<char>(pcmInput)), {});
        if (pcmBytes != liarsoft::WavOggExtractor::decodeToPcm(clean))
            throw std::runtime_error("Default GUI conversion did not decode PCM without reference");
        const auto pcmTime = fs::last_write_time(wav);
        convert(ogg.string(), wav.string(), mode);
        if (fs::last_write_time(wav) != pcmTime)
            throw std::runtime_error("Identical PCM WAV was rewritten");
        mode.vorbisInWav = true;
        convert(ogg.string(), absent.string(), mode);
        std::ifstream wrappedInput(absent, std::ios::binary);
        const std::vector<uint8_t> wrappedBytes((std::istreambuf_iterator<char>(wrappedInput)), {});
        if (liarsoft::WavOggExtractor::extract(wrappedBytes) != clean)
            throw std::runtime_error("GUI compressed wrapping required a reference or changed Ogg");

        const auto audioDirectory = directory / "audio";
        fs::create_directory(audioDirectory);
        liarsoft::writeFileIfChanged((audioDirectory / "voice.wav").string(), untouched);
        liarsoft::writeFileIfChanged((audioDirectory / "voice.ogg").string(),
                                    std::vector<uint8_t>{'b','a','d'});
        liarsoft::writeTextFileIfChanged((audioDirectory / "layout.xml").string(), "resource");
        ConversionOptions recursive;
        recursive.encoding = "CP932";
        recursive.recursive = true;
        const auto archive = directory / "audio.xfl";
        auto warnings = convert(audioDirectory.string(), archive.string(), recursive);
        const auto entries = liarsoft::XflArchive::fromFile(archive.string()).entries;
        if (warnings.size() != 1 || entries.size() != 1 || entries[0].fileName != "layout.xml")
            throw std::runtime_error("Failed recursive GUI audio included the stale WAV");
    } catch (const std::exception& error) {
        fs::remove_all(directory);
        std::cerr << error.what() << '\n';
        return 1;
    }
    fs::remove_all(directory);

    // Explicit checks also run in Release builds, where assert is disabled.
    std::vector<ConversionDiagnostic> diagnostics;
    if (!formatDiagnostics(diagnostics).empty()) return 1;
    for (int i = 0; i < 25; ++i)
        diagnostics.push_back({false, "入力/voice.wav", "出力/voice.ogg",
                               "warning " + std::to_string(i)});
    diagnostics.push_back({true, "bad.gsc", "bad.tsc", "failure\nwith details"});
    const auto report = formatDiagnostics(diagnostics);
    for (const auto* expected : {"Errors: 1\nWarnings: 25", "[Warning]", "[Error]",
             "Input: 入力/voice.wav", "Output: 出力/voice.ogg", "warning 24\n",
             "Input: bad.gsc", "Output: bad.tsc", "failure\nwith details"}) {
        if (report.find(expected) == std::string::npos) {
            std::cerr << "Missing diagnostic detail: " << expected << '\n';
            return 1;
        }
    }
}
