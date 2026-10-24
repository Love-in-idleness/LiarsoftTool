#include "forest_text.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cstring>
#include <cwchar>
#include <stdexcept>

namespace liarsoft::runtime {
namespace {

// Forest.exe SHA256 8eb71d07304d362ec26cc47d93c6ef5854b5acadd7c9fe941755ad50820a3e56.
// REA ev_fc508a2faf06272d66027cd66c50c6adc18f7c7bc34f4f2e6610ed0f3f809290:
// 0x4453e0 rasterizes one DBCS glyph. REA ev_d824fba2c4320774bb4093626dadf3312428296a0939d40fd2e5ad4823d4aeda:
// 0x438790 checks CRT lead/trail and
// 0x438836-0x438842 builds lead<<8|trail, then 0x438866 advances two bytes.
// Glyph tokens therefore travel through the real parser, control-code handling,
// character cache and history without treating UTF-8 continuation bytes as text.
GlyphTokens* activeTokens = nullptr;
void (RSCRIPT_CALL *logger)(uint32_t, const char*) = nullptr;
decltype(&GetGlyphOutlineA) originalGlyph = nullptr;
decltype(&TextOutA) originalTextOut = nullptr;
decltype(&GetTextExtentPoint32A) originalExtent = nullptr;

struct ImportPatch { void** slot = nullptr; void* original = nullptr; };
std::array<ImportPatch, 4> patches;

// The static CRT initializes its DBCS table from GetACP before game scripts run.
// REA ev_17e895a8bdb39082341c3f9bdb782e5e4ef09b18431096f0819b17088726f365.
UINT WINAPI engineCodepage() { return 932; }

void report(const char* error) {
    if (logger) logger(RSCRIPT_LOG_ERROR, error);
    SetLastError(ERROR_NO_UNICODE_TRANSLATION);
}

// GDI outline extraction does not perform font linking. Try installed fonts
// with the same size/style before reporting a missing glyph, never draw '?'.
class GlyphFont {
public:
    GlyphFont(HDC dc, wchar_t cp) : dc_(dc) {
        WORD glyph;
        if (GetGlyphIndicesW(dc_, &cp, 1, &glyph, GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR && glyph != 0xffff)
            return;
        LOGFONTW description{};
        const auto current = GetCurrentObject(dc_, OBJ_FONT);
        if (GetObjectW(current, sizeof(description), &description) != sizeof(description))
            throw std::runtime_error("Cannot inspect the game's current font");
        const bool vertical = description.lfFaceName[0] == L'@';
        description.lfCharSet = DEFAULT_CHARSET;
        for (const auto* face : {L"Microsoft YaHei", L"SimSun", L"Yu Gothic", L"MS Gothic", L"Segoe UI", L"Noto Sans CJK JP"}) {
            std::wmemset(description.lfFaceName, 0, LF_FACESIZE);
            if (vertical) description.lfFaceName[0] = L'@';
            std::wcsncpy(description.lfFaceName + (vertical ? 1 : 0), face, LF_FACESIZE - 2);
            HFONT candidate = CreateFontIndirectW(&description);
            if (!candidate) continue;
            HGDIOBJ previous = SelectObject(dc_, candidate);
            if (!previous || previous == HGDI_ERROR) { DeleteObject(candidate); continue; }
            if (GetGlyphIndicesW(dc_, &cp, 1, &glyph, GGI_MARK_NONEXISTING_GLYPHS) != GDI_ERROR && glyph != 0xffff) {
                font_ = candidate;
                previous_ = previous;
                return;
            }
            SelectObject(dc_, previous);
            DeleteObject(candidate);
        }
        throw std::runtime_error("No installed font provides a requested Unicode glyph");
    }
    ~GlyphFont() {
        if (font_) { SelectObject(dc_, previous_); DeleteObject(font_); }
    }
    GlyphFont(const GlyphFont&) = delete;
    GlyphFont& operator=(const GlyphFont&) = delete;
private:
    HDC dc_;
    HFONT font_ = nullptr;
    HGDIOBJ previous_ = nullptr;
};

DWORD WINAPI unicodeGlyph(HDC dc, UINT character, UINT format, LPGLYPHMETRICS metrics,
                          DWORD size, LPVOID buffer, const MAT2* matrix) {
    if (!(format & GGO_GLYPH_INDEX) && character <= 0xffff && activeTokens) {
        if (const auto cp = activeTokens->glyph(uint16_t(character))) {
            try {
                GlyphFont font(dc, wchar_t(*cp));
                const auto result = GetGlyphOutlineW(dc, UINT(*cp), format, metrics, size, buffer, matrix);
                if (result != GDI_ERROR) return result;
                report("GetGlyphOutlineW could not rasterize a Unicode glyph");
            } catch (const std::exception& error) { report(error.what()); }
            // Forest checks zero, not GDI_ERROR, before allocating the bitmap.
            if (metrics) *metrics = {};
            return 0;
        }
    }
    return originalGlyph(dc, character, format, metrics, size, buffer, matrix);
}

BOOL WINAPI unicodeTextOut(HDC dc, int x, int y, LPCSTR text, int count) {
    if (!activeTokens || !text || count <= 0 || !activeTokens->containsTokens({text, size_t(count)}))
        return originalTextOut(dc, x, y, text, count);
    try {
        const auto wide = activeTokens->decode({text, size_t(count)});
        if (wide.size() == 1) {
            GlyphFont font(dc, wchar_t(wide[0]));
            return TextOutW(dc, x, y, reinterpret_cast<const wchar_t*>(wide.data()), int(wide.size()));
        }
        return TextOutW(dc, x, y, reinterpret_cast<const wchar_t*>(wide.data()), int(wide.size()));
    } catch (const std::exception& error) { report(error.what()); return FALSE; }
}

BOOL WINAPI unicodeExtent(HDC dc, LPCSTR text, int count, LPSIZE size) {
    if (!activeTokens || !text || count <= 0 || !activeTokens->containsTokens({text, size_t(count)}))
        return originalExtent(dc, text, count, size);
    try {
        const auto wide = activeTokens->decode({text, size_t(count)});
        if (wide.size() == 1) {
            GlyphFont font(dc, wchar_t(wide[0]));
            return GetTextExtentPoint32W(dc, reinterpret_cast<const wchar_t*>(wide.data()), int(wide.size()), size);
        }
        return GetTextExtentPoint32W(dc, reinterpret_cast<const wchar_t*>(wide.data()), int(wide.size()), size);
    } catch (const std::exception& error) { report(error.what()); return FALSE; }
}

void patchImport(ImportPatch& patch, uint8_t* base, size_t rva, const char* name, void* replacement,
                 const wchar_t* library = L"gdi32.dll") {
    void* expected = reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(library), name));
    auto** slot = reinterpret_cast<void**>(base + rva);
    if (!expected || *slot != expected)
        throw std::runtime_error(std::string("Unexpected Forest GDI import: ") + name);
    DWORD protection;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &protection))
        throw std::runtime_error("Cannot write a Forest GDI import slot");
    patch.slot = slot;
    patch.original = *slot;
    InterlockedExchangePointer(slot, replacement);
    DWORD ignored;
    if (!VirtualProtect(slot, sizeof(void*), protection, &ignored))
        throw std::runtime_error("Cannot restore Forest GDI import-slot protection");
}

} // namespace

