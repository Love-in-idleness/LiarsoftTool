#ifndef LIARSOFTTOOL_WAV_OGG_H
#define LIARSOFTTOOL_WAV_OGG_H

#include <cstdint>
#include <string>
#include <vector>

namespace liarsoft {

/**
 * Extract embedded Ogg Vorbis data from a WAV file.
 *
 * Locate embedded Ogg by RIFF chunks, or decode Ogg to 16-bit PCM WAV.
 */
class WavOggExtractor {
public:
    /// Check if a WAV file contains embedded Ogg data.
    static bool hasEmbeddedOgg(const std::vector<uint8_t>& data);

    /// Check if the input is already a standard PCM WAV that needs no extraction.
    static bool isStandardPcmWav(const std::vector<uint8_t>& data);

    /// Extract Ogg Vorbis stream from WAV data.
    /// Returns the extracted OGG data, or empty if no OGG found.
    static std::vector<uint8_t> extract(const std::vector<uint8_t>& wavData);

    /// Wrap a complete Ogg stream as Vorbis mode 1 (0x674f), including its
    /// own headers/codebooks. All WAV fields are built without a template.
    static std::vector<uint8_t> embed(const std::vector<uint8_t>& oggData);

    /// Fully decode mono/stereo Vorbis to signed 16-bit little-endian PCM.
    /// Keeps the source sample rate and channels; rejects corrupt/partial audio.
    static std::vector<uint8_t> decodeToPcm(const std::vector<uint8_t>& oggData);

    /// Extract from file and save to .ogg file. Returns false when the input is
    /// already a standard PCM WAV and is deliberately retained unchanged.
    static bool extractToFile(const std::string& wavPath, const std::string& oggPath);

    /// Default: PCM WAV. With vorbisInWav: preserve the compressed Ogg stream.
    /// Neither mode reads or requires an existing WAV file.
    static void convertToFile(const std::string& oggPath, const std::string& wavPath,
                              bool vorbisInWav = false);
};

} // namespace liarsoft

#endif // LIARSOFTTOOL_WAV_OGG_H
