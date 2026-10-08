#include "wav_ogg.h"
#include "vorbis_fixture.h"

#include <algorithm>
#include <fstream>
#include <iostream>

namespace {
void writeU32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) data[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}
uint32_t readU32(const std::vector<uint8_t>& data, size_t offset) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
    return value;
}
void require(bool result, const char* message) {
    if (!result) throw std::runtime_error(message);
}
template<typename Fn> void rejects(Fn fn, const char* message) {
    try { fn(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error(message);
}
void fixRiffSize(std::vector<uint8_t>& wav) { writeU32(wav, 4, wav.size() - 8); }
void checksum(std::vector<uint8_t>& page) {
    writeU32(page, 22, 0);
    uint32_t crc = 0;
    for (uint8_t b : page) {
        crc ^= uint32_t(b) << 24;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc << 1) ^ ((crc & 0x80000000u) ? 0x04c11db7u : 0);
    }
    writeU32(page, 22, crc);
}
} // namespace

int main(int argc, char** argv) {
    try {
        using Audio = liarsoft::WavOggExtractor;
        const auto clean = testVorbis();
        const auto wav = Audio::embed(clean);
        require(Audio::hasEmbeddedOgg(wav) && Audio::extract(wav) == clean,
                "Template-free Vorbis WAV round trip failed");
        require(wav[20] == 0x4f && wav[21] == 0x67 && readU32(wav, 54) == 1257 &&
                readU32(wav, 62) == clean.size() && readU32(wav, 4) == wav.size() - 8 &&
                wav.size() % 2 == 0, "Mode 1 WAV format, fact, length or padding is incorrect");

        const auto pcm = Audio::decodeToPcm(clean);
        require(Audio::isStandardPcmWav(pcm) && !Audio::hasEmbeddedOgg(pcm) &&
                Audio::extract(pcm).empty(), "PCM WAV misidentified as compressed audio");
        require(pcm.size() == 44 + 1257 * 2 && readU32(pcm, 24) == 44100 &&
                pcm[22] == 1 && pcm[32] == 2 && pcm[34] == 16 &&
                readU32(pcm, 28) == 88200 && readU32(pcm, 40) == 2514 &&
                readU32(pcm, 4) == pcm.size() - 8, "Decoded PCM size/format is incorrect");
        require(std::any_of(pcm.begin() + 44, pcm.end(), [](uint8_t b) { return b != 0; }),
                "Decoded PCM is silent instead of the test tone");
        const auto stereo = testVorbis(2, 48000, 4800);
        const auto stereoPcm = Audio::decodeToPcm(stereo);
        const auto stereoWav = Audio::embed(stereo);
        require(stereoPcm.size() == 44 + 4800 * 4 && stereoPcm[22] == 2 &&
                readU32(stereoPcm, 24) == 48000 && readU32(stereoPcm, 28) == 192000 &&
                readU32(stereoWav, 54) == 4800 && Audio::extract(stereoWav) == stereo,
                "Stereo/48 kHz audio parameters or decoded length changed");
        rejects([&] { Audio::decodeToPcm(testVorbis(3)); },
                "Multichannel audio was silently mixed or incorrectly mapped");

        for (uint8_t tag : {0x4f, 0x6f, 0x51, 0x71}) {
            auto legacy = wav; legacy[20] = tag;
            require(Audio::hasEmbeddedOgg(legacy) && Audio::extract(legacy) == clean,
                    "Vorbis mode 1/1+/3/3+ recognition failed");
        }
        auto mode3 = wav; mode3[20] = 0x51;
        auto unknown = wav; unknown[20] = 0x50;
        require(!Audio::hasEmbeddedOgg(unknown) && Audio::extract(unknown).empty(),
                "Mode 2 with separate codec headers was misidentified as a full Ogg stream");
        auto coincidental = pcm;
        std::copy_n(reinterpret_cast<const uint8_t*>("OggS"), 4, coincidental.begin() + 66);
        require(!Audio::hasEmbeddedOgg(coincidental), "PCM sample bytes mistaken for Ogg");

        // Variable chunk offsets, odd metadata, reordered chunks and longer fmt.
        auto moved = wav;
        moved.insert(moved.begin() + 12, {'J','U','N','K',3,0,0,0,1,2,3,0});
        moved.insert(moved.end(), {'L','I','S','T',0,0,0,0});
        fixRiffSize(moved);
        require(Audio::extract(moved) == clean, "Metadata at variable offsets became audio");
        auto shortRiff = moved;
        writeU32(shortRiff, 4, shortRiff.size() - 9);
        require(Audio::extract(shortRiff) == clean,
                "Sound Forge RIFF length missing one alignment byte was rejected");
        auto noFact = wav;
        noFact.erase(noFact.begin() + 46, noFact.begin() + 58);
        fixRiffSize(noFact);
        require(Audio::extract(noFact) == clean, "Optional fact chunk was required");
        auto extended = wav;
        extended.insert(extended.begin() + 46, {1,2,3,4});
        writeU32(extended, 16, 30); extended[36] = 12;
        fixRiffSize(extended);
        require(Audio::extract(extended) == clean, "Variable-length fmt extension failed");
        std::vector<uint8_t> reordered(wav.begin(), wav.begin() + 12);
        reordered.insert(reordered.end(), wav.begin() + 58, wav.end());
        reordered.insert(reordered.end(), wav.begin() + 12, wav.begin() + 58);
        fixRiffSize(reordered);
        require(Audio::extract(reordered) == clean, "data before fmt was not located");
        auto decodedSize = moved;
        writeU32(decodedSize, 74, 0x7fffffffu);
        require(Audio::extract(decodedSize) == clean, "Legacy decoded-size chunk with metadata failed");

        std::vector<uint8_t> padding(28, 0);
        std::copy_n(reinterpret_cast<const uint8_t*>("OggS"), 4, padding.begin());
        padding[5] = 2; padding[26] = 1; writeU32(padding, 14, 0xffffffffu);
        checksum(padding);
        auto padded = clean;
        padded.insert(padded.begin() + 58, padding.begin(), padding.end());
        require(Audio::extract(Audio::embed(padded)) == clean,
                "Independent empty ACM padding stream was not removed");
        const auto special = testVorbis(1, 44100, 1257, -1);
        require(Audio::extract(Audio::embed(special)) == special,
                "Legal audio stream serial 0xffffffff was removed");

        for (auto invalid : {std::vector<uint8_t>{}, std::vector<uint8_t>{'b','a','d'}}) {
            rejects([&] { Audio::embed(invalid); }, "Invalid input was wrapped");
            rejects([&] { Audio::decodeToPcm(invalid); }, "Invalid input was decoded");
        }
        auto bad = clean; bad.back() ^= 1;
        rejects([&] { Audio::embed(bad); }, "Corrupt Ogg checksum accepted for wrapping");
        rejects([&] { Audio::decodeToPcm(bad); }, "Corrupt Ogg checksum accepted for PCM");
        auto truncated = clean; truncated.pop_back();
        rejects([&] { Audio::embed(truncated); }, "Truncated Ogg accepted");
        auto chained = clean; chained.insert(chained.end(), clean.begin(), clean.end());
        rejects([&] { Audio::decodeToPcm(chained); }, "Chained audio silently truncated");
        rejects([&] { Audio::embed(chained); }, "Chained Ogg wrapped");
        auto brokenRiff = wav; brokenRiff.resize(40);
        rejects([&] { Audio::extract(brokenRiff); }, "Truncated RIFF accepted");
        auto mismatch = wav; mismatch[22] = 2;
        rejects([&] { Audio::extract(mismatch); }, "WAV/Vorbis channel mismatch accepted");
        auto empty = wav; writeU32(empty, 62, 0);
        rejects([&] { Audio::extract(empty); }, "Empty compressed data chunk accepted");
        auto huge = wav; writeU32(huge, 16, 0xffffffffu);
        rejects([&] { Audio::extract(huge); }, "Overflowing fmt chunk accepted");

        if (argc == 2) {
            const std::string directory = argv[1];
            auto save = [&](const char* name, const std::vector<uint8_t>& data) {
                std::ofstream out(directory + "/" + name, std::ios::binary);
                out.write(reinterpret_cast<const char*>(data.data()), data.size());
                if (!out) throw std::runtime_error("Cannot write audio test input");
            };
            save("audio.ogg", clean); save("audio.wav", wav);
            save("badref.ogg", {'b','a','d'}); save("badref.wav", pcm);
            save("pcm.wav", pcm); save("mode3.wav", mode3); save("mode3.ogg", clean);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
