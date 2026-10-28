#include "preflight.h"
#include "pe_image.h"
#include "pe_fixture.h"
#include "fileio.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#if RSCRIPT_BUILD_GAME_MODULES
#include "unicode_state.h"
#include "gsc_decompiler.h"
#include "encoding.h"
#include "xflarchive.h"
#endif

namespace fs = std::filesystem;
using namespace liarsoft::runtime;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void fails(F action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Expected rejection");
}
void write(const fs::path& path, const std::vector<uint8_t>& data) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(data.data()), data.size()); output.close();
    require(bool(output), "Fixture write failed");
}
void write(const fs::path& path, const std::string& data) { write(path, std::vector<uint8_t>(data.begin(), data.end())); }

int main() {
    fs::path historyFolder;
    const auto root = fs::temp_directory_path() / fs::u8path("preflight 中文 Русский " +
        std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    try {
        PeFixture fixture;
        for (const auto* name : {"CreateFileA", "ReadFile", "SetFilePointer", "GetFileSize", "CloseHandle", "GetGlyphOutlineA"})
            fixture.addImport(name);
        require(PeImage(fixture.bytes).imports().size() == 6, "Named import traversal failed");
        for (const auto pos : {0u, 60u, 0x98u, 0x178u + 20, 0x98u + 104}) {
            auto bad = fixture; bad.put(pos, UINT32_MAX);
            fails([&] { PeImage(bad.bytes).imports(); });
        }
        auto bad = fixture; bad.put(bad.file(0x1000), UINT32_MAX);
        fails([&] { PeImage(bad.bytes).imports(); });
        bad = fixture; bad.bytes.resize(0x100);
        fails([&] { PeImage(bad.bytes); });
        CheckOptions options;
        options.game = root / "game" / fs::u8path("游戏 Русский.exe");
        options.root = root / "patch";
        options.launcher = root / "launcher.exe";
        options.host = root / "RScriptHost.dll";
        options.kinsoku = root / "kinsoku.json";
        fs::create_directories(options.root);
        write(options.game, fixture.bytes); write(options.launcher, PeFixture().bytes);
        PeFixture host(true); host.exportName("RScriptHost_Initialize"); write(options.host, host.bytes);
        PeFixture dll(true); dll.exportName("RScriptPlugin_GetDescriptor");
        for (const auto* name : {"RScriptFiles.dll", "RScriptResources.dll", "RScriptUnicode.dll", "third-party.dll"}) write(root / name, dll.bytes);
        write(options.kinsoku, "{\"version\":1,\"no_line_start\":\"。\",\"no_line_end\":\"（\"}");
        options.plugins = {root / "RScriptFiles.dll", root / "RScriptResources.dll", root / "RScriptUnicode.dll"};
        auto report = checkCompatibility(options, true);
        require(report.ok(), report.text().c_str());
#ifdef _WIN32
#if RSCRIPT_BUILD_GAME_MODULES
        const auto location = dictionaryLocation(options.game, options.root);
        require(!fs::exists(location.file.parent_path()), "Read-only check created a glyph dictionary directory");
#endif
#endif
        require(report.text().find("PENDING") != std::string::npos, "Unrecognized classifier was not pending");
        require(!checkCompatibility(options, true, [](size_t, size_t, const std::string&) { return false; }).ok(), "Cancellation passed");
        auto changed = options; changed.plugins = {root / "third-party.dll"};
        require(checkCompatibility(changed, false).ok(), "Third-party pending blocked startup");
        changed.plugins = {root / "RScriptUnicode.dll"};
        require(!checkCompatibility(changed, false).ok(), "Missing engine dependency passed");
        changed = options; changed.plugins.push_back(root / "RScriptFiles.dll");
        require(!checkCompatibility(changed, false).ok(), "Duplicate module passed");
        bad = dll; bad.put(0x84, 0x1c0, 2); write(root / "third-party.dll", bad.bytes);
        changed = options; changed.plugins = {root / "third-party.dll"};
        require(!checkCompatibility(changed, false).ok(), "Wrong DLL architecture passed");
        bad = fixture; bad.put(bad.file(0x1000 + 4 * 20), 0); write(options.game, bad.bytes);
        require(!checkCompatibility(options, false).ok(), "Malformed import passed");
        write(options.game, PeFixture().bytes);
#if RSCRIPT_BUILD_GAME_MODULES
        require(!checkCompatibility(options, false).ok(), "Missing file interfaces passed");
#endif
        write(options.game, fixture.bytes);
        write(options.kinsoku, "{}");
        require(!checkCompatibility(options, false).ok(), "Invalid kinsoku passed");
        write(options.kinsoku, "{\"version\":1,\"no_line_start\":\"。\",\"no_line_end\":\"（\"}");
#if RSCRIPT_BUILD_GAME_MODULES
        const std::string header = ";@gsc-byte-format modern-36\n;@gsc-schema rscript18\n";
        const auto script = [&](const std::string& text) { return header + "*font 1 0 0 0 0 \"" + text + "\"\n*end\n"; };
        write(options.root / "scr/0001.tsc", script(u8"简体中文 Русский 日本語"));
        write(options.root / "scr/0001.gsc", liarsoft::restoreGscFromTsc(script("native"), "CP932"));
        write(options.root / "scr/0002.tsc", "broken script\n");
        write(options.root / "scr/0003.tsc", script(u8"😀"));
        write(options.root / "scr/0004.tsc", script(u8"日本語"));
        report = checkCompatibility(options, true);
        require(!report.ok() && report.scripts == 4 && report.checked == 4 && report.failed == 2, report.text().c_str());
        auto light = checkCompatibility(options, false);
        require(light.ok() && light.checked == 0, "Light check compiled the patch");
        changed = options; changed.plugins.pop_back();
        report = checkCompatibility(changed, true);
        require(report.failed == 3, "CP932 errors not reported independently");
        fs::remove(options.root / "scr/0002.tsc"); fs::remove(options.root / "scr/0003.tsc");
        report = checkCompatibility(options, true);
        require(report.ok() && report.scripts == 2, report.text().c_str());
        write(root / "third-party.dll", dll.bytes);
        changed = options; changed.plugins.pop_back(); changed.plugins.push_back(root / "third-party.dll");
        require(checkCompatibility(changed, true).ok(), "Unverified third-party Unicode capability was treated as a definite error");
#ifdef _WIN32
        const auto history = dictionaryLocation(options.game, options.root);
        require(!fs::exists(history.file.parent_path()), "History fixture directory already exists");
        historyFolder = history.file.parent_path();
        GlyphTokens historyTokens;
        historyTokens.encode(u8"简");
        const auto historyBytes = dictionaryBytes(history.identity, historyTokens);
        write(history.file, historyBytes);
        const auto timestamp = fs::last_write_time(history.file);
        const auto historyReport = checkCompatibility(options, true);
        require(historyReport.ok() && historyReport.historical == 1 && historyReport.required < report.required,
                historyReport.text().c_str());
        require(liarsoft::readFile(history.file) == historyBytes && fs::last_write_time(history.file) == timestamp,
                "Preflight rewrote history dictionary");
        write(history.file, dictionaryBytes("different game identity", historyTokens));
        const auto wrongHistory = checkCompatibility(options, true);
        require(!wrongHistory.ok() && !wrongHistory.complete, "Wrong dictionary identity passed");
        fs::remove_all(historyFolder);
        historyFolder.clear();
#endif
        const auto before = liarsoft::readFile(options.root / "scr/0001.gsc");
        require(checkCompatibility(options, true).ok(), "Repeated check failed");
        require(before == liarsoft::readFile(options.root / "scr/0001.gsc"), "Check rewrote GSC");
        // A different header format must fail against the original loose GSC.
        write(options.game.parent_path() / "scr/0001.gsc", liarsoft::restoreGscFromTsc(
            ";@gsc-byte-format legacy-28\n;@gsc-schema early\n*end\n", "CP932"));
        report = checkCompatibility(options, true);
        require(!report.ok() && report.failed == 1, "Container mismatch passed");

        GlyphTokens tokens;
        tokens.reserveNative(std::string("\xf0\x40", 2));
        tokens.encode(u8"简");
        const auto state = tokens.serialize();
        auto usage = tokens.measure({u8"简㐀简 日本語 Русский"});
        require(usage.reserved == 1 && usage.historical == 1 && usage.required > 0, "Capacity counters are wrong");
        require(tokens.serialize() == state, "Capacity check allocated mappings");
        require(tokens.measure({u8"日本語 Русский"}).required == 0, "Native CP932 characters used new slots");
        const auto dictionary = dictionaryBytes("test identity", tokens);
        GlyphTokens restored;
        restoreDictionary(dictionary, "test identity", restored);
        require(restored.serialize() == state, "History restore changed mappings");
        fails([&] { restoreDictionary(dictionary, "wrong identity", restored); });
        std::string needed;
        for (unsigned cp = 0x3400; cp < 0x3400 + GlyphTokens::capacity - 2; ++cp)
            needed += std::string{char(0xe0 | cp >> 12), char(0x80 | (cp >> 6 & 63)), char(0x80 | (cp & 63))};
        usage = tokens.measure({needed});
        require(usage.fits && usage.remaining == 0, "Exact capacity did not fit");
        needed += "\xe3\xae\x98"; // U+3B98, outside the preceding fixture range.
        require(!tokens.measure({needed}).fits, "Over-capacity patch passed");
        require(tokens.serialize() == state, "Capacity failure changed dictionary");

        // Reserve loose, archive and static-data EUDC through the actual startup scanner.
        const auto nativeText = [](unsigned low) { return std::string{char(0xf0),char(low)}; };
        const auto nativeScript = [&](unsigned low) {
            return liarsoft::restoreGscFromTsc(script(liarsoft::convertEncoding(nativeText(low), "CP932", "UTF-8", true)), "CP932");
        };
        write(options.game.parent_path() / "original.gsc", nativeScript(0x40));
        liarsoft::XflArchive nested; nested.encoding = "CP932";
        nested.entries = {{"inside.gsc", nativeScript(0x41)}};
        liarsoft::XflArchive outer; outer.encoding = "CP932";
        outer.entries = {{"nested.xfl", nested.toBytes()}};
        write(options.game.parent_path() / "native.xfl", outer.toBytes());
        auto reservedImage = fixture;
        reservedImage.text(0x2a00, nativeText(0x42)); write(options.game, reservedImage.bytes);
        GlyphTokens scanned;
        reserveOriginalText(options.game, options.root, scanned);
        require(scanned.measure({}).reserved == 3, "Startup scanner missed native reserved slots");
        write(options.game.parent_path() / "damaged.xfl", "broken archive");
        report = checkCompatibility(options, true);
        require(!report.complete && !report.ok() && !report.capacityKnown, "Incomplete reservation scan reported success");
        require(report.checked == report.scripts, "One scan failure stopped patch compilation");
#else
        require(report.text().find("basic checks only") != std::string::npos, "Standalone limitation not reported");
#endif
        fs::remove_all(root);
        std::cout << "Preflight: PE, modules, read-only scripts, cancellation and glyph capacity passed\n";
    } catch (const std::exception& error) {
        if (!historyFolder.empty()) fs::remove_all(historyFolder);
        fs::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
