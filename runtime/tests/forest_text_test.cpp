#include "forest_text.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Surface {
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    void* pixels = nullptr;
    Surface() {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = 128;
        info.bmiHeader.biHeight = -64;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        require(dc && bitmap && pixels, "Cannot create GDI test surface");
        oldBitmap = SelectObject(dc, bitmap);
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkColor(dc, 0);
        SetBkMode(dc, OPAQUE);
        std::memset(pixels, 0, 128 * 64 * 4);
    }
    ~Surface() { SelectObject(dc, oldBitmap); DeleteObject(bitmap); DeleteDC(dc); }
};
}

int main() {
    using namespace liarsoft::runtime;
    try {
        auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x60000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        require(image, "Cannot allocate hook test image");
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = 0x80;
        auto* pe = reinterpret_cast<IMAGE_NT_HEADERS32*>(image + 0x80);
        pe->Signature = IMAGE_NT_SIGNATURE;
        pe->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
        pe->OptionalHeader.SizeOfImage = 0x60000;
        const uint8_t call[] = {0xff, 0x15, 0x38, 0x90, 0x45, 0x00};
        const uint8_t walk[] = {0x66, 0x0f, 0xb6, 0x02, 0x66, 0x0f, 0xb6, 0x52, 0x01,
                                0xc1, 0xe0, 0x08, 0x03, 0xc2};
        std::memcpy(image + 0x45542, call, sizeof(call));
        std::memcpy(image + 0x38836, walk, sizeof(walk));
        *reinterpret_cast<void**>(image + 0x59038) = reinterpret_cast<void*>(&GetGlyphOutlineA);
        *reinterpret_cast<void**>(image + 0x59024) = reinterpret_cast<void*>(&TextOutA);
        *reinterpret_cast<void**>(image + 0x5903c) = reinterpret_cast<void*>(&GetTextExtentPoint32A);
        *reinterpret_cast<void**>(image + 0x59184) = reinterpret_cast<void*>(&GetACP);
        GlyphTokens table;
        const auto transport = table.encode(u8"Ą");
        require(transport.size() == 2 && table.size() == 1, "GDI fixture needs one non-CP932 glyph");
        const auto token = UINT((uint8_t(transport[0]) << 8) | uint8_t(transport[1]));
        installForestTextHooks(image, table, nullptr);
        const auto codepage = *reinterpret_cast<decltype(&GetACP)*>(image + 0x59184);
        require(codepage() == 932 && *reinterpret_cast<void**>(image + 0x59184) != reinterpret_cast<void*>(&GetACP),
                "The engine CRT would use the system codepage instead of CP932");
        const auto glyph = *reinterpret_cast<decltype(&GetGlyphOutlineA)*>(image + 0x59038);
        const auto textOut = *reinterpret_cast<decltype(&TextOutA)*>(image + 0x59024);
        const auto extent = *reinterpret_cast<decltype(&GetTextExtentPoint32A)*>(image + 0x5903c);
        Surface actual, expected;
        HFONT font = CreateFontW(24, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                 0, 0, 0, 0, L"Arial");
        require(font, "Cannot create GDI test font");
        const auto oldActual = SelectObject(actual.dc, font);
        const auto oldExpected = SelectObject(expected.dc, font);
        MAT2 identity{};
        identity.eM11.value = identity.eM22.value = 1;
        GLYPHMETRICS a{}, e{};
        const auto expectedSize = GetGlyphOutlineW(expected.dc, 0x104, GGO_BITMAP, &e, 0, nullptr, &identity);
        const auto actualSize = glyph(actual.dc, token, GGO_BITMAP, &a, 0, nullptr, &identity);
        require(expectedSize != GDI_ERROR && actualSize == expectedSize && !std::memcmp(&a, &e, sizeof(a)),
                "Unicode outline metrics differ from GDI W API");
        std::vector<uint8_t> actualBits(actualSize), expectedBits(expectedSize);
        require(glyph(actual.dc, token, GGO_BITMAP, &a, actualSize, actualBits.data(), &identity) == actualSize,
                "Cannot rasterize the Unicode token");
        GetGlyphOutlineW(expected.dc, 0x104, GGO_BITMAP, &e, expectedSize, expectedBits.data(), &identity);
        require(actualBits == expectedBits, "Unicode token rendered different glyph pixels");
        SIZE actualExtent{}, expectedExtent{};
        require(extent(actual.dc, transport.data(), 2, &actualExtent) &&
                GetTextExtentPoint32W(expected.dc, L"Ą", 1, &expectedExtent) &&
                !std::memcmp(&actualExtent, &expectedExtent, sizeof(SIZE)), "Unicode text measurement differs");
        require(textOut(actual.dc, 3, 4, transport.data(), 2) && TextOutW(expected.dc, 3, 4, L"Ą", 1),
                "Unicode TextOut failed");
        GdiFlush();
        require(!std::memcmp(actual.pixels, expected.pixels, 128 * 64 * 4), "Unicode TextOut pixels differ");
        GLYPHMETRICS nativeActual{}, nativeExpected{};
        const auto nativeA = glyph(actual.dc, 'A', GGO_BITMAP, &nativeActual, 0, nullptr, &identity);
        const auto nativeE = GetGlyphOutlineA(expected.dc, 'A', GGO_BITMAP, &nativeExpected, 0, nullptr, &identity);
        require(nativeA == nativeE && !std::memcmp(&nativeActual, &nativeExpected, sizeof(nativeActual)),
                "Native glyph path changed");
        removeForestTextHooks();
        require(*reinterpret_cast<void**>(image + 0x59038) == reinterpret_cast<void*>(&GetGlyphOutlineA) &&
                *reinterpret_cast<void**>(image + 0x59024) == reinterpret_cast<void*>(&TextOutA) &&
                *reinterpret_cast<void**>(image + 0x5903c) == reinterpret_cast<void*>(&GetTextExtentPoint32A) &&
                *reinterpret_cast<void**>(image + 0x59184) == reinterpret_cast<void*>(&GetACP),
                "Unicode hooks were not restored");
        image[0x45542] = 0;
        bool rejected = false;
        try { installForestTextHooks(image, table, nullptr); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Unknown engine text bytes were accepted");
        SelectObject(actual.dc, oldActual);
        SelectObject(expected.dc, oldExpected);
        DeleteObject(font);
        VirtualFree(image, 0, MEM_RELEASE);
        std::cout << "Forest GDI Unicode hook tests passed\n";
    } catch (const std::exception& error) {
        liarsoft::runtime::removeForestTextHooks();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
