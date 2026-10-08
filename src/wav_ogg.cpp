#include "wav_ogg.h"
#include "fileio.h"
#include <fstream>
#include <limits>
#include <stdexcept>
#include <cstring>
#include <array>
#include <algorithm>
#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>

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

void writeU16(std::vector<uint8_t>& data, size_t offset, uint16_t value) {
    data[offset] = static_cast<uint8_t>(value);
    data[offset + 1] = static_cast<uint8_t>(value >> 8);
}

bool isVorbisTag(uint16_t tag) {
    // Modes 1/3 keep an Ogg stream in data. Mode 2 has separate codec headers
    // in fmt and is deliberately not treated as a self-contained Ogg stream.
    return tag == 0x674f || tag == 0x676f || tag == 0x6751 || tag == 0x6771;
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

struct VorbisStream {
    uint16_t channels;
    uint32_t rate;
    uint32_t samples;
    std::vector<uint8_t> bytes;
};

VorbisStream inspectVorbis(const std::vector<uint8_t>& data) {
    const auto first = readPage(data, 0, data.size());
    const size_t packet = first.headerSize;
    if (data[5] != 2 || readU32(data, 18) != 0 || first.size != packet + 30 ||
        std::memcmp(data.data() + packet, "\x01vorbis", 7) != 0 ||
        readU32(data, packet + 7) != 0 || data[packet + 29] != 1)
        throw std::runtime_error("Input must be a single Ogg Vorbis stream");
    VorbisStream stream{data[packet + 11], readU32(data, packet + 12), 0, {}};
    if (stream.channels == 0 || stream.rate == 0)
        throw std::runtime_error("Invalid Vorbis channels/sample rate");

    size_t pos = 0;
    uint32_t sequence = 0;
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
                stream.samples = readU32(data, pos + 6);
                eos = true;
            }
            stream.bytes.insert(stream.bytes.end(), data.begin() + pos,
                                data.begin() + pos + page.size);
        }
        pos += page.size;
    }
    if (!eos) throw std::runtime_error("Ogg Vorbis stream has no end-of-stream page");
    return stream;
}

struct WavChunks {
    size_t format = 0;
    size_t audio = 0;
    size_t audioSize = 0;
};

WavChunks parseWav(const std::vector<uint8_t>& data) {
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 ||
        std::memcmp(data.data() + 8, "WAVE", 4) != 0)
        return {};
    const uint64_t declaredEnd = uint64_t(readU32(data, 4)) + 8;
    if (declaredEnd < 12) throw std::runtime_error("Invalid RIFF size");
    // Some legacy files declare decoded PCM lengths instead of stored bytes.
    size_t end = static_cast<size_t>(std::min<uint64_t>(declaredEnd, data.size()));
    // Sound Forge-authored game assets can omit one alignment byte from the
    // RIFF total even though it precedes a valid LIST chunk in the file.
    if ((declaredEnd & 1u) && declaredEnd + 1 == data.size()) ++end;
    WavChunks chunks;
    for (size_t pos = 12; pos < end;) {
        if (end - pos < 8) throw std::runtime_error("Truncated RIFF chunk header");
        const size_t start = pos + 8;
        size_t size = readU32(data, pos + 4);
        const bool audio = std::memcmp(data.data() + pos, "data", 4) == 0;
        if (size > end - start) {
            if (!audio || !chunks.format ||
                !isVorbisTag(readU16(data, chunks.format)) ||
                end - start < 4 || std::memcmp(data.data() + start, "OggS", 4) != 0)
                throw std::runtime_error("Truncated RIFF chunk");
            // Recover only complete Ogg pages through the actual stream EOS.
            // Do not absorb subsequent LIST/cue chunks as audio.
            size_t cursor = start;
            const auto first = readPage(data, cursor, end);
            bool eos = false;
            while (cursor < end) {
                const auto page = readPage(data, cursor, end);
                eos = page.serial == first.serial && (data[cursor + 5] & 4u);
                cursor += page.size;
                if (eos) break;
            }
            if (!eos) throw std::runtime_error("Legacy WAV audio has no EOS");
            size = cursor - start;
        }
        if (std::memcmp(data.data() + pos, "fmt ", 4) == 0) {
            if (chunks.format || size < 16)
                throw std::runtime_error("Invalid or duplicate WAV fmt chunk");
            chunks.format = start;
            if (size >= 18 && readU16(data, start + 16) > size - 18)
                throw std::runtime_error("Truncated WAV format extension");
        } else if (audio) {
            if (chunks.audio) throw std::runtime_error("Duplicate WAV data chunk");
            chunks.audio = start;
            chunks.audioSize = size;
        }
        pos = start + size;
        if (size & 1u) {
            // Old Liar-soft WAVs sometimes omit the final alignment byte.
            if (pos < end) ++pos;
        }
    }
    return chunks;
}

