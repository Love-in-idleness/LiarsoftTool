#include "resource_provider.h"
#include "fileio.h"
#include "gsc_decompiler.h"
#include "lwg_decoder.h"
#include "stb_image_write.h"
#include "wav_ogg.h"
#include "wcg_decoder.h"
#include "xflarchive.h"
#include "../../tests/vorbis_fixture.h"
#include <chrono>
#include <iostream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace liarsoft;
using namespace liarsoft::runtime;
#define require(condition) do { if (!(condition)) throw std::runtime_error("Runtime resource check failed: " #condition); } while (false)
template<class F> static void rejected(F action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid resource was accepted");
}
static void text(const fs::path& path, const std::string& value) {
    writeFileIfChanged(path.u8string(), std::vector<uint8_t>(value.begin(), value.end()));
}
static std::vector<uint8_t> png(const LimImage& image) {
    std::vector<uint8_t> output;
    auto append = [](void* context, void* bytes, int size) {
        auto& data = *static_cast<std::vector<uint8_t>*>(context);
        const auto* first = static_cast<uint8_t*>(bytes);
        data.insert(data.end(), first, first + size);
    };
    require(stbi_write_png_to_func(append, &output, image.width, image.height, 4,
                                  image.pixels.data(), image.width * 4));
    return output;
}
static std::string meta(const std::string& items) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Canvas>\n<Width>800</Width>"
        "<Height>600</Height><Items>" + items + "</Items></Canvas>\n";
}
struct Snapshot { std::vector<uint8_t> bytes; fs::file_time_type time; };
static std::map<fs::path, Snapshot> snapshot(const fs::path& root) {
    std::map<fs::path, Snapshot> result;
    for (const auto& entry : fs::recursive_directory_iterator(root))
        if (entry.is_regular_file()) result.emplace(entry.path(), Snapshot{readFile(entry.path()), entry.last_write_time()});
    return result;
}
int main() {
    const auto root = fs::temp_directory_path() / ("liarsoft-runtime-resource-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "grps" / "panel");
    fs::create_directory(root / "scr"); fs::create_directory(root / "voice");
    try {
        const LimImage webpImage{1, 1, {11, 22, 33, 64}};
        const LimImage pngImage{1, 1, {44, 55, 66, 128}};
        const LimImage limImage{1, 1, {77, 88, 99, 192}};
        const LimImage wcgImage{1, 1, {111, 122, 133, 255}};
        const auto image = root / "grps" / "image";
        writeFileIfChanged(image.u8string() + ".WebP", webpEncode(webpImage));
        writeFileIfChanged(image.u8string() + ".png", png(pngImage));
        writeFileIfChanged(image.u8string() + ".lim", limEncode(limImage));
        writeFileIfChanged(image.u8string() + ".wcg", wcgEncode(wcgImage.pixels, 1, 1));
        require(!loadOverride(root, "grps/missing.wcg", ResourceKind::Image));
        auto selected = loadOverride(root, "GRPS\\IMAGE.WCG", ResourceKind::Image);
        require(selected && decodeRuntimeImage(selected->bytes, ".wcg").pixels == webpImage.pixels);
        fs::remove(image.u8string() + ".WebP");
        require(decodeRuntimeImage(loadOverride(root, "grps/image", ResourceKind::Image)->bytes, ".wcg").pixels == pngImage.pixels);
        fs::remove(image.u8string() + ".png");
        require(decodeRuntimeImage(loadOverride(root, "grps/image", ResourceKind::Image)->bytes, ".wcg").pixels == limImage.pixels);
        fs::remove(image.u8string() + ".lim");
        require(decodeRuntimeImage(loadOverride(root, "grps/image", ResourceKind::Image)->bytes, ".wcg").pixels == wcgImage.pixels);
        text(image.u8string() + ".webp", "damaged");
        rejected([&] { loadOverride(root, "grps/image", ResourceKind::Image); });
        fs::remove(image.u8string() + ".webp");

        const std::string header = ";@gsc-byte-format modern-36\n;@gsc-schema modern\n";
        const auto nativeScript = restoreGscFromTsc(header + "*end\n");
        const auto originalRoot = root / "original";
        fs::create_directory(originalRoot);
        XflArchive scripts;
        scripts.encoding = "CP932";
        const auto legacyScript = restoreGscFromTsc(";@gsc-byte-format legacy-28\n;@gsc-schema early\n*end\n");
        scripts.entries.push_back({"2100.gsc", legacyScript});
        scripts.save((originalRoot / "chapterset.xfl").u8string());
        rejected([&] { validateScriptOverride(nativeScript, originalRoot, "chapterset/2100.gsc"); });
        validateScriptOverride(legacyScript, originalRoot, "chapterset/2100.gsc");
        validateScriptOverride(nativeScript, originalRoot, "chapterset/2101.gsc");
        scripts.entries[0].data = nativeScript;
        scripts.save((originalRoot / "chapterset.xfl").u8string());
        validateScriptOverride(nativeScript, originalRoot, "chapterset/2100.gsc");
        fs::create_directory(originalRoot / "chapterset");
        writeFileIfChanged((originalRoot / "chapterset" / "2100.gsc").u8string(), legacyScript);
        rejected([&] { validateScriptOverride(nativeScript, originalRoot, "chapterset/2100.gsc"); });
        writeFileIfChanged((root / "scr" / "2100.gsc").u8string(), nativeScript);
        const std::string unicodeScript = header + "*font 1 0 0 0 0 \"日本中文Русский😀\"\n*end\n";
        text(root / "scr" / "2100.tsc", unicodeScript);
        rejected([&] { loadOverride(root, "scr/2100.gsc", ResourceKind::Script); });
        const auto compiled = loadOverride(root, "scr/2100.gsc", ResourceKind::Script, true);
        require(compiled && compiled->utf8Script && compiled->source.extension() == ".tsc");
        require(decompileGscBytes(compiled->bytes, "UTF-8").find("日本中文Русский😀") != std::string::npos);
        text(root / "scr" / "2100.tsc", header + "*missing\n");
        rejected([&] { loadOverride(root, "scr/2100", ResourceKind::Script, true); });
        text(root / "scr" / "2100.tsc", header + "*font 1 0 0 0 0 \";@gsc-raw-v1 example\"\n*end\n");
        require(loadOverride(root, "scr/2100", ResourceKind::Script)->source.extension() == ".tsc");
        text(root / "scr" / "2100.tsc", ";@gsc-raw-v1 size=0 fnv1a64=0\n");
        rejected([&] { loadOverride(root, "scr/2100", ResourceKind::Script, true); });
        fs::remove(root / "scr" / "2100.tsc");
        require(loadOverride(root, "scr/2100", ResourceKind::Script)->bytes == nativeScript);
        require(!loadOverride(root, "scr/2100", ResourceKind::Script, true)->utf8Script);
        text(root / "scr" / "bad.gsc", "broken");
        rejected([&] { loadOverride(root, "scr/bad", ResourceKind::Script); });

        const auto ogg = testVorbis();
        const auto pcm = WavOggExtractor::decodeToPcm(ogg);
        writeFileIfChanged((root / "voice" / "line.ogg").u8string(), ogg);
        text(root / "voice" / "line.wav", "must not be selected");
        require(loadOverride(root, "voice/line.wav", ResourceKind::Audio)->bytes == pcm);
        text(root / "voice" / "line.ogg", "damaged");
        writeFileIfChanged((root / "voice" / "line.wav").u8string(), pcm);
        rejected([&] { loadOverride(root, "voice/line", ResourceKind::Audio); });
        fs::remove(root / "voice" / "line.ogg");
        require(loadOverride(root, "voice/line", ResourceKind::Audio)->bytes == pcm);
        writeFileIfChanged((root / "voice" / "line.wav").u8string(), WavOggExtractor::embed(ogg));
        require(loadOverride(root, "voice/line", ResourceKind::Audio)->bytes == pcm);

        const auto panel = root / "grps" / "panel";
        const std::string items = "<Item x=\"-14\" y=\"29\" flag=\"40\">face.wcg</Item>"
            "<Item x=\"4\" y=\"8\" flag=\"1\" empty=\"1\">face.wcg</Item>";
        text(panel / ".meta.xml", meta(items));
        writeFileIfChanged((panel / "face.wcg.webp").u8string(), webpEncode(webpImage));
        writeFileIfChanged((panel / "face.wcg.png").u8string(), png(pngImage));
        text(root / "grps" / "panel.lwg", "lower priority invalid archive");
        const auto componentResult = loadOverride(root, "grps/panel.lwg", ResourceKind::Component);
        require(componentResult && componentResult->source.filename() == ".meta.xml");
        const auto archive = LwgDecoder::decode(componentResult->bytes, "CP932");
        require(archive.width == 800 && archive.height == 600 && archive.entries.size() == 2);
        require(archive.entries[0].name == "face.wcg" && archive.entries[0].x == -14 &&
            archive.entries[0].y == 29 && archive.entries[0].flag == 40);
        require(decodeRuntimeImage(archive.entries[0].data, ".wcg").pixels == webpImage.pixels);
        require(archive.entries[1].name == "face.wcg" && archive.entries[1].data.empty());
        const auto before = snapshot(root);
        loadOverride(root, "grps/panel", ResourceKind::Component);
        loadOverride(root, "grps/image", ResourceKind::Image);
        loadOverride(root, "voice/line", ResourceKind::Audio);
        const auto after = snapshot(root);
        require(before.size() == after.size());
        for (const auto& entry : before) {
            require(after.at(entry.first).bytes == entry.second.bytes);
            require(after.at(entry.first).time == entry.second.time);
        }
        for (const auto& invalid : {meta(""), meta("<Item x=\"0\" y=\"0\" flag=\"40\">missing</Item>"),
            meta("<Item x=\"0\" y=\"0\" flag=\"256\">face.wcg</Item>"),
            meta("<Item x=\"2147483648\" y=\"0\" flag=\"40\">face.wcg</Item>"),
            meta("<Item x=\"0\" y=\"0\" flag=\"40\">unfinished")}) {
            text(panel / ".meta.xml", invalid);
            rejected([&] { loadOverride(root, "grps/panel", ResourceKind::Component); });
        }
        fs::remove(panel / ".meta.xml");
        writeFileIfChanged((root / "grps" / "panel.lwg").u8string(), componentResult->bytes);
        require(loadOverride(root, "grps/panel", ResourceKind::Component)->bytes == componentResult->bytes);

        for (const auto* invalid : {"../outside.wcg", "/absolute.wcg", "C:\\outside.wcg", "grps/../../outside", "\\\\server\\file"})
            rejected([&] { loadOverride(root, invalid, ResourceKind::Image); });
        std::error_code error;
        fs::create_directory_symlink(root.parent_path(), root / "escape", error);
        if (!error) rejected([&] { loadOverride(root, "escape/image", ResourceKind::Image); });
        fs::remove_all(root);
        std::cout << "OK: override priority, in-memory codecs/TSC/components, strict failures and read-only inputs\n";
    } catch (...) { fs::remove_all(root); throw; }
}
