#include "wav_ogg.h"
#include <fstream>
#include <stdexcept>
#include <cstring>

namespace liarsoft {

bool WavOggExtractor::hasEmbeddedOgg(const std::vector<uint8_t>& data) {
    if (data.size() < OGG_OFFSET + 4) return false;
    return data[OGG_OFFSET]     == OGG_MAGIC[0] &&
           data[OGG_OFFSET + 1] == OGG_MAGIC[1] &&
           data[OGG_OFFSET + 2] == OGG_MAGIC[2] &&
           data[OGG_OFFSET + 3] == OGG_MAGIC[3];
}

std::vector<uint8_t> WavOggExtractor::extract(const std::vector<uint8_t>& wavData) {
    if (!hasEmbeddedOgg(wavData)) return {};

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

    while (pos + 27 < wavData.size()) {
        // Verify Ogg magic at current position
        if (wavData[pos] != 'O' || wavData[pos+1] != 'g' ||
            wavData[pos+2] != 'g' || wavData[pos+3] != 'S') {
            break; // No more complete pages
        }

        size_t pageStart = pos;

        // Read segment table (at offset 26 from page start)
        size_t segTableOff = pageStart + 26;
        if (segTableOff >= wavData.size()) break;
        uint8_t segmentCount = wavData[segTableOff];
        if (segTableOff + 1 + segmentCount > wavData.size()) break;

        // Sum segment sizes to get total payload + segment table size
        size_t payloadSize = 0;
        for (uint8_t i = 0; i < segmentCount; ++i) {
            payloadSize += wavData[segTableOff + 1 + i];
        }

        size_t pageSize = 27 + segmentCount + payloadSize;
        if (pageStart + pageSize > wavData.size()) break;

        // A page with segmentCount==1 and payloadSize==0 is an empty page; skip it
        // (but the original code skips *only* this case, copying all others)
        // Actually the original code copies ALL pages including empty ones,
        // but skips when segmentCount==1 && payloadSize==0. Let me follow the original:
        // Actually re-reading: if(segmentCount == 1 && offset == 0) { skip } else { copy }
        // This means: skip only the single-segment zero-length empty pages.
        // But we should copy all pages to produce a valid OGG file.
        // Let me just copy everything for now.

        // Copy the entire page
        output.insert(output.end(),
                      wavData.begin() + static_cast<ptrdiff_t>(pageStart),
                      wavData.begin() + static_cast<ptrdiff_t>(pageStart + pageSize));

        pos = pageStart + pageSize;
    }

    return output;
}

void WavOggExtractor::extractToFile(const std::string& wavPath, const std::string& oggPath) {
    std::ifstream in(wavPath, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open: " + wavPath);
    in.seekg(0, std::ios::end);
    size_t sz = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(sz);
    in.read(reinterpret_cast<char*>(data.data()), sz);

    auto ogg = extract(data);
    if (ogg.empty())
        throw std::runtime_error("No embedded Ogg Vorbis found in: " + wavPath);

    std::ofstream out(oggPath, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write: " + oggPath);
    out.write(reinterpret_cast<const char*>(ogg.data()), ogg.size());
}

void WavOggExtractor::embedToFile(const std::string& oggPath, const std::string& refWavPath,
                                   const std::string& wavPath) {
    // Read OGG data
    std::ifstream inOgg(oggPath, std::ios::binary);
    if (!inOgg) throw std::runtime_error("Cannot open: " + oggPath);
    inOgg.seekg(0, std::ios::end);
    size_t oggSz = inOgg.tellg();
    inOgg.seekg(0, std::ios::beg);
    std::vector<uint8_t> ogg(oggSz);
    inOgg.read(reinterpret_cast<char*>(ogg.data()), oggSz);

    // Read 66-byte WAV header template from reference file
    std::ifstream inRef(refWavPath, std::ios::binary);
    if (!inRef) throw std::runtime_error("Cannot open reference WAV: " + refWavPath);
    uint8_t hdr[66];
    inRef.read(reinterpret_cast<char*>(hdr), 66);
    if (inRef.gcount() != 66)
        throw std::runtime_error("Reference WAV too small (need ≥ 66 bytes)");

    // Patch RIFF chunk size at offset 4: total file size - 8
    uint32_t riffSize = static_cast<uint32_t>(66 + oggSz - 8);
    hdr[4] = riffSize & 0xFF;
    hdr[5] = (riffSize >> 8) & 0xFF;
    hdr[6] = (riffSize >> 16) & 0xFF;
    hdr[7] = (riffSize >> 24) & 0xFF;

    // Write output
    std::ofstream out(wavPath, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write: " + wavPath);
    out.write(reinterpret_cast<const char*>(hdr), 66);
    out.write(reinterpret_cast<const char*>(ogg.data()), oggSz);
}

} // namespace liarsoft