struct MemoryInput {
    const std::vector<uint8_t>& bytes;
    size_t pos = 0;
};

size_t readVorbis(void* ptr, size_t size, size_t count, void* source) {
    auto& input = *static_cast<MemoryInput*>(source);
    if (size == 0) return 0;
    count = std::min(count, (input.bytes.size() - input.pos) / size);
    std::memcpy(ptr, input.bytes.data() + input.pos, count * size);
    input.pos += count * size;
    return count;
}

class VorbisDecoder {
public:
    OggVorbis_File file{};
    explicit VorbisDecoder(MemoryInput& input) {
        if (ov_open_callbacks(&input, &file, nullptr, 0,
                              {readVorbis, nullptr, nullptr, nullptr}) != 0)
            throw std::runtime_error("Invalid Vorbis headers/codebooks");
    }
    ~VorbisDecoder() { ov_clear(&file); }
    VorbisDecoder(const VorbisDecoder&) = delete;
    VorbisDecoder& operator=(const VorbisDecoder&) = delete;
};

std::vector<uint8_t> wavHeader(uint16_t tag, uint16_t channels, uint32_t rate) {
    const bool pcm = tag == 1;
    std::vector<uint8_t> out(pcm ? 44 : 66, 0);
    std::memcpy(out.data(), "RIFF", 4);
    std::memcpy(out.data() + 8, "WAVEfmt ", 8);
    writeU32(out, 16, pcm ? 16 : 26);
    writeU16(out, 20, tag);
    writeU16(out, 22, channels);
    writeU32(out, 24, rate);
    writeU16(out, 32, pcm ? channels * 2 : 1);
    writeU16(out, 34, 16);
    if (!pcm) {
        writeU16(out, 36, 8);
        // OGGWAVEFORMAT compatibility identifiers from the original ACM source.
        // Mode 1 carries its own codebooks; these do not select built-in ones.
        writeU32(out, 38, 0x20020201);
        writeU32(out, 42, 0x20011231);
        std::memcpy(out.data() + 46, "fact", 4);
        writeU32(out, 50, 4);
    }
    std::memcpy(out.data() + out.size() - 8, "data", 4);
    return out;
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
    try {
        const auto chunks = parseWav(data);
        return chunks.format && chunks.audio && chunks.audioSize >= 4 &&
               isVorbisTag(readU16(data, chunks.format)) &&
               std::memcmp(data.data() + chunks.audio, "OggS", 4) == 0;
    } catch (const std::runtime_error&) { return false; }
}

bool WavOggExtractor::isStandardPcmWav(const std::vector<uint8_t>& data) {
    try {
        const auto chunks = parseWav(data);
        return chunks.format && chunks.audio && readU16(data, chunks.format) == 1;
    } catch (const std::runtime_error&) { return false; }
}

