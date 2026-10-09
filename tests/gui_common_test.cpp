#include "gui_common.h"
#include "xflarchive.h"
#include "lim_decoder.h"
#include "fileio.h"
#include "wav_ogg.h"
#include "vorbis_fixture.h"
#include "gsc_decompiler.h"
#include "lwg_decoder.h"
#include "wcg_decoder.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

static std::vector<uint8_t> readBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read test output: " + path.string());
    return {(std::istreambuf_iterator<char>(in)), {}};
}

static void testParallelConversions(const std::filesystem::path& directory,
                                    const std::vector<uint8_t>& ogg,
                                    const liarsoft::LimImage& image) {
    namespace fs = std::filesystem;
    using namespace liarsoft;
    const auto root = directory / "parallel";
    const auto packed = directory / "parallel.xfl";
    const auto tsc = ";@gsc-byte-format modern-36\n;@gsc-schema modern\n"
                     "*TXT 0 0 0 0 \"\" \"original\" 0\n*end\n";
    const auto gsc = restoreGscFromTsc(tsc);
    const auto wcg = wcgEncode(image.pixels, image.width, image.height);
    const auto lim = limEncode(image);
    const auto wav = WavOggExtractor::embed(ogg);
    const auto sourceTime = fs::file_time_type::clock::now() - std::chrono::hours(24);
    const auto wcgImage = wcgDecode(wcg);
    for (bool wrapped : {false, true}) {
        std::vector<uint8_t> expectedArchive;
        std::vector<std::string> expectedPackWarnings;
        std::vector<std::string> expectedUnpackWarnings[2];
        std::map<std::string, std::vector<uint8_t>> expectedFiles;
        for (unsigned workers : {1u, 2u, 4u, 8u, 0u}) {
            fs::remove_all(root);
            fs::create_directories(root / "nested/scene");
            for (int i = 0; i < 16; ++i) {
                const auto stem = root / ("resource" + std::to_string(i));
                writeTextFileIfChanged(stem.string() + ".tsc", tsc);
                writeTextFileIfChanged(stem.string() + ".txt", "#original\n>wrong-priority\n");
                wcgSavePng(wcgImage, stem.string() + ".png");
                limSaveWebp(image, stem.string() + ".webp");
                writeFileIfChanged(stem.string() + ".ogg", ogg);
            }
            // A failed preferred source must allow the next one to succeed.
            writeTextFileIfChanged((root / "Fallback.tsc").string(), "*unknown-command\n");
            writeFileIfChanged((root / "FALLBACK.GSC").string(), gsc);
            writeTextFileIfChanged((root / "fallback.txt").string(), "#original\n>translated\n");
            writeTextFileIfChanged((root / "photo.png").string(), "broken");
            wcgSavePng(wcgImage, (root / "PHOTO.bmp").string());
            // Multiple failing groups, stale outputs, and a missing reference.
            for (const auto* name : {"bad0", "bad1", "bad2"}) {
                writeTextFileIfChanged((root / (std::string(name) + ".webp")).string(), "broken");
                writeFileIfChanged((root / (std::string(name) + ".lim")).string(), lim);
            }
            writeTextFileIfChanged((root / "orphan.txt").string(), "#text\n>translation\n");
            for (const auto* name : {"invalidraw0.wcg", "invalidraw1.lim", "invalidraw2.wav"})
                writeTextFileIfChanged((root / name).string(), "broken");
            writeTextFileIfChanged((root / "nested/scene/.meta.xml").string(),
                "<Canvas><Width>2</Width><Height>1</Height><Items>"
                "<Item x=\"0\" y=\"0\" flag=\"40\">background</Item></Items></Canvas>");
            writeFileIfChanged((root / "nested/scene/background.lim").string(), lim);
            limSaveWebp(image, (root / "nested/scene/background.webp").string());
            writeFileIfChanged((root / "nested/voice.ogg").string(), ogg);

            const auto warnings = packDirectoryToFile(root.string(), packed.string(),
                                                       "CP932", true, wrapped, workers);
            const auto archiveBytes = readBytes(packed);
            if (workers == 1) {
                expectedArchive = archiveBytes;
                expectedPackWarnings = warnings;
            } else if (archiveBytes != expectedArchive || warnings != expectedPackWarnings) {
                throw std::runtime_error("Parallel packing changed bytes or warning order");
            }
            if (warnings.size() != 22)
                throw std::runtime_error("Missing conversion failure/duplicate warning");
            const auto archive = XflArchive::fromFile(packed.string());
            for (const auto& entry : archive.entries) {
                if (entry.fileName == "resource0.gsc" && entry.data != gsc)
                    throw std::runtime_error("TSC/TXT source priority changed");
                if (entry.fileName.rfind("bad", 0) == 0)
                    throw std::runtime_error("Failed resource retained its stale target");
                if (entry.fileName == "FALLBACK.GSC" && entry.data == gsc)
                    throw std::runtime_error("TXT fallback did not update same-name GSC");
            }
            // Recreate the same destination so warnings and paths can compare exactly.
            fs::remove_all(root);
            fs::last_write_time(packed, sourceTime);
            archive.extractToDirectory(root.string(), sourceTime);
            for (bool tscOutput : {false, true}) {
                const auto unpackWarnings = unpackDirectoryRecursively(
                    root.string(), "CP932", tscOutput, workers);
                if (unpackWarnings.size() != 3)
                    throw std::runtime_error("Unpacking did not warn and continue after bad resources");
                if (workers == 1) expectedUnpackWarnings[tscOutput] = unpackWarnings;
                else if (unpackWarnings != expectedUnpackWarnings[tscOutput])
                    throw std::runtime_error("Parallel unpacking changed warning order");
            }
            std::map<std::string, std::vector<uint8_t>> actualFiles;
            for (const auto& entry : fs::recursive_directory_iterator(root)) {
                if (!entry.is_regular_file()) continue;
                if (fs::last_write_time(entry.path()) != sourceTime)
                    throw std::runtime_error("Parallel extraction changed timestamp inheritance");
                actualFiles.emplace(entry.path().lexically_relative(root).string(), readBytes(entry.path()));
            }
            if (workers == 1) expectedFiles = actualFiles;
            else if (actualFiles != expectedFiles)
                throw std::runtime_error("Parallel extraction changed resource contents or filenames");
            // Repeated conversion must also reuse upper-case targets from the index.
            if (!fs::exists(root / "FALLBACK.GSC") || fs::exists(root / "fallback.gsc") ||
                !fs::exists(root / "PHOTO.wcg") || !fs::exists(root / "nested/scene/background.webp"))
                throw std::runtime_error("Case-insensitive lookup or nested ordering changed");
        }
    }
    bool rejected = false;
    try { unpackDirectoryRecursively(root.string(), "CP932", false, MaxConversionWorkers + 1); }
    catch (const std::runtime_error&) { rejected = true; }
    if (!rejected) throw std::runtime_error("Invalid worker count accepted");
}

