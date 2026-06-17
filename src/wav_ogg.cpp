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

} // namespace liarsoft
