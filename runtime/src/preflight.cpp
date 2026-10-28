#include "preflight.h"
#include "pe_image.h"
#include "fileio.h"
#include "kinsoku.h"
#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <sstream>
#if RSCRIPT_BUILD_GAME_MODULES
#include "resource_provider.h"
#include "native_kinsoku.h"
#include "unicode_state.h"
#include "gscfile.h"
#include "encoding.h"
#include "archive_index.h"
#endif

namespace liarsoft::runtime {
namespace fs = std::filesystem;
namespace {
struct Cancelled {};
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
    return text;
}
}
bool CheckResult::ok() const {
    return !cancelled && complete && std::none_of(issues.begin(), issues.end(),
        [](const CheckIssue& issue) { return issue.severity == CheckSeverity::Error; });
}
std::string CheckResult::text() const {
    std::ostringstream out;
    for (const auto& issue : issues) {
        out << (issue.severity == CheckSeverity::Error ? "ERROR" : issue.severity == CheckSeverity::Pending ? "PENDING" : "INFO")
            << " [" << issue.source << "] " << issue.message << '\n';
    }
    out << "Scripts: " << checked << '/' << scripts << " checked, " << failed << " failed\n";
    if (capacityKnown) out << "Glyph slots (1880): reserved=" << reserved << ", historical=" << historical
        << ", new=" << required << ", remaining=" << remaining << '\n';
    out << (cancelled ? "CANCELLED: check is incomplete" : !complete ? "INCOMPLETE: not all inputs were checked" :
            !ok() ? "FAILED: fix the reported errors" : "No definite errors; pending items still require DLL initialization") << '\n';
    return out.str();
}

