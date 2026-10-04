#include "gui_common.h"

#include <cassert>
#include <iostream>

int main() {
    using namespace liarsoft::gui;

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
