#include "engine_text.h"
#include "imports.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstring>
#include <cwchar>
#include <stdexcept>

namespace liarsoft::runtime {
namespace {

// Static observations, not runtime address selectors:
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

std::vector<ImportPatch> patches;
decltype(&IsDBCSLeadByteEx) originalLead = nullptr;

// The static CRT initializes its DBCS table from GetACP before game scripts run.
// REA ev_17e895a8bdb39082341c3f9bdb782e5e4ef09b18431096f0819b17088726f365.
UINT WINAPI engineCodepage() { return 932; }
BOOL WINAPI engineLead(UINT codepage, BYTE value) {
    return originalLead(codepage == CP_ACP || codepage == CP_THREAD_ACP ? 932 : codepage, value);
}

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
            // Verified legacy renderers test zero, not GDI_ERROR, before allocation.
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

template<class Function> void patchImport(void* module, const char* name, Function hook, Function& original) {
    void* address = hookImport(module, name, reinterpret_cast<void*>(hook), patches);
    std::memcpy(&original, &address, sizeof(original));
}

} // namespace

void installEngineTextHooks(void* module, GlyphTokens& tokens,
                            void (RSCRIPT_CALL *log)(uint32_t, const char*)) {
    static_assert(sizeof(wchar_t) == 2, "Text hooks require Windows UTF-16");
    if (activeTokens) throw std::runtime_error("Unicode hooks are already installed");
    logger = log;
    activeTokens = &tokens;
    try {
        patchImport(module, "GetGlyphOutlineA", unicodeGlyph, originalGlyph);
        patchImport(module, "TextOutA", unicodeTextOut, originalTextOut);
        patchImport(module, "GetTextExtentPoint32A", unicodeExtent, originalExtent);
        if (!originalGlyph && !originalTextOut)
            throw std::runtime_error("No supported ANSI GDI text API is imported");
        hookImport(module, "GetACP", reinterpret_cast<void*>(&engineCodepage), patches);
        patchImport(module, "IsDBCSLeadByteEx", engineLead, originalLead);
    } catch (...) { removeEngineTextHooks(); throw; }
}

void removeEngineTextHooks() {
    try { restoreImports(patches); }
    catch (...) {
        report("Cannot remove Unicode imports safely; terminating the game");
        TerminateProcess(GetCurrentProcess(), ERROR_INVALID_ADDRESS);
    }
    activeTokens = nullptr;
    logger = nullptr;
}

} // namespace liarsoft::runtime