CheckResult checkCompatibility(const CheckOptions& supplied, bool full, const CheckProgress& progress) {
    CheckResult result;
    auto issue = [&](CheckSeverity severity, const std::string& source, const std::string& message) {
        result.issues.push_back({severity, source, message});
    };
    auto tick = [&](size_t done, size_t total, const std::string& source) {
        if (progress && !progress(done, total, source)) throw Cancelled{};
    };
    auto inspect = [&](const fs::path& path, bool dll, uint16_t machine, const std::string& entry = {}) -> std::optional<PeImage> {
        tick(0, 0, path.u8string());
        try {
            auto image = PeImage::read(path);
            if (image.dll != dll) throw std::runtime_error(dll ? "Expected a DLL" : "Expected a game executable");
            if (machine && image.machine != machine) throw std::runtime_error("Architecture does not match the game");
            image.imports();
            if (!entry.empty() && !image.exports(entry) && !image.exports(entry + "@4"))
                throw std::runtime_error("Required DLL export is missing: " + entry);
            issue(CheckSeverity::Info, path.u8string(), "PE structure and architecture checked" +
                  (entry.empty() ? std::string{} : "; required export found"));
            return image;
        } catch (const std::exception& error) { issue(CheckSeverity::Error, path.u8string(), error.what()); return {}; }
    };
    try {
        if (supplied.game.empty()) throw std::runtime_error("A game executable is required");
        auto options = supplied;
        options.game = fs::absolute(options.game).lexically_normal();
        options.root = options.root.empty() ? options.game.parent_path() : fs::absolute(options.root).lexically_normal();
        if (!fs::is_directory(options.root)) throw std::runtime_error("Resource directory does not exist: " + options.root.u8string());
        if (options.plugins.size() > 64) throw std::runtime_error("Too many enabled DLLs (maximum 64)");
        auto game = inspect(options.game, false, 0);
        if (!game) { result.complete = false; return result; }
        bool files = false, resources = false, unicode = false, thirdParty = false;
        std::set<fs::path> selected;
        for (const auto& path : options.plugins) {
            const auto name = lower(path.filename().u8string());
            const auto canonical = fs::canonical(path);
            if (std::any_of(selected.begin(), selected.end(), [&](const fs::path& known) { return fs::equivalent(canonical, known); }))
                issue(CheckSeverity::Error, path.u8string(), "The same DLL is selected more than once");
            selected.insert(canonical);
            inspect(path, true, game->machine, "RScriptPlugin_GetDescriptor");
            files |= name == "rscriptfiles.dll";
            resources |= name == "rscriptresources.dll";
            unicode |= name == "rscriptunicode.dll";
            if (name != "rscriptfiles.dll" && name != "rscriptresources.dll" && name != "rscriptunicode.dll") {
                thirdParty = true;
                issue(CheckSeverity::Pending, path.u8string(), "Third-party DLL capabilities, ABI and initialization require runtime validation");
            }
        }
        if (!options.plugins.empty()) {
            inspect(options.launcher, false, game->machine);
            inspect(options.host, true, game->machine, "RScriptHost_Initialize");
        }
        if ((resources || unicode) && !files)
            issue(thirdParty ? CheckSeverity::Pending : CheckSeverity::Error, "modules",
                  "Resources and Unicode require an enabled file/engine adapter");
        if (unicode && options.kinsoku.empty()) options.kinsoku = options.launcher.parent_path() / "kinsoku.json";
        if (!options.kinsoku.empty()) {
            try {
                if (fs::file_size(options.kinsoku) > 64 * 1024) throw std::runtime_error("Kinsoku JSON must not exceed 64 KiB");
                const auto bytes = readFile(options.kinsoku);
                parseKinsoku(std::string(bytes.begin(), bytes.end()));
                issue(CheckSeverity::Info, options.kinsoku.u8string(), "Kinsoku configuration is valid");
            } catch (const std::exception& error) { issue(CheckSeverity::Error, options.kinsoku.u8string(), error.what()); }
        }
#if RSCRIPT_BUILD_GAME_MODULES
        std::set<std::string> imports;
        for (const auto& item : game->imports()) imports.insert(item.name);
        if (files) {
            for (const auto* name : {"CreateFileA", "ReadFile", "SetFilePointer", "GetFileSize", "CloseHandle"})
                if (!imports.count(name)) issue(CheckSeverity::Error, "RScriptFiles", std::string("Required synchronous file API is not imported: ") + name);
            issue(CheckSeverity::Pending, "RScriptFiles", "Actual import addresses and hook conflicts are checked during DLL initialization");
        }
        if (unicode) {
            if (game->machine != 0x14c) issue(CheckSeverity::Error, "RScriptUnicode", "The native line-break bridge requires a 32-bit x86 game");
            if (!imports.count("GetGlyphOutlineA") && !imports.count("TextOutA"))
                issue(CheckSeverity::Pending, "RScriptUnicode", "No supported ANSI GDI text import could be confirmed; DLL initialization must verify the text path");
            size_t classifiers = 0;
            for (const auto& section : game->sections) if (section.flags & 0x20000000) {
                tick(0, 0, "Checking line-break classifier: " + section.name);
                const auto bytes = game->sectionBytes(section);
                classifiers += findKinsokuBlocks(bytes.data(), bytes.size()).size();
            }
            issue(classifiers ? CheckSeverity::Info : CheckSeverity::Pending, "RScriptUnicode",
                  classifiers ? "Recognized line-break classifiers: " + std::to_string(classifiers) :
                  "Line-break classifier could not be confirmed statically; DLL initialization must verify it");
            issue(CheckSeverity::Pending, "RScriptUnicode", "Font coverage and actual text-hook availability require runtime validation");
        }
        if (!full) return result;
        if (!resources) {
            issue(thirdParty ? CheckSeverity::Pending : CheckSeverity::Info, "scripts", thirdParty ?
                  "Built-in resource module is disabled; third-party script support is unverified and scripts were not compiled" :
                  "Resource module is disabled; override scripts are inactive and were not compiled");
            return result;
        }
        GlyphTokens tokens;
        bool reservationComplete = true;
        if (unicode) {
#ifdef _WIN32
            try {
                const auto location = dictionaryLocation(options.game, options.root);
                tick(0, 0, location.file.u8string());
                if (fs::exists(location.file)) {
                    const ResourceSource source(location.file);
                    if (source.size() > 8 + location.identity.size() + 8 + GlyphTokens::capacity * 3)
                        throw std::runtime_error("Oversized Unicode dictionary");
                    restoreDictionary(source.readAll(), location.identity, tokens);
                }
            } catch (const std::exception& error) {
                reservationComplete = false;
                issue(CheckSeverity::Error, "glyph dictionary", error.what());
            }
#else
            issue(CheckSeverity::Pending, "glyph dictionary", "Windows dictionary location is unavailable on this host; capacity excludes existing Windows history");
#endif
            reserveOriginalText(options.game, options.root, tokens, {
                [&](const std::string& source) { tick(0, 0, "Scanning native reservations: " + source); },
                [&](const std::string& source, const std::string& error) {
                    reservationComplete = false;
                    issue(CheckSeverity::Error, source, error);
                }});
        }
        std::set<fs::path> logicals;
        try {
            for (const auto& item : fs::recursive_directory_iterator(options.root)) {
                tick(0, 0, "Finding scripts: " + item.path().u8string());
                if (!item.is_regular_file()) continue;
                const auto ext = lower(item.path().extension().u8string());
                if (ext != ".tsc" && ext != ".gsc") continue;
                auto relative = item.path().lexically_relative(options.root);
                relative.replace_extension(".gsc");
                logicals.insert(fs::u8path(lower(relative.generic_u8string())));
            }
        } catch (const std::exception& error) {
            result.complete = false;
            issue(CheckSeverity::Error, options.root.u8string(), error.what());
        }
        result.scripts = logicals.size();
        std::vector<std::string> strings;
        for (const auto& relative : logicals) {
            tick(result.checked, result.scripts, relative.u8string());
            auto source = relative.u8string();
            try {
                const auto selected = selectResource(options.root, relative, ResourceKind::Script);
                if (!selected) throw std::runtime_error("Script disappeared while checking");
                source = selected->u8string();
                std::optional<ResourceData> resource;
                try { resource = loadOverride(options.root, relative, ResourceKind::Script, unicode); }
                catch (const std::exception&) {
                    if (unicode || !thirdParty) throw;
                    resource = loadOverride(options.root, relative, ResourceKind::Script, true);
                }
                if (!resource) throw std::runtime_error("Script disappeared while checking");
                validateScriptOverride(resource->bytes, options.game.parent_path(), relative);
                const auto script = GscFile::fromBytes(resource->bytes, resource->utf8Script ? "UTF-8" : "CP932");
                if (resource->utf8Script) {
                    if (unicode) {
                        tokens.measure(script.strings);
                        strings.insert(strings.end(), script.strings.begin(), script.strings.end());
                    } else issue(CheckSeverity::Pending, source,
                        "Requires Unicode support from an unverified third-party DLL; CP932 alone cannot compile this script");
                } else {
                    const auto begin = size_t(script.headerLength) + size_t(script.commandLength) + size_t(script.stringDeclarationLength);
                    convertEncoding(std::string(reinterpret_cast<const char*>(resource->bytes.data() + begin),
                                    size_t(script.stringDefinitionLength)), "CP932", "UTF-8", true);
                    for (const auto& text : script.strings) convertEncoding(text, "UTF-8", "CP932", true);
                }
            } catch (const std::exception& error) {
                ++result.failed;
                issue(CheckSeverity::Error, source, error.what());
            }
            ++result.checked;
        }
        result.complete &= result.failed == 0;
        if (unicode) {
            try {
                const auto usage = tokens.measure(strings);
                result.reserved = usage.reserved; result.historical = usage.historical;
                result.required = usage.required; result.remaining = usage.remaining;
                result.capacityKnown = reservationComplete && result.failed == 0;
#ifndef _WIN32
                result.capacityKnown = false;
#endif
                if (result.failed) issue(CheckSeverity::Pending, "glyph capacity", "Capacity is incomplete because some scripts could not be checked");
                if (!usage.fits) issue(CheckSeverity::Error, "glyph capacity", "Patch exceeds the available Unicode glyph-token capacity (1880 slots)");
            } catch (const std::exception& error) {
                reservationComplete = false;
                issue(CheckSeverity::Error, "glyph capacity", error.what());
            }
            result.complete &= reservationComplete;
        }
#else
        if (full) issue(CheckSeverity::Pending, "scripts", "This standalone build provides basic checks only; full script checking is unavailable");
#endif
        tick(result.checked, result.scripts, "Finished");
    } catch (const Cancelled&) { result.cancelled = true; result.complete = false; }
      catch (const std::exception& error) { result.complete = false; issue(CheckSeverity::Error, "check", error.what()); }
    return result;
}
}
