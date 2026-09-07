#include "wav_ogg.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void appendPage(std::vector<uint8_t>& out, uint32_t serial, uint32_t sequence,
                const std::vector<uint8_t>& payload) {
    const size_t start = out.size();
    out.resize(start + 28, 0);
    out[start] = 'O'; out[start + 1] = 'g';
    out[start + 2] = 'g'; out[start + 3] = 'S';
    out[start + 14] = static_cast<uint8_t>(serial);
    out[start + 15] = static_cast<uint8_t>(serial >> 8);
    out[start + 16] = static_cast<uint8_t>(serial >> 16);
    out[start + 17] = static_cast<uint8_t>(serial >> 24);
    out[start + 18] = static_cast<uint8_t>(sequence);
    out[start + 19] = static_cast<uint8_t>(sequence >> 8);
    out[start + 20] = static_cast<uint8_t>(sequence >> 16);
    out[start + 21] = static_cast<uint8_t>(sequence >> 24);
    out[start + 26] = 1;
    out[start + 27] = static_cast<uint8_t>(payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
}

uint32_t readU32(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

} // namespace

int main() {
    std::vector<uint8_t> pcm(48, 0);
    std::copy_n(reinterpret_cast<const uint8_t*>("RIFF"), 4, pcm.begin());
    std::copy_n(reinterpret_cast<const uint8_t*>("WAVEfmt "), 8, pcm.begin() + 8);
    pcm[16] = 16;
    pcm[20] = 1;
    pcm[22] = 2;
    std::copy_n(reinterpret_cast<const uint8_t*>("data"), 4, pcm.begin() + 36);
    pcm[40] = 4;
    if (!liarsoft::WavOggExtractor::isStandardPcmWav(pcm) ||
        liarsoft::WavOggExtractor::hasEmbeddedOgg(pcm)) {
        std::cerr << "Standard PCM WAV was not recognized" << std::endl;
        return 1;
    }

    std::vector<uint8_t> clean;
    appendPage(clean, 7, 0, {'a', 'b', 'c'});
    appendPage(clean, 7, 1, {'d', 'e'});

    std::vector<uint8_t> wrappedPages;
    appendPage(wrappedPages, 7, 0, {'a', 'b', 'c'});
    appendPage(wrappedPages, 0xffffffffu, 0, {});
    appendPage(wrappedPages, 7, 1, {'d', 'e'});

    std::vector<uint8_t> reference(66, 0);
    const auto wav = liarsoft::WavOggExtractor::embed(wrappedPages, reference);
    if (liarsoft::WavOggExtractor::extract(wav) != clean) {
        std::cerr << "Wrapper padding page was not removed" << std::endl;
        return 1;
    }
    if (readU32(wav, 62) != wrappedPages.size() ||
        readU32(wav, 4) != 58 + wrappedPages.size() || wav.size() % 2 != 0) {
        std::cerr << "Embedded WAV sizes or alignment are invalid" << std::endl;
        return 1;
    }

    auto legacyWav = wav;
    legacyWav[62] = 0xff;
    legacyWav[63] = 0xff;
    legacyWav[64] = 0xff;
    legacyWav[65] = 0x7f;
    if (liarsoft::WavOggExtractor::extract(legacyWav) != clean) {
        std::cerr << "Legacy decoded-size data chunk was rejected" << std::endl;
        return 1;
    }

    auto truncated = wav;
    truncated.resize(truncated.size() - 2);
    try {
        liarsoft::WavOggExtractor::extract(truncated);
        std::cerr << "Truncated audio was silently accepted" << std::endl;
        return 1;
    } catch (const std::runtime_error&) {
    }
    return 0;
}
