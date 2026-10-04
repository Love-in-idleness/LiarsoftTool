#include "cg_decompress.h"
#include "lim_decoder.h"
#include "wcg_decoder.h"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void append16(Bytes& out, uint16_t value) {
    out.push_back(value & 255);
    out.push_back(value >> 8);
}

void append32(Bytes& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back((value >> (i * 8)) & 255);
}

Bytes imageHeader(char type, uint16_t flags, uint16_t depth) {
    Bytes out{static_cast<uint8_t>(type), static_cast<uint8_t>(type == 'W' ? 'G' : 'M')};
    append16(out, flags);
    append16(out, depth);
    append16(out, 0);
    append32(out, 1); // width
    append32(out, 1); // height
    return out;
}

void appendBlock(Bytes& out, const Bytes& palette, const Bytes& compressed) {
    append32(out, static_cast<uint32_t>(palette.size())); // one pixel
    append32(out, static_cast<uint32_t>(compressed.size()));
    append16(out, 1); // one palette entry
    append16(out, 14); // this field is not the coding mode
    out.insert(out.end(), palette.begin(), palette.end());
    out.insert(out.end(), compressed.begin(), compressed.end());
}

void rejectsWcg(const Bytes& data, const char* message) {
    try { liarsoft::wcgDecode(data); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
        // 0010 = length 1, palette index 0. Later bytes are unused padding,
        // just like Khime's first WCG channel; they must not become headers.
        auto padded = imageHeader('W', 0x271, 32);
        appendBlock(padded, {0x11, 0x00}, {0x20, 0xa5, 0x5a});
        appendBlock(padded, {0x33, 0x22}, {0x20, 0xcc});
        const Bytes bgra{0x33, 0x22, 0x11, 0xff};
        require(liarsoft::wcgDecode(padded).pixels == bgra,
                "Padded WCG channels were misaligned or colors were changed");

        auto compact = imageHeader('W', 0x271, 32);
        appendBlock(compact, {0x11, 0x00}, {0x20});
        appendBlock(compact, {0x33, 0x22}, {0x20});
        require(liarsoft::wcgDecode(compact).pixels == bgra,
                "Unpadded WCG decoding regressed");
        const Bytes rgba{0x11, 0x22, 0x33, 0xff};
        require(liarsoft::wcgDecode(liarsoft::wcgEncode(rgba, 1, 1)).pixels == bgra,
                "WCG encoder/decoder round trip regressed");

        // All four LIM channels use the same block framing, one byte per index.
        auto lim = imageHeader('L', 0, 32);
        for (uint8_t channel : {0x00, 0x11, 0x22, 0x33})
            appendBlock(lim, {channel}, {0x20, 0xdd});
        require(liarsoft::limDecode(lim).pixels == Bytes({0x11, 0x22, 0x33, 0xff}),
                "Padded LIM channels were misaligned");

        // Compressed BGR565 followed by compressed alpha also needs full-block advance.
        auto lim16 = imageHeader('L', 0x330, 16);
        appendBlock(lim16, {0x00, 0xf8}, {0x20, 0xab, 0xcd});
        appendBlock(lim16, {0x00}, {0x20, 0xee});
        require(liarsoft::limDecode(lim16).pixels == Bytes({0xff, 0x00, 0x00, 0xff}),
                "Padded LIM 16bpp/alpha channels were misaligned");

        // Verify pointer advancement itself, including a larger palette/4-bit coding.
        Bytes largeBlock;
        append32(largeBlock, 2);
        append32(largeBlock, 3);
        append16(largeBlock, 0x1002);
        append16(largeBlock, 204);
        largeBlock.resize(12 + 0x1002 * 2, 0);
        largeBlock[12 + 2049 * 2] = 0x34;
        largeBlock[13 + 2049 * 2] = 0x12;
        largeBlock.insert(largeBlock.end(), {0xc0, 0x02, 0xaa}); // length 12, index 2049 + pad
        Bytes output(2), palette;
        const auto* source = largeBlock.data();
        liarsoft::cg_decompress_16bpp(output, 2, source, 0, palette,
                                     largeBlock.data() + largeBlock.size());
        require(output == Bytes({0x34, 0x12}) && source == largeBlock.data() + largeBlock.size(),
                "Large-palette decoding did not advance past declared padding");

        Bytes extendedBlock;
        append32(extendedBlock, 2);
        append32(extendedBlock, 3);
        append16(extendedBlock, 129);
        append16(extendedBlock, 14);
        extendedBlock.resize(12 + 129 * 2, 0);
        extendedBlock[12 + 128 * 2] = 0x78;
        extendedBlock[13 + 128 * 2] = 0x56;
        extendedBlock.insert(extendedBlock.end(), {0xf0, 0x00, 0xdd}); // extended index 128 + pad
        source = extendedBlock.data();
        liarsoft::cg_decompress_16bpp(output, 2, source, 0, palette,
                                     extendedBlock.data() + extendedBlock.size());
        require(output == Bytes({0x78, 0x56}) && source == extendedBlock.data() + extendedBlock.size(),
                "Extended index decoding regressed");

        // Independent engine-format fixtures: the last valid index at the
        // exact 4096/4097 boundary, not just our encoder reading its own output.
        for (unsigned count : {4096, 4097}) {
            Bytes block;
            append32(block, 2);
            append32(block, count == 4096 ? 3 : 2);
            append16(block, count);
            append16(block, 0);
            block.resize(12 + count * 2, 0);
            block[12 + (count - 1) * 2] = 0x34;
            block[13 + (count - 1) * 2] = 0x12;
            const Bytes bits = count == 4096 ? Bytes{0xff, 0x7f, 0xf0} : Bytes{0xd0, 0x00};
            block.insert(block.end(), bits.begin(), bits.end());
            source = block.data();
            liarsoft::cg_decompress_16bpp(output, 2, source, 0, palette,
                                         block.data() + block.size());
            require(output == Bytes({0x34, 0x12}), "4096/4097 palette boundary is incorrect");
        }

        for (unsigned count : {4095, 4096, 4097, 4098, 65535}) {
            Bytes rgba(count * 4), expected(count * 4);
            for (unsigned i = 0; i < count; ++i) {
                rgba[i*4] = 11; rgba[i*4+1] = i >> 8;
                rgba[i*4+2] = i; rgba[i*4+3] = 255;
                expected[i*4] = i; expected[i*4+1] = i >> 8;
                expected[i*4+2] = 11; expected[i*4+3] = 255;
            }
            require(liarsoft::wcgDecode(liarsoft::wcgEncode(rgba, count, 1)).pixels == expected,
                    "Palette boundary encoding did not round trip losslessly");
        }

        auto broken = compact;
        broken.resize(20);
        rejectsWcg(broken, "Truncated block header was accepted");
        broken = compact;
        broken[24] = 0xff; broken[25] = 0xff;
        rejectsWcg(broken, "Truncated palette was accepted");
        broken = compact;
        broken[23] = 0x80;
        rejectsWcg(broken, "Oversized compressed length was accepted");
        broken = compact;
        broken.pop_back();
        rejectsWcg(broken, "Truncated second channel was accepted");
        broken = compact;
        broken[24] = 0;
        rejectsWcg(broken, "Empty palette was accepted");
        broken = compact;
        broken[16] = 4;
        rejectsWcg(broken, "Wrong uncompressed size was accepted");
        broken = compact;
        broken[30] = 0x30; // palette index 1, but only entry 0 exists
        rejectsWcg(broken, "Out-of-range palette index was accepted");
        broken = imageHeader('W', 0x271, 32);
        appendBlock(broken, {0x11, 0x00}, {0x00, 0x40}); // repeat 2 pixels into 1
        appendBlock(broken, {0x33, 0x22}, {0x20});
        rejectsWcg(broken, "Oversized pixel run was accepted");
        broken = compact;
        broken[30] = 0xff; // unterminated extended index: EOF is not another '1'
        rejectsWcg(broken, "Truncated extended index was accepted");
        broken[30] = 0;
        rejectsWcg(broken, "Incomplete pixel data was silently accepted");

        std::cout << "CG block padding, WCG/LIM pixels and malformed-input checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