int main(int argc, char** argv) {
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
        fs::last_write_time(original, fs::file_time_type::clock::now() - std::chrono::hours(48));
        const auto originalTime = fs::last_write_time(original);
        const ConversionOptions options;
        convert(original.string(), webp.string(), options);
        const auto time = fs::last_write_time(webp);
        if (time != originalTime)
            throw std::runtime_error("Decoded WebP did not inherit LIM modification time");
        convert(original.string(), webp.string(), options);
        if (fs::last_write_time(webp) != time)
            throw std::runtime_error("Identical WebP was rewritten");
        convert(webp.string(), rebuilt.string(), options);
        if (fs::last_write_time(rebuilt) == originalTime)
            throw std::runtime_error("Packing unexpectedly inherited extraction timestamps");
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

        // A dated outer archive must propagate its time through every unpacking layer.
        const auto sceneSource = directory / "scene_source";
        fs::create_directory(sceneSource);
        liarsoft::writeTextFileIfChanged((sceneSource / ".meta.xml").string(),
            "<Canvas><Width>2</Width><Height>1</Height><Items>"
            "<Item x=\"0\" y=\"0\" flag=\"40\">background</Item></Items></Canvas>");
        liarsoft::writeFileIfChanged((sceneSource / "background.lim").string(),
                                    liarsoft::limEncode(image));
        liarsoft::XflArchive nested;
        nested.encoding = "CP932";
        nested.entries = {{"layout.xml", {'x'}}};
        liarsoft::XflArchive dated;
        dated.encoding = "CP932";
        dated.entries = {
            {"nested.xfl", nested.toBytes()},
            {"scene.lwg", liarsoft::LwgPacker::pack(sceneSource.string(), "CP932")},
            {"image.wcg", liarsoft::wcgEncode(image.pixels, image.width, image.height)},
            {"image.lim", liarsoft::limEncode(image)},
            {"voice.wav", liarsoft::WavOggExtractor::embed(clean)},
            {"script.gsc", liarsoft::restoreGscFromTsc(
                ";@gsc-byte-format modern-36\n;@gsc-schema modern\n"
                "*TXT 0 0 0 0 \"\" \"Text\" 0\n*end\n")}
        };
        const auto datedPath = directory / "dated.xfl";
        dated.save(datedPath.string());
        const auto output = directory / "dated";
        fs::create_directory(output);
        const auto unrelated = output / "keep.txt";
        liarsoft::writeTextFileIfChanged(unrelated.string(), "user file");
        const auto unrelatedTime = fs::last_write_time(unrelated);
        ConversionOptions unpack;
        unpack.recursive = true;
        unpack.gscToTsc = true;
        unpack.workers = 2;
        {
            auto invalid = unpack;
            invalid.workers = liarsoft::MaxConversionWorkers + 1;
            const auto rejectedOutput = directory / "invalid-workers";
            bool rejected = false;
            try { convert(datedPath.string(), rejectedOutput.string(), invalid); }
            catch (const std::runtime_error&) { rejected = true; }
            if (!rejected || fs::exists(rejectedOutput))
                throw std::runtime_error("Invalid GUI worker count modified outputs");
        }
        for (int pass = 0; pass < 2; ++pass) {
            fs::last_write_time(datedPath, originalTime + std::chrono::hours(pass));
            const auto expected = fs::last_write_time(datedPath);
            if (!convert(datedPath.string(), output.string(), unpack).empty())
                throw std::runtime_error("Dated archive unpacking failed");
            for (const auto* name : {"nested.xfl", "nested/layout.xml", "scene.lwg",
                     "scene/.meta.xml", "scene/background.lim", "scene/background.webp",
                     "image.wcg", "image.png", "image.lim", "image.webp", "voice.wav",
                     "voice.ogg", "script.gsc", "script.tsc"}) {
                if (fs::last_write_time(output / name) != expected)
                    throw std::runtime_error(std::string("Unpacked timestamp mismatch: ") + name);
            }
            if (fs::last_write_time(unrelated) != unrelatedTime)
                throw std::runtime_error("Unpacking retimed an unrelated existing file");
        }
        convert((output / "script.gsc").string(), (output / "script.txt").string(), options);
        if (fs::last_write_time(output / "script.txt") != fs::last_write_time(datedPath))
            throw std::runtime_error("Extracted TXT timestamp mismatch");
        convert((output / "scene.lwg").string(), (directory / "scene_direct").string(), options);
        if (fs::last_write_time(directory / "scene_direct/.meta.xml") != fs::last_write_time(datedPath))
            throw std::runtime_error("Direct LWG extraction timestamp mismatch");
        const auto staleOggTime = fs::last_write_time(ogg);
        convert(wav.string(), ogg.string(), options); // PCM: no extraction, leave the old Ogg alone.
        if (fs::last_write_time(ogg) != staleOggTime)
            throw std::runtime_error("Retained PCM retimed an unrelated Ogg");
        if (argc == 2) {
            const fs::path fixtures(argv[1]);
            fs::create_directories(fixtures);
            fs::copy_file(datedPath, fixtures / "dated.xfl", fs::copy_options::overwrite_existing);
            liarsoft::copyModificationTime(datedPath.string(), (fixtures / "dated.xfl").string());
        }

        const auto audioDirectory = directory / "audio";
        fs::create_directory(audioDirectory);
        liarsoft::writeFileIfChanged((audioDirectory / "voice.wav").string(), untouched);
        liarsoft::writeFileIfChanged((audioDirectory / "voice.ogg").string(),
                                    std::vector<uint8_t>{'b','a','d'});
        liarsoft::writeTextFileIfChanged((audioDirectory / "layout.xml").string(), "resource");
        ConversionOptions recursive;
        recursive.encoding = "CP932";
        recursive.recursive = true;
        recursive.workers = 4;
        const auto archive = directory / "audio.xfl";
        auto warnings = convert(audioDirectory.string(), archive.string(), recursive);
        const auto entries = liarsoft::XflArchive::fromFile(archive.string()).entries;
        if (warnings.size() != 1 || entries.size() != 1 || entries[0].fileName != "layout.xml")
            throw std::runtime_error("Failed recursive GUI audio included the stale WAV");

        const auto recoveryDirectory = directory / "recovery";
        fs::create_directory(recoveryDirectory);
        auto staleGsc = liarsoft::restoreGscFromTsc(
            ";@gsc-byte-format legacy-28\n;@gsc-schema pre-codex\n"
            "*datablock 0 0\n*TXT 0 0 0 0 \"\" \"Recovered text.\"\n*return 0\n");
        // The ASCII fixture leaves the period and NUL outside the declared pool.
        staleGsc[0] -= 2;
        staleGsc[16] -= 2;
        const auto recoveryInput = recoveryDirectory / "stale.gsc";
        const auto recoveryOutput = recoveryDirectory / "stale.tsc";
        liarsoft::writeFileIfChanged(recoveryInput.string(), staleGsc);
        ConversionOptions recoveryOptions;
        recoveryOptions.gscToTsc = true;
        const auto recoveredWarnings = convert(
            recoveryInput.string(), recoveryOutput.string(), recoveryOptions);
        if (recoveredWarnings.size() != 1 ||
            recoveredWarnings[0].find("Recovered legacy GSC") == std::string::npos ||
            fs::last_write_time(recoveryInput) != fs::last_write_time(recoveryOutput))
            throw std::runtime_error("GUI recovery warning or timestamp missing");
        for (unsigned workers : {1u, 4u}) {
            recoveryOptions.unpackOnly = true;
            recoveryOptions.workers = workers;
            if (convert(recoveryDirectory.string(), "", recoveryOptions) != recoveredWarnings)
                throw std::runtime_error("Recursive recovery warning differs from direct GUI conversion");
        }
        if (argc == 2) {
            const fs::path fixtures(argv[1]);
            liarsoft::writeFileIfChanged((fixtures / "stale.gsc").string(), staleGsc);
        }
        testParallelConversions(directory, clean, image);
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
