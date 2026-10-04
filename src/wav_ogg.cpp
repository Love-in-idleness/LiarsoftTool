#include "wav_ogg.h"
#include "fileio.h"
#include <fstream>
#include <limits>
#include <stdexcept>
#include <cstring>
#include <array>

namespace liarsoft {

namespace {

uint32_t readU32(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

uint16_t readU16(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<uint16_t>(data[offset]) |
           (static_cast<uint16_t>(data[offset + 1]) << 8);
}

void writeU32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    data[offset] = static_cast<uint8_t>(value);
    data[offset + 1] = static_cast<uint8_t>(value >> 8);
    data[offset + 2] = static_cast<uint8_t>(value >> 16);
    data[offset + 3] = static_cast<uint8_t>(value >> 24);
}

bool isVorbisWavHeader(const std::vector<uint8_t>& data) {
    return data.size() >= WavOggExtractor::OGG_OFFSET &&
           std::memcmp(data.data(), "RIFF", 4) == 0 &&
           std::memcmp(data.data() + 8, "WAVEfmt ", 8) == 0 &&
           readU32(data, 16) == 26 &&
           (readU16(data, 20) == 0x6771 || readU16(data, 20) == 0x6751) &&
           readU16(data, 36) == 8 &&
           std::memcmp(data.data() + 46, "fact", 4) == 0 &&
           readU32(data, 50) == 4 &&
           std::memcmp(data.data() + 58, "data", 4) == 0;
}

struct OggPage {
    size_t size;
    size_t headerSize;
    uint32_t serial;
};

OggPage readPage(const std::vector<uint8_t>& data, size_t pos, size_t end) {
    if (end - pos < 27) throw std::runtime_error("Truncated Ogg page header");
    if (std::memcmp(data.data() + pos, "OggS", 4) != 0 || data[pos + 4] != 0)
        throw std::runtime_error("Invalid Ogg page header");
    const size_t headerSize = 27 + data[pos + 26];
    if (headerSize > end - pos)
        throw std::runtime_error("Truncated Ogg segment table");
    size_t payloadSize = 0;
    for (size_t i = 27; i < headerSize; ++i) payloadSize += data[pos + i];
    if (payloadSize > end - pos - headerSize)
        throw std::runtime_error("Truncated Ogg page payload");
    return {headerSize + payloadSize, headerSize, readU32(data, pos + 14)};
}

bool isPaddingPage(const OggPage& page, uint32_t audioSerial) {
    // 0xffffffff is a legal audio serial too: only remove the separate empty
    // wrapper stream, never an empty page belonging to the actual Vorbis stream.
    return page.serial == 0xffffffffu && page.serial != audioSerial &&
           page.headerSize == 28 && page.size == 28;
}

uint32_t pageChecksum(const std::vector<uint8_t>& data, size_t pos, size_t size) {
    static const auto table = [] {
        std::array<uint32_t, 256> values{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t value = i << 24;
            for (int bit = 0; bit < 8; ++bit)
                value = (value << 1) ^ ((value & 0x80000000u) ? 0x04c11db7u : 0);
            values[i] = value;
        }
        return values;
    }();
    uint32_t crc = 0;
    for (size_t i = 0; i < size; ++i) {
        const uint8_t byte = i >= 22 && i < 26 ? 0 : data[pos + i];
        crc = (crc << 8) ^ table[(crc >> 24) ^ byte];
    }
    return crc;
}

uint32_t vorbisSamples(const std::vector<uint8_t>& data,
                       const std::vector<uint8_t>& reference) {
    const auto first = readPage(data, 0, data.size());
    const size_t packet = first.headerSize;
    if (data[5] != 2 || readU32(data, 18) != 0 || first.size != packet + 30 ||
        std::memcmp(data.data() + packet, "\x01vorbis", 7) != 0 ||
        readU32(data, packet + 7) != 0 || data[packet + 29] != 1)
        throw std::runtime_error("Input must be a single Ogg Vorbis stream");
    if (data[packet + 11] == 0 || readU32(data, packet + 12) == 0 ||
        data[packet + 11] != readU16(reference, 22) ||
        readU32(data, packet + 12) != readU32(reference, 24))
        throw std::runtime_error("Ogg channels/sample rate do not match the WAV template; "
                                 "use a matching Vorbis WAV template or resample the Ogg");

    size_t pos = 0;
    uint32_t sequence = 0;
    uint32_t samples = 0;
    bool eos = false;
    while (pos < data.size()) {
        const auto page = readPage(data, pos, data.size());
        if (pageChecksum(data, pos, page.size) != readU32(data, pos + 22))
            throw std::runtime_error("Invalid Ogg page checksum");
        if (!isPaddingPage(page, first.serial)) {
            const uint8_t flags = data[pos + 5];
            if (eos || page.serial != first.serial ||
                readU32(data, pos + 18) != sequence++ || (flags & ~7u) ||
                (pos != 0 && (flags & 2u)))
                throw std::runtime_error("Chained, multiplexed or discontinuous Ogg is unsupported");
            if (flags & 4u) {
                if (readU32(data, pos + 10) != 0)
                    throw std::runtime_error("Ogg sample count exceeds the WAV fact field");
                samples = readU32(data, pos + 6);
                eos = true;
            }
        }
        pos += page.size;
    }
    if (!eos) throw std::runtime_error("Ogg Vorbis stream has no end-of-stream page");
    return samples;
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
    if (!isVorbisWavHeader(data) || data.size() < OGG_OFFSET + 4) return false;
    return data[OGG_OFFSET]     == OGG_MAGIC[0] &&
           data[OGG_OFFSET + 1] == OGG_MAGIC[1] &&
           data[OGG_OFFSET + 2] == OGG_MAGIC[2] &&
           data[OGG_OFFSET + 3] == OGG_MAGIC[3];
}

bool WavOggExtractor::isStandardPcmWav(const std::vector<uint8_t>& data) {
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 ||
        std::memcmp(data.data() + 8, "WAVE", 4) != 0)
        return false;

