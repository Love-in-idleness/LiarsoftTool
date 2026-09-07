#ifndef LIARSOFTTOOL_WAV_OGG_H
#define LIARSOFTTOOL_WAV_OGG_H

#include <cstdint>
#include <string>
#include <vector>

namespace liarsoft {

/**
 * Extract embedded Ogg Vorbis data from a WAV file.
 *
 * Liar-soft WAV files may contain Ogg Vorbis audio embedded at offset 66.
 * The OGG magic bytes are "OggS" (0x4F 0x67 0x67 0x53).
 */
class WavOggExtractor {
public:
    static constexpr size_t OGG_OFFSET = 66;
    static constexpr uint8_t OGG_MAGIC[4] = {0x4F, 0x67, 0x67, 0x53}; // "OggS"

    /// Check if a WAV file contains embedded Ogg data.
    static bool hasEmbeddedOgg(const std::vector<uint8_t>& data);

    /// Check if the input is already a standard PCM WAV that needs no extraction.
    static bool isStandardPcmWav(const std::vector<uint8_t>& data);

    /// Extract Ogg Vorbis stream from WAV data.
    /// Returns the extracted OGG data, or empty if no OGG found.
    static std::vector<uint8_t> extract(const std::vector<uint8_t>& wavData);

    /// Wrap Ogg data using the 66-byte header from a reference WAV.
    static std::vector<uint8_t> embed(const std::vector<uint8_t>& oggData,
                                      const std::vector<uint8_t>& refWavData);

    /// Extract from file and save to .ogg file. Returns false when the input is
    /// already a standard PCM WAV and is deliberately retained unchanged.
    static bool extractToFile(const std::string& wavPath, const std::string& oggPath);

    /// Embed Ogg Vorbis data into a WAV container.
    /// Uses the first 66 bytes of `refWavPath` as the WAV header template.
    static void embedToFile(const std::string& oggPath, const std::string& refWavPath,
                            const std::string& wavPath);
};

} // namespace liarsoft

#endif // LIARSOFTTOOL_WAV_OGG_H
