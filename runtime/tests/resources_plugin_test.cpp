#include "runtime_bridge.h"
#include "fileio.h"
#include "gsc_decompiler.h"
#include "lim_decoder.h"
#include "wcg_decoder.h"
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
namespace {
RScriptRuntimeBridge bridge{};
void* RSCRIPT_CALL get_api() { return &bridge; }
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void script(const fs::path& path, const std::string& format, const std::string& schema) {
    liarsoft::writeFileIfChanged(path.u8string(), liarsoft::restoreGscFromTsc(
        ";@gsc-byte-format " + format + "\n;@gsc-schema " + schema + "\n*end\n"));
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto root = fs::temp_directory_path() / ("rscript-forest-resources-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "scr");
    fs::create_directories(root / "original" / "scr");
    try {
        HMODULE module = LoadLibraryW(fs::u8path(argv[1]).c_str());
        require(module != nullptr, "Cannot load resource module");
        const auto entry = GetProcAddress(module, RSCRIPT_PLUGIN_ENTRY);
        RScriptGetDescriptor query = nullptr;
        std::memcpy(&query, &entry, sizeof(query));
        require(query != nullptr, "Missing descriptor");
        const auto* descriptor = query(RSCRIPT_PLUGIN_ABI);
        require(descriptor != nullptr, "Missing ABI");
        const auto rootString = root.wstring();
        const auto executable = (root / "original" / "AnyGame.exe").wstring();
        RScriptHostContext host{sizeof(host), RSCRIPT_PLUGIN_ABI, nullptr, module, executable.c_str(),
            rootString.c_str(), RSCRIPT_FEATURE_ENGINE | RSCRIPT_FEATURE_RESOURCES,
            nullptr, nullptr, get_api};
        bridge = {sizeof(bridge), 2, nullptr, &host, nullptr, nullptr};
        require(descriptor->initialize(&host) == 0 && bridge.load_resource, "Cannot initialize resources");
        script(root / "scr" / "0000.gsc", "legacy-28", "early");
        script(root / "original" / "scr" / "0000.gsc", "legacy-28", "early");
        RScriptBuffer output{};
        const char* error = nullptr;
        require(bridge.load_resource(L"scr\\0000.gsc", RScriptScript, &output, &error) == 1,
                "Legacy script was rejected");
        require(output.size >= 28 && output.data[4] == 28, "Wrong script layout");
        output.release(output.owner);
        script(root / "scr" / "0000.gsc", "modern-36", "modern");
        output = {};
        require(bridge.load_resource(L"scr\\0000.gsc", RScriptScript, &output, &error) == -1 &&
                error && std::string(error).find("original GSC") != std::string::npos && !output.release,
                "Incompatible script was delivered");
        script(root / "original" / "scr" / "0000.gsc", "modern-36", "modern");
        require(bridge.load_resource(L"scr\\0000.gsc", RScriptScript, &output, &error) == 1 && output.data[4] == 36,
                "Modern script was rejected despite matching the original");
        output.release(output.owner);
        output = {};
        // A selected incompatible TSC must not fall back to a valid native GSC.
        script(root / "scr" / "0000.gsc", "legacy-28", "early");
        script(root / "original" / "scr" / "0000.gsc", "legacy-28", "early");
        const std::string tsc = ";@gsc-byte-format modern-36\n;@gsc-schema modern\n*end\n";
        liarsoft::writeFileIfChanged((root / "scr" / "0000.tsc").u8string(),
            std::vector<uint8_t>(tsc.begin(), tsc.end()));
        require(bridge.load_resource(L"scr\\0000.gsc", RScriptScript, &output, &error) == -1,
                "Incompatible TSC silently fell back to GSC");
        fs::create_directory(root / "grpe");
        const liarsoft::LimImage image{1, 1, {11, 22, 33, 64}};
        liarsoft::writeFileIfChanged((root / "grpe" / "9001.webp").u8string(), liarsoft::webpEncode(image));
        require(bridge.load_resource(L"grpe\\9001.lim", RScriptImage, &output, &error) == 1,
                "Cannot supply WebP as LIM");
        const auto lim = liarsoft::limDecode({output.data, output.data + output.size});
        require(lim.width == 1 && lim.height == 1 && lim.pixels == image.pixels, "LIM conversion changed RGBA pixels");
        output.release(output.owner);
        require(bridge.load_resource(L"grpe\\9001.wcg", RScriptImage, &output, &error) == 1,
                "Cannot supply WebP as WCG");
        const auto wcg = liarsoft::wcgDecode({output.data, output.data + output.size});
        require(wcg.width == 1 && wcg.height == 1 && wcg.pixels == std::vector<uint8_t>({33, 22, 11, 64}),
                "WCG conversion changed BGRA pixels");
        output.release(output.owner);
        descriptor->shutdown();
        require(!bridge.load_resource, "Resource callback survived shutdown");
        FreeLibrary(module);
        fs::remove_all(root);
        std::puts("Resource module uses original script layouts, without game-specific headers");
        return 0;
    } catch (const std::exception& error) {
        fs::remove_all(root);
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
