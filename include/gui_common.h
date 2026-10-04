#pragma once

#include <string>
#include <vector>

namespace liarsoft::gui {

struct ConversionOptions {
    std::string encoding;
    std::string referencePath;
    bool recursive = false;
    bool gscToTsc = false;
    bool unpackOnly = false;
};

struct ConversionDiagnostic {
    bool error = false;
    std::string inputPath;
    std::string outputPath;
    std::string message;
};

std::string formatDiagnostics(const std::vector<ConversionDiagnostic>& diagnostics);

std::string extension(const std::string& path);
std::string replaceExtension(const std::string& path,
                             const std::string& newExtension);
std::string guessOutput(const std::string& inputPath,
                        const std::string& outputDirectory,
                        bool gscToTsc,
                        const std::string& encoding);
std::string guessType(const std::string& path,
                      bool gscToTsc,
                      const std::string& encoding);
bool isSupported(const std::string& path);

std::vector<std::string> convert(const std::string& inputPath,
                                 const std::string& outputPath,
                                 const ConversionOptions& options);

} // namespace liarsoft::gui
