#include "wav_ogg.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void writeU32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) data[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}

uint32_t readU32(const std::vector<uint8_t>& data, size_t offset) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
    return value;
}

void appendPage(std::vector<uint8_t>& out, uint32_t serial, uint32_t sequence,
                const std::vector<uint8_t>& payload, uint8_t flags = 0,
                uint32_t samples = 0) {
    const size_t start = out.size();
    out.resize(start + 28, 0);
    std::copy_n(reinterpret_cast<const uint8_t*>("OggS"), 4, out.begin() + start);
    out[start + 5] = flags;
    writeU32(out, start + 6, samples);
    writeU32(out, start + 14, serial);
    writeU32(out, start + 18, sequence);
    out[start + 26] = 1;
    out[start + 27] = static_cast<uint8_t>(payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
    uint32_t crc = 0;
    for (size_t i = start; i < out.size(); ++i) {
        crc ^= static_cast<uint32_t>(out[i]) << 24;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc << 1) ^ ((crc & 0x80000000u) ? 0x04c11db7u : 0);
    }
    writeU32(out, start + 22, crc);
}

std::vector<uint8_t> identification() {
    std::vector<uint8_t> packet(30, 0);
    std::copy_n(reinterpret_cast<const uint8_t*>("\x01vorbis"), 7, packet.begin());
    packet[11] = 1;
    writeU32(packet, 12, 44100);
    packet[28] = 0xb8;
    packet[29] = 1;
    return packet;
}

std::vector<uint8_t> reference() {
    std::vector<uint8_t> wav(66, 0);
    std::copy_n(reinterpret_cast<const uint8_t*>("RIFF"), 4, wav.begin());
    std::copy_n(reinterpret_cast<const uint8_t*>("WAVEfmt "), 8, wav.begin() + 8);
    writeU32(wav, 16, 26);
    wav[20] = 0x71; wav[21] = 0x67; wav[22] = 1;
    writeU32(wav, 24, 44100); writeU32(wav, 28, 16000);
    wav[32] = 1; wav[34] = 16; wav[36] = 8;
    const std::vector<uint8_t> codecExtra{1, 2, 2, 0x20, 0x31, 0x12, 1, 0x20};
    std::copy(codecExtra.begin(), codecExtra.end(), wav.begin() + 38);
    std::copy_n(reinterpret_cast<const uint8_t*>("fact"), 4, wav.begin() + 46);
    writeU32(wav, 50, 4); writeU32(wav, 54, 99999); // Deliberately stale duration.
    std::copy_n(reinterpret_cast<const uint8_t*>("data"), 4, wav.begin() + 58);
    return wav;
}

void require(bool result, const char* message) {
    if (!result) throw std::runtime_error(message);
}