    bool pcm = false;
    bool samples = false;
    size_t pos = 12;
    while (pos + 8 <= data.size()) {
        const uint32_t chunkSize = readU32(data, pos + 4);
        const size_t chunkData = pos + 8;
        if (chunkSize > data.size() - chunkData) return false;
        if (std::memcmp(data.data() + pos, "fmt ", 4) == 0 && chunkSize >= 16)
            pcm = readU16(data, chunkData) == 1;
        else if (std::memcmp(data.data() + pos, "data", 4) == 0)
            samples = true;
        pos = chunkData + chunkSize + (chunkSize & 1u);
    }
    return pcm && samples;
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

    std::vector<uint8_t> output;
    size_t pos = OGG_OFFSET;
    const auto first = readPage(wavData, pos, dataEnd);

    while (pos < dataEnd) {
        if (dataEnd - pos == 1 && wavData[pos] == 0) break;
        const auto page = readPage(wavData, pos, dataEnd);
        if (!isPaddingPage(page, first.serial)) {
            output.insert(output.end(),
                          wavData.begin() + static_cast<ptrdiff_t>(pos),
                          wavData.begin() + static_cast<ptrdiff_t>(pos + page.size));
        }
        pos += page.size;
    }

    return output;
}

std::vector<uint8_t> WavOggExtractor::embed(
    const std::vector<uint8_t>& oggData,
    const std::vector<uint8_t>& refWavData) {
    if (!isVorbisWavHeader(refWavData))
        throw std::runtime_error("Reference must be an RScript Ogg-in-WAV template (format 0x6771 or 0x6751), "
                                 "not a PCM WAV; PCM replacement requires PCM audio");
    if (oggData.size() > std::numeric_limits<uint32_t>::max() - OGG_OFFSET)
        throw std::runtime_error("Ogg data is too large for a WAV container");

    const uint32_t samples = vorbisSamples(oggData, refWavData);
    std::vector<uint8_t> output(refWavData.begin(),
                                refWavData.begin() + OGG_OFFSET);
    const uint32_t oggSize = static_cast<uint32_t>(oggData.size());
    writeU32(output, 4, static_cast<uint32_t>(OGG_OFFSET - 8) + oggSize + (oggSize & 1u));
    writeU32(output, 54, samples);
    writeU32(output, OGG_OFFSET - 4, oggSize);
    output.insert(output.end(), oggData.begin(), oggData.end());
    if (oggData.size() % 2 != 0) output.push_back(0);
    return output;
}

bool WavOggExtractor::extractToFile(const std::string& wavPath, const std::string& oggPath) {
    const auto wav = readFile(wavPath);
    auto ogg = extract(wav);
    if (ogg.empty() && isStandardPcmWav(wav)) return false;
    if (ogg.empty())
        throw std::runtime_error("No embedded Ogg Vorbis found in: " + wavPath);

    writeFileIfChanged(oggPath, ogg);
    return true;
}

void WavOggExtractor::embedToFile(const std::string& oggPath, const std::string& refWavPath,
                                   const std::string& wavPath) {
    writeFileIfChanged(wavPath, embed(readFile(oggPath), readFile(refWavPath)));
}

} // namespace liarsoft
