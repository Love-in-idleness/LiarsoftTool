#include "engine_text.h"
#include "import_fixture.h"

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
        ImportFixture image(0xa000);
        for (const auto* name : {"GetGlyphOutlineA", "TextOutA", "GetTextExtentPoint32A"}) image.add("gdi32.dll", name);
        image.add("kernel32.dll", "GetACP");
        image.add("kernel32.dll", "IsDBCSLeadByteEx");
        GlyphTokens table;
        const auto transport = table.encode(u8"Ą");
        require(transport.size() == 2 && table.size() == 1, "GDI fixture needs one non-CP932 glyph");
        const auto token = UINT((uint8_t(transport[0]) << 8) | uint8_t(transport[1]));
        installEngineTextHooks(image.image, table, nullptr);
        const auto codepage = image.function<decltype(&GetACP)>("GetACP");
        require(codepage() == 932 && codepage != &GetACP,
                "The engine CRT would use the system codepage instead of CP932");
        const auto lead = image.function<decltype(&IsDBCSLeadByteEx)>("IsDBCSLeadByteEx");
        require(lead(CP_ACP, 0xf0) && !lead(1252, 0xf0), "DBCS detection still depends on the system language");
        const auto glyph = image.function<decltype(&GetGlyphOutlineA)>("GetGlyphOutlineA");
        const auto textOut = image.function<decltype(&TextOutA)>("TextOutA");
        const auto extent = image.function<decltype(&GetTextExtentPoint32A)>("GetTextExtentPoint32A");
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
        removeEngineTextHooks();
        require(image.function<decltype(&GetGlyphOutlineA)>("GetGlyphOutlineA") == &GetGlyphOutlineA &&
                image.function<decltype(&TextOutA)>("TextOutA") == &TextOutA &&
                image.function<decltype(&GetTextExtentPoint32A)>("GetTextExtentPoint32A") == &GetTextExtentPoint32A &&
                image.function<decltype(&GetACP)>("GetACP") == &GetACP,
                "Unicode hooks were not restored");
        ImportFixture unsupported;
        unsupported.add("kernel32.dll", "GetACP");
        bool rejected = false;
        try { installEngineTextHooks(unsupported.image, table, nullptr); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected, "Missing rendering APIs were accepted");
        SelectObject(actual.dc, oldActual);
        SelectObject(expected.dc, oldExpected);
        DeleteObject(font);
        std::cout << "Named-import GDI Unicode hook tests passed\n";
    } catch (const std::exception& error) {
        liarsoft::runtime::removeEngineTextHooks();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