void installForestTextHooks(void* module, GlyphTokens& tokens,
                            void (RSCRIPT_CALL *log)(uint32_t, const char*)) {
    static_assert(sizeof(void*) == 4 && sizeof(wchar_t) == 2, "Forest text hooks require x86 Windows");
    if (activeTokens) throw std::runtime_error("Forest Unicode hooks are already installed");
    auto* base = static_cast<uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096)
        throw std::runtime_error("Invalid Forest executable image");
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe->OptionalHeader.SizeOfImage < 0x59188)
        throw std::runtime_error("Unsupported Forest executable image");
    const unsigned char glyphCall[] = {0xff, 0x15, 0x38, 0x90, 0x45, 0x00};
    const unsigned char walker[] = {0x66, 0x0f, 0xb6, 0x02, 0x66, 0x0f, 0xb6, 0x52, 0x01,
                                   0xc1, 0xe0, 0x08, 0x03, 0xc2};
    if (std::memcmp(base + 0x45542, glyphCall, sizeof(glyphCall)) ||
        std::memcmp(base + 0x38836, walker, sizeof(walker)))
        throw std::runtime_error("Forest text code does not match the verified executable");
    logger = log;
    activeTokens = &tokens;
    originalGlyph = reinterpret_cast<decltype(originalGlyph)>(*reinterpret_cast<void**>(base + 0x59038));
    originalTextOut = reinterpret_cast<decltype(originalTextOut)>(*reinterpret_cast<void**>(base + 0x59024));
    originalExtent = reinterpret_cast<decltype(originalExtent)>(*reinterpret_cast<void**>(base + 0x5903c));
    try {
        patchImport(patches[0], base, 0x59038, "GetGlyphOutlineA", reinterpret_cast<void*>(&unicodeGlyph));
        patchImport(patches[1], base, 0x59024, "TextOutA", reinterpret_cast<void*>(&unicodeTextOut));
        patchImport(patches[2], base, 0x5903c, "GetTextExtentPoint32A", reinterpret_cast<void*>(&unicodeExtent));
        patchImport(patches[3], base, 0x59184, "GetACP", reinterpret_cast<void*>(&engineCodepage), L"kernel32.dll");
    } catch (...) { removeForestTextHooks(); throw; }
}

void removeForestTextHooks() {
    for (auto& patch : patches) {
        if (!patch.slot) continue;
        DWORD protection;
        if (!VirtualProtect(patch.slot, sizeof(void*), PAGE_READWRITE, &protection)) {
            report("Cannot remove a Unicode hook safely; terminating the game");
            TerminateProcess(GetCurrentProcess(), ERROR_INVALID_ADDRESS);
        }
        InterlockedExchangePointer(patch.slot, patch.original);
        DWORD ignored;
        if (!VirtualProtect(patch.slot, sizeof(void*), protection, &ignored)) {
            report("Cannot restore Unicode hook protection; terminating the game");
            TerminateProcess(GetCurrentProcess(), ERROR_INVALID_ADDRESS);
        }
        patch = {};
    }
    activeTokens = nullptr;
    logger = nullptr;
}

} // namespace liarsoft::runtime
