#include "gui_common.h"

#include <algorithm>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace liarsoft::gui {

std::string nativePathFromUtf8(const std::string& path) {
#ifdef _WIN32
    if (path.empty()) return path;
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        path.c_str(), -1, nullptr, 0);
    if (!size) throw std::runtime_error("Invalid UTF-8 file path");
    std::vector<wchar_t> wide(size);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.c_str(), -1,
        wide.data(), size);
    const bool utf8 = GetACP() == CP_UTF8;
    BOOL substituted = FALSE;
    const auto bytes = WideCharToMultiByte(CP_ACP,
        utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS,
        wide.data(), -1, nullptr, 0, nullptr, utf8 ? nullptr : &substituted);
    if (!bytes || substituted)
        throw std::runtime_error("File path is not representable in the Windows system code page; move it to an ASCII path");
    std::string result(bytes, '\0');
    if (!WideCharToMultiByte(CP_ACP,
            utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS,
            wide.data(), -1, &result[0], bytes, nullptr,
            utf8 ? nullptr : &substituted) || substituted)
        throw std::runtime_error("Cannot convert file path to the Windows system code page");
    result.pop_back();
    return result;
#else
    return path;
#endif
}

std::string nativeTextToUtf8(const std::string& text) {
#ifdef _WIN32
    if (text.empty()) return text;
    const auto size = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
    if (!size) throw std::runtime_error("Cannot decode Windows file path or diagnostic");
    std::vector<wchar_t> wide(size);
    MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, wide.data(), size);
    const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        wide.data(), -1, nullptr, 0, nullptr, nullptr);
    if (!bytes) throw std::runtime_error("Cannot encode Windows file path or diagnostic as UTF-8");
    std::string result(bytes, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), -1,
            &result[0], bytes, nullptr, nullptr))
        throw std::runtime_error("Cannot encode Windows file path or diagnostic as UTF-8");
    result.pop_back();
    return result;
#else
    return text;
#endif
}

std::string formatDiagnostics(const std::vector<ConversionDiagnostic>& diagnostics) {
    if (diagnostics.empty()) return "";
    const auto errors = std::count_if(diagnostics.begin(), diagnostics.end(),
        [](const ConversionDiagnostic& diagnostic) { return diagnostic.error; });
    std::string report = "LiarsoftTool conversion diagnostics\nErrors: " +
        std::to_string(errors) + "\nWarnings: " +
        std::to_string(diagnostics.size() - errors) + "\n";
    for (const auto& diagnostic : diagnostics) {
        report += diagnostic.error ? "\n[Error]\n" : "\n[Warning]\n";
        report += "Input: " + diagnostic.inputPath + "\nOutput: " +
            diagnostic.outputPath + "\n" + diagnostic.message + "\n";
    }
    return report;
}

} // namespace liarsoft::gui