std::vector<uint8_t> WavOggExtractor::extract(const std::vector<uint8_t>& wavData) {
    const auto chunks = parseWav(wavData);
    if (!chunks.format || !chunks.audio ||
        !isVorbisTag(readU16(wavData, chunks.format))) return {};
    std::vector<uint8_t> ogg(wavData.begin() + chunks.audio,
                             wavData.begin() + chunks.audio + chunks.audioSize);
    // Compatibility: older files can include the alignment byte in data size.
    if (ogg.size() > 1 && ogg.back() == 0) {
        size_t pos = 0;
        while (pos + 1 < ogg.size()) pos += readPage(ogg, pos, ogg.size()).size;
        if (pos + 1 == ogg.size()) ogg.pop_back();
    }
    const auto stream = inspectVorbis(ogg);
    if (stream.channels != readU16(wavData, chunks.format + 2) ||
        stream.rate != readU32(wavData, chunks.format + 4))
        throw std::runtime_error("WAV format does not match embedded Vorbis audio");
    return stream.bytes;
}

std::vector<uint8_t> WavOggExtractor::embed(
    const std::vector<uint8_t>& oggData) {
    const auto stream = inspectVorbis(oggData);
    MemoryInput input{stream.bytes};
    VorbisDecoder decoder(input); // Validate real Vorbis headers, not just Ogg pages.
    if (stream.bytes.size() > std::numeric_limits<uint32_t>::max() - 66)
        throw std::runtime_error("Ogg data is too large for a WAV container");
    auto output = wavHeader(0x674f, stream.channels, stream.rate);
    const uint64_t average = (uint64_t(stream.bytes.size()) * stream.rate +
        std::max(1u, stream.samples) - 1) / std::max(1u, stream.samples);
    if (average > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Vorbis average byte rate exceeds WAV limit");
    writeU32(output, 28, static_cast<uint32_t>(std::max<uint64_t>(1, average)));
    writeU32(output, 54, stream.samples);
    writeU32(output, 62, static_cast<uint32_t>(stream.bytes.size()));
    output.insert(output.end(), stream.bytes.begin(), stream.bytes.end());
    if (stream.bytes.size() & 1u) output.push_back(0);
    writeU32(output, 4, static_cast<uint32_t>(output.size() - 8));
    return output;
}

std::vector<uint8_t> WavOggExtractor::decodeToPcm(const std::vector<uint8_t>& oggData) {
    const auto stream = inspectVorbis(oggData);
    // Game engines and plain WAVE PCM agree on mono/stereo channel order.
    // Multichannel needs an explicit speaker mapping, not silent channel mixing.
    if (stream.channels > 2)
        throw std::runtime_error("PCM WAV conversion supports mono/stereo Vorbis only");
    const uint64_t bytes = uint64_t(stream.samples) * stream.channels * 2;
    const uint64_t byteRate = uint64_t(stream.rate) * stream.channels * 2;
    if (bytes > std::numeric_limits<uint32_t>::max() - 36 ||
        byteRate > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Decoded PCM is too large for a WAV container");
    MemoryInput input{stream.bytes};
    VorbisDecoder decoder(input);
    auto output = wavHeader(1, stream.channels, stream.rate);
    writeU32(output, 28, static_cast<uint32_t>(byteRate));
    std::array<char, 16384> buffer;
    int section = 0;
    for (;;) {
        const long count = ov_read(&decoder.file, buffer.data(), buffer.size(), 0, 2, 1, &section);
        if (count < 0) throw std::runtime_error("Corrupt Vorbis audio during PCM decoding");
        if (count == 0) break;
        if (section != 0 || output.size() - 44 + count > bytes)
            throw std::runtime_error("Vorbis decoded length exceeds its EOS sample count");
        output.insert(output.end(), buffer.data(), buffer.data() + count);
    }
    if (output.size() - 44 != bytes)
        throw std::runtime_error("Vorbis decoded length does not match its EOS sample count");
    writeU32(output, 4, static_cast<uint32_t>(output.size() - 8));
    writeU32(output, 40, static_cast<uint32_t>(bytes));
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

void WavOggExtractor::convertToFile(const std::string& oggPath, const std::string& wavPath,
                                   bool vorbisInWav) {
    const auto ogg = readFile(oggPath);
    writeFileIfChanged(wavPath, vorbisInWav ? embed(ogg) : decodeToPcm(ogg));
}

} // namespace liarsoft
