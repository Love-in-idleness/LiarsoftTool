#include "wav_ogg.h"
#include "fileio.h"
#include <fstream>
#include <limits>
#include <stdexcept>
#include <cstring>

namespace liarsoft {

namespace {

uint32_t readU32(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

void writeU32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    data[offset] = static_cast<uint8_t>(value);
    data[offset + 1] = static_cast<uint8_t>(value >> 8);
    data[offset + 2] = static_cast<uint8_t>(value >> 16);
    data[offset + 3] = static_cast<uint8_t>(value >> 24);
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open: " + path);
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    if (size < 0) throw std::runtime_error("Cannot read size: " + path);
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), size);
    if (in.gcount() != size) throw std::runtime_error("Cannot read: " + path);
    return data;
}

} // namespace

bool WavOggExtractor::hasEmbeddedOgg(const std::vector<uint8_t>& data) {
    if (data.size() < OGG_OFFSET + 4) return false;
    return data[OGG_OFFSET]     == OGG_MAGIC[0] &&
           data[OGG_OFFSET + 1] == OGG_MAGIC[1] &&
           data[OGG_OFFSET + 2] == OGG_MAGIC[2] &&
           data[OGG_OFFSET + 3] == OGG_MAGIC[3];
}

std::vector<uint8_t> WavOggExtractor::extract(const std::vector<uint8_t>& wavData) {
    if (!hasEmbeddedOgg(wavData)) return {};

    const uint32_t dataSize = readU32(wavData, OGG_OFFSET - 4);
    const size_t available = wavData.size() - OGG_OFFSET;
    // Some original RScript WAV files store the decoded PCM size in the data
    // chunk instead of the embedded Ogg byte size. In that case the Ogg stream
    // simply runs to EOF. Files produced here store the exact Ogg size so an
    // optional RIFF alignment byte is not parsed as another page.
    const size_t dataEnd = OGG_OFFSET +
        (dataSize <= available ? static_cast<size_t>(dataSize) : available);

    // Copy pages starting from OGG_OFFSET.
    // Each Ogg page has a standard header:
    //   capture_pattern: "OggS" (4)
    //   stream_structure_version: 1 (1)
    //   header_type_flag: 1 (1)
    //   granule_position: 8 (8)
    //   bitstream_serial_number: 4 (4)
    //   page_sequence_number: 4 (4)
    //   CRC checksum: 4 (4)
    //   page_segments: 1 (1) - number of segment entries
    //   segment_table: N (variable)
    //   payload: sum(segment_table) (variable)
    // Total header before segment_table: 27 bytes

    std::vector<uint8_t> output;
    size_t pos = OGG_OFFSET;

    while (pos < dataEnd) {
        if (dataEnd - pos == 1 && wavData[pos] == 0) break;
        if (dataEnd - pos < 27)
            throw std::runtime_error("Truncated Ogg page header");
        // Verify Ogg magic at current position
        if (wavData[pos] != 'O' || wavData[pos+1] != 'g' ||
            wavData[pos+2] != 'g' || wavData[pos+3] != 'S') {
            throw std::runtime_error("Invalid Ogg page capture pattern");
        }

        size_t pageStart = pos;

        // Read segment table (at offset 26 from page start)
        size_t segTableOff = pageStart + 26;
        uint8_t segmentCount = wavData[segTableOff];
        if (segTableOff + 1 + segmentCount > dataEnd)
            throw std::runtime_error("Truncated Ogg segment table");

        // Sum segment sizes to get total payload + segment table size
        size_t payloadSize = 0;
        for (uint8_t i = 0; i < segmentCount; ++i) {
            payloadSize += wavData[segTableOff + 1 + i];
        }

        size_t pageSize = 27 + segmentCount + payloadSize;
        if (pageStart + pageSize > dataEnd)
            throw std::runtime_error("Truncated Ogg page payload");

        // Liar-soft pads the real Vorbis stream with zero-length pages using
        // serial 0xffffffff. They are wrapper data, not playable audio.
        const uint32_t serial = readU32(wavData, pageStart + 14);
        if (serial != 0xffffffffu || segmentCount != 1 || payloadSize != 0) {
            output.insert(output.end(),
                          wavData.begin() + static_cast<ptrdiff_t>(pageStart),
                          wavData.begin() + static_cast<ptrdiff_t>(pageStart + pageSize));
        }

        pos = pageStart + pageSize;
    }

    return output;
}

std::vector<uint8_t> WavOggExtractor::embed(
    const std::vector<uint8_t>& oggData,
    const std::vector<uint8_t>& refWavData) {
    if (refWavData.size() < OGG_OFFSET)
        throw std::runtime_error("Reference WAV too small (need >= 66 bytes)");
    if (oggData.size() > std::numeric_limits<uint32_t>::max() - OGG_OFFSET)
        throw std::runtime_error("Ogg data is too large for a WAV container");

    std::vector<uint8_t> output(refWavData.begin(),
                                refWavData.begin() + OGG_OFFSET);
    const uint32_t oggSize = static_cast<uint32_t>(oggData.size());
    writeU32(output, 4, static_cast<uint32_t>(OGG_OFFSET - 8) + oggSize);
    writeU32(output, OGG_OFFSET - 4, oggSize);
    output.insert(output.end(), oggData.begin(), oggData.end());
    if (oggData.size() % 2 != 0) output.push_back(0);
    return output;
}

void WavOggExtractor::extractToFile(const std::string& wavPath, const std::string& oggPath) {
    auto ogg = extract(readFile(wavPath));
    if (ogg.empty())
        throw std::runtime_error("No embedded Ogg Vorbis found in: " + wavPath);

    writeFileIfChanged(oggPath, ogg);
}

void WavOggExtractor::embedToFile(const std::string& oggPath, const std::string& refWavPath,
                                   const std::string& wavPath) {
    writeFileIfChanged(wavPath, embed(readFile(oggPath), readFile(refWavPath)));
}

} // namespace liarsoft
