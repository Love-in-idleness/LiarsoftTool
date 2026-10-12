#pragma once

#include <string>
#include <vector>

namespace liarsoft::gui {

struct ConversionDiagnostic {
    bool error = false;
    std::string inputPath;
    std::string outputPath;
    std::string message;
};

std::string formatDiagnostics(const std::vector<ConversionDiagnostic>& diagnostics);

// GTK uses UTF-8; the Windows conversion core still uses system-code-page paths.
std::string nativePathFromUtf8(const std::string& path);
std::string nativeTextToUtf8(const std::string& text);

} // namespace liarsoft::gui
