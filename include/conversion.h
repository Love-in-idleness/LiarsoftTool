#pragma once

#include <string>
#include <vector>

namespace liarsoft::conversion {

struct ConversionOptions {
    std::string encoding = "CP932";
    std::string referencePath;
    bool recursive = false;
    bool gscToTsc = false;
    bool unpackOnly = false;
    bool vorbisInWav = false;
    unsigned workers = 0;
};

struct ConversionResult {
    std::vector<std::string> warnings;
    bool retainedPcm = false;
};

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

ConversionResult convert(const std::string& inputPath,
                                 const std::string& outputPath,
                                 const ConversionOptions& options);

} // namespace liarsoft::conversion