template<typename Fn>
void rejects(Fn fn, const char* message) {
    try { fn(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error(message);
}

} // namespace

int main(int argc, char** argv) {
    try {
        using Audio = liarsoft::WavOggExtractor;
        std::vector<uint8_t> pcm(70, 0);
        std::copy_n(reinterpret_cast<const uint8_t*>("RIFF"), 4, pcm.begin());
        writeU32(pcm, 4, pcm.size() - 8);
        std::copy_n(reinterpret_cast<const uint8_t*>("WAVEfmt "), 8, pcm.begin() + 8);
        pcm[16] = 16; pcm[20] = 1; pcm[22] = 1;
        writeU32(pcm, 24, 44100); writeU32(pcm, 28, 88200);
        pcm[32] = 2; pcm[34] = 16;
        std::copy_n(reinterpret_cast<const uint8_t*>("data"), 4, pcm.begin() + 36);
        pcm[40] = 26;
        // PCM samples can coincidentally contain the embedded Ogg signature.
        std::copy_n(reinterpret_cast<const uint8_t*>("OggS"), 4, pcm.begin() + 66);
        require(Audio::isStandardPcmWav(pcm) && !Audio::hasEmbeddedOgg(pcm) &&
                Audio::extract(pcm).empty(), "PCM WAV misidentified as embedded Ogg");

        std::vector<uint8_t> clean;
        appendPage(clean, 7, 0, identification(), 2);
        appendPage(clean, 7, 1, {'a', 'b', 'c'}, 4, 1257);
        const auto ref = reference();
        rejects([&] { Audio::embed(clean, pcm); }, "PCM template was accepted");
        rejects([&] { Audio::embed(clean, std::vector<uint8_t>(66)); }, "Non-WAV template accepted");

        std::vector<uint8_t> wrappedPages;
        appendPage(wrappedPages, 7, 0, identification(), 2);
        appendPage(wrappedPages, 0xffffffffu, 0, {}, 2);
        appendPage(wrappedPages, 7, 1, {'a', 'b', 'c'}, 4, 1257);
        const auto wav = Audio::embed(wrappedPages, ref);
        require(Audio::extract(wav) == clean, "Wrapper padding page was not removed");
        require(readU32(wav, 62) == wrappedPages.size() &&
                readU32(wav, 4) == wav.size() - 8 && wav.size() % 2 == 0 &&
                readU32(wav, 54) == 1257, "WAV sizes, fact or alignment are invalid");
        require(std::equal(ref.begin() + 20, ref.begin() + 46, wav.begin() + 20),
                "Opaque codec parameters were changed");
        auto withTags = wav;
        withTags.insert(withTags.end(), {'L','I','S','T',0,0,0,0});
        require(Audio::extract(withTags) == clean, "Trailing RIFF metadata became audio");

        auto legacyWav = wav;
        writeU32(legacyWav, 62, 0x7fffffffu);
        require(Audio::extract(legacyWav) == clean, "Legacy decoded-size data chunk rejected");
        auto truncated = wav; truncated.resize(truncated.size() - 2);
        rejects([&] { Audio::extract(truncated); }, "Truncated audio accepted");
        writeU32(truncated, 62, 0);
        rejects([&] { Audio::extract(truncated); }, "Empty data chunk accepted");

        auto bad = clean; bad.back() ^= 1;
        rejects([&] { Audio::embed(bad, ref); }, "Corrupt Ogg checksum accepted");
        rejects([&] { Audio::embed({}, ref); }, "Empty Ogg accepted");
        rejects([&] { Audio::embed({'n','o','t','o','g','g'}, ref); }, "Non-Ogg data accepted");
        bad = clean; bad.pop_back();
        rejects([&] { Audio::embed(bad, ref); }, "Truncated Ogg accepted");
        bad = clean; bad.resize(58);
        rejects([&] { Audio::embed(bad, ref); }, "Missing EOS accepted");
        auto otherRef = ref; otherRef[22] = 2;
        rejects([&] { Audio::embed(clean, otherRef); }, "Different channel count accepted");
        otherRef = ref; writeU32(otherRef, 24, 48000);
        rejects([&] { Audio::embed(clean, otherRef); }, "Different sample rate accepted");

        std::vector<uint8_t> chained = clean;
        appendPage(chained, 8, 0, identification(), 2);
        rejects([&] { Audio::embed(chained, ref); }, "Chained Ogg accepted");
        std::vector<uint8_t> discontinuous;
        appendPage(discontinuous, 7, 0, identification(), 2);
        appendPage(discontinuous, 7, 2, {'a'}, 4, 1257);
        rejects([&] { Audio::embed(discontinuous, ref); }, "Missing page accepted");
        std::vector<uint8_t> notVorbis;
        appendPage(notVorbis, 7, 0, std::vector<uint8_t>(30), 2);
        rejects([&] { Audio::embed(notVorbis, ref); }, "Non-Vorbis Ogg accepted");

        // A legitimate Vorbis stream can itself use the wrapper's serial number.
        std::vector<uint8_t> specialSerial;
        appendPage(specialSerial, 0xffffffffu, 0, identification(), 2);
        appendPage(specialSerial, 0xffffffffu, 1, {}, 4, 0);
        const auto emptyAudio = Audio::embed(specialSerial, ref);
        require(Audio::extract(emptyAudio) == specialSerial &&
                readU32(emptyAudio, 4) == emptyAudio.size() - 8,
                "Real empty EOS page removed or even RIFF size incorrect");

        // Share structurally valid synthetic audio with the archive integration
        // test, rather than teaching it to rely on malformed WAV/Ogg fixtures.
        if (argc == 2) {
            const std::string directory = argv[1];
            auto save = [&](const char* name, const std::vector<uint8_t>& data) {
                std::ofstream out(directory + "/" + name, std::ios::binary);
                out.write(reinterpret_cast<const char*>(data.data()), data.size());
                if (!out) throw std::runtime_error("Cannot write audio test fixture");
            };
            save("audio.ogg", clean);
            save("audio.wav", Audio::embed(clean, ref));
            save("badref.ogg", clean);
            save("badref.wav", pcm);
            save("pcm.wav", pcm);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    return 0;
}
