#include "cg_decompress.h"
#include "lim_decoder.h"
#include "wcg_decoder.h"
#include "fileio.h"

#include <filesystem>
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

template<class Function>
void rejects(Function function, const char* message) {
    try { function(); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error(message);
}

void rejectsWcg(const Bytes& data, const char* message) {
    rejects([&] { liarsoft::wcgDecode(data); }, message);
}

} // namespace

int main(int argc, char** argv) {
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

        auto single = imageHeader('W', 0x231, 32);
        for (uint8_t channel : {0x00, 0x11, 0x22, 0x33})
            appendBlock(single, {channel}, {0x20, 0xdd});
        require(liarsoft::wcgDecode(single).pixels == bgra,
                "Single-channel WCG decoding or block padding is incorrect");
        auto mask = imageHeader('W', 0x201, 32);
        appendBlock(mask, {0x80}, {0x20, 0xdd});
        require(liarsoft::wcgDecode(mask).pixels == Bytes({0, 0, 0, 0x7f}),
                "Alpha-only WCG did not preserve the mask");
        auto truncatedSingle = single;
        truncatedSingle.pop_back();
        rejectsWcg(truncatedSingle, "Truncated single-channel WCG was accepted");
        auto truncatedMask = mask;
        truncatedMask.resize(16);
        rejectsWcg(truncatedMask, "Missing WCG mask was accepted");

        // All four LIM channels use the same block framing, one byte per index.
        auto lim = imageHeader('L', 0x13, 24);
        for (uint8_t channel : {0x00, 0x11, 0x22, 0x33})
            appendBlock(lim, {channel}, {0x20, 0xdd});
        require(liarsoft::limDecode(lim).pixels == Bytes({0x11, 0x22, 0x33, 0xff}),
                "Padded LIM channels were misaligned");

        // Compressed BGR565 followed by compressed alpha also needs full-block advance.
        auto limMask = imageHeader('L', 3, 24);
        appendBlock(limMask, {0x80}, {0x20});
        require(liarsoft::limDecode(limMask).pixels == Bytes({0, 0, 0, 0x7f}),
                "Alpha-only LIM attempted to read nonexistent RGB blocks");

        auto lim16 = imageHeader('L', 0x332, 16);
        appendBlock(lim16, {0x00, 0xf8}, {0x20, 0xab, 0xcd});
        appendBlock(lim16, {0x00}, {0x20, 0xee});
        require(liarsoft::limDecode(lim16).pixels == Bytes({0xf8, 0x00, 0x00, 0xff}),
                "Padded LIM 16bpp/alpha channels were misaligned");
        auto keyed = imageHeader('L', 0x32, 16);
        appendBlock(keyed, {0xe0, 0x07}, {0x20});
        require(liarsoft::limDecode(keyed).pixels == Bytes({0, 0xfc, 0, 0}),
                "LIM BGR565 green transparency key was lost");
        auto explicitAlpha = imageHeader('L', 0x332, 16);
        appendBlock(explicitAlpha, {0xe0, 0x07}, {0x20});
        appendBlock(explicitAlpha, {0x00}, {0x20});
        require(liarsoft::limDecode(explicitAlpha).pixels == Bytes({0, 0xfc, 0, 0xff}),
                "Explicit LIM alpha did not override the color key");
        auto white16 = imageHeader('L', 0x32, 16);
        appendBlock(white16, {0xff, 0xff}, {0x20});
        require(liarsoft::limDecode(white16).pixels == Bytes({0xf8, 0xfc, 0xf8, 0xff}),
                "LIM BGR565 expansion differs from original engine");

        // Exercise every channel value, including invisible RGB and partial alpha.
        liarsoft::LimImage sourceImage{256, 4, {}};
        for (int alpha : {0, 1, 127, 255}) {
            for (int i = 0; i < 256; ++i)
                sourceImage.pixels.insert(sourceImage.pixels.end(), {
                    static_cast<uint8_t>(i), static_cast<uint8_t>(255 - i),
                    static_cast<uint8_t>((i * 37) & 255), static_cast<uint8_t>(alpha)});
        }
        const auto encodedLim = liarsoft::limEncode(sourceImage);
        require(encodedLim[0] == 'L' && encodedLim[1] == 'M' &&
                encodedLim[2] == 0x13 && encodedLim[3] == 0 && encodedLim[4] == 24,
                "LIM encoder did not produce a version-3 LIM header");
        require(liarsoft::limDecode(encodedLim).pixels == sourceImage.pixels,
                "Separate-channel LIM encoding lost RGBA pixels");
        const auto webp = liarsoft::webpEncode(sourceImage);
        const auto decodedWebp = liarsoft::webpDecode(webp);
        require(decodedWebp.width == sourceImage.width && decodedWebp.height == sourceImage.height &&
                decodedWebp.pixels == sourceImage.pixels,
                "Lossless WebP changed pixels, dimensions or invisible RGB");
        require(liarsoft::limDecode(liarsoft::limEncode(decodedWebp)).pixels == sourceImage.pixels,
                "LIM/WebP/LIM round trip lost pixels");
        for (const auto& legacy : {limMask, lim16, keyed, explicitAlpha, white16}) {
            const auto original = liarsoft::limDecode(legacy);
            const auto rebuilt = liarsoft::limEncode(
                liarsoft::webpDecode(liarsoft::webpEncode(original)));
            require(liarsoft::limDecode(rebuilt).pixels == original.pixels,
                    "Legacy LIM mask or BGR565 pixels were lost during WebP round trip");
        }
        rejects([&] { liarsoft::webpDecode({}); }, "Empty WebP was accepted");
        rejects([&] { liarsoft::webpDecode(Bytes{'n', 'o'}); }, "Invalid WebP was accepted");
        auto truncatedWebp = webp;
        truncatedWebp.resize(webp.size() / 2);
        rejects([&] { liarsoft::webpDecode(truncatedWebp); }, "Truncated WebP was accepted");
        auto animatedWebp = Bytes{'R', 'I', 'F', 'F', 22, 0, 0, 0,
                                 'W', 'E', 'B', 'P', 'V', 'P', '8', 'X',
                                 10, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        rejects([&] { liarsoft::webpDecode(animatedWebp); }, "Animated WebP was accepted");

        // The directory/CLI regression reuses these generated fixtures, not game assets.
        if (argc == 2) {
            const std::filesystem::path directory(argv[1]);
            std::filesystem::create_directories(directory);
            liarsoft::writeFileIfChanged((directory / "image.lim").string(), encodedLim);
            liarsoft::limSaveWebp(sourceImage, (directory / "image.webp").string());
            const auto decoded = liarsoft::wcgDecode(liarsoft::wcgEncode(rgba, 1, 1));
            liarsoft::wcgSavePng(decoded, (directory / "image.png").string());
        }

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

        // Exhaust every 16-bit pair: overflow may occur in either pass, or both.
        for (int overflowingPass : {0, 1, 2}) {
            Bytes rgba(65536 * 4), expected(65536 * 4);
            for (unsigned i = 0; i < 65536; ++i) {
                rgba[i*4] = overflowingPass == 0 ? 11 : i & 255;
                rgba[i*4+1] = overflowingPass == 1 ? 22 : i >> 8;
                rgba[i*4+2] = overflowingPass == 1 ? 33 : i & 255;
                rgba[i*4+3] = overflowingPass == 0 ? 255 : 255 - (i >> 8);
                expected[i*4] = rgba[i*4+2]; expected[i*4+1] = rgba[i*4+1];
                expected[i*4+2] = rgba[i*4]; expected[i*4+3] = rgba[i*4+3];
            }
            const auto encoded = liarsoft::wcgEncode(rgba, 256, 256);
            require(encoded[2] == 0x31 && encoded[3] == 0x02,
                    "65536-color pairs did not switch to single-channel WCG");
            require(liarsoft::wcgDecode(encoded).pixels == expected,
                    "Single-channel fallback changed colors or alpha");
            require(liarsoft::wcgEncode(rgba, 256, 256) == encoded,
                    "Equal-frequency palette ordering is not deterministic");
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

        require(liarsoft::checkedRgbaSize(1920, 1080) == 1920u * 1080 * 4,
                "Valid image size was changed");
        for (const auto& dimensions : std::vector<std::pair<uint32_t, uint32_t>>{
                 {0, 1}, {1, 0}, {0x80000000u, 0x80000000u}, {0x40000000u, 1},
                 {65536, 65536}, {0x400000, 1}, {1, 0x10000000}}) {
            auto bad = compact;
            bad.resize(8);
            append32(bad, dimensions.first);
            append32(bad, dimensions.second);
            bad.insert(bad.end(), compact.begin() + 16, compact.end());
            rejectsWcg(bad, "Invalid/overflowing WCG dimensions were accepted");
            bad[0] = 'L'; bad[1] = 'M';
            rejects([&] { liarsoft::limDecode(bad); }, "Invalid LIM dimensions were accepted");
            rejects([&] { liarsoft::wcgEncode(rgba, dimensions.first, dimensions.second); },
                    "Invalid encoder dimensions were accepted");
        }
        auto oversized = compact;
        oversized[9] = 0x10; // width 4097 but only two tiny blocks: reject before allocation
        rejectsWcg(oversized, "Impossible WCG expansion was accepted");
        auto oversizedLim = lim;
        oversizedLim[9] = 0x10;
        rejects([&] { liarsoft::limDecode(oversizedLim); }, "Impossible LIM expansion was accepted");
        rejects([&] { liarsoft::wcgEncode(Bytes{1, 2, 3}, 1, 1); },
                "Short RGBA buffer was accepted");
        rejects([&] { liarsoft::wcgEncode(Bytes(5), 1, 1); },
                "Mismatched RGBA buffer was accepted");
        rejects([&] { liarsoft::wcgEncode(nullptr, 1, 1); }, "Null RGBA buffer was accepted");
        rejects([&] { liarsoft::wcgSavePng({1, 1, Bytes(3)}, "unused-invalid.png"); },
                "Short BGRA PNG buffer was accepted");
        rejects([&] { liarsoft::limSaveWebp({1, 1, Bytes(3)}, "unused-invalid.webp"); },
                "Short LIM WebP buffer was accepted");
        rejects([&] { liarsoft::limEncode({1, 1, Bytes(3)}); },
                "Short LIM encoder buffer was accepted");
        rejects([&] { liarsoft::webpEncode({16384, 1, Bytes(16384 * 4)}); },
                "Unsupported WebP dimensions were accepted");

        std::cout << "CG block padding, WCG/LIM pixels and malformed-input checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
