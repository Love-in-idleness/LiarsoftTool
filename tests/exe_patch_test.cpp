#include "exe_editor.h"
#include "exe_editor_fixture.h"
#include "fileio.h"
#include "pe_image.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
using namespace liarsoft;
namespace fs = std::filesystem;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void reject(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Unsafe EXE operation was accepted");
}
#if defined(_WIN32) && !defined(_WIN64)
std::array<uint32_t, 14> fontArguments{};
uint32_t WINAPI captureFont(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                            uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11,
                            uint32_t a12, uint32_t a13) {
    fontArguments = {a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13};
    return 0x12345678;
}
void execute(const std::vector<uint8_t> &bytes) {
    runtime::PeImage image(bytes);
    auto *memory =
        static_cast<uint8_t *>(VirtualAlloc(nullptr, image.size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    require(memory, "Cannot allocate test image");
    for (const auto &s : image.sections)
        std::memcpy(memory + s.rva, bytes.data() + s.offset, s.rawSize);
    const auto doc = inspectExe(bytes);
    const auto fontCallback = uint32_t(reinterpret_cast<uintptr_t>(&captureFont));
    for (const auto &font : doc.fontBridges)
        std::memcpy(memory + font.slot, &fontCallback, 4);
    DWORD old;
    require(VirtualProtect(memory, image.size, PAGE_EXECUTE_READ, &old), "Cannot protect test image");
    using Classifier = uint32_t(WINAPI *)(uint32_t);
    auto function = reinterpret_cast<Classifier>(memory + 0x1400);
    require(function(';') == 1 && function('(') == 2 && function('X') == 0,
            "Generated classifier result is incorrect");
    using FontCall = decltype(&captureFont);
    for (const auto &font : doc.fontBridges) {
        auto call = reinterpret_cast<FontCall>(memory + font.target);
        require(call(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14) == 0x12345678,
                "Font bridge return value differs");
        for (unsigned n = 0; n < 14; ++n)
            require(fontArguments[n] == (n == 8 ? font.charset : n + 1),
                    "Font bridge changed another argument");
    }
    VirtualFree(memory, 0, MEM_RELEASE);
}
#endif
int main(int argc, char **argv) {
    if (argc == 2) {
        const auto root = fs::u8path(argv[1]);
        fs::create_directories(root);
        writePathIfChanged(root / "game.exe", editorFixture().bytes);
        return 0;
    }
    const auto root = fs::temp_directory_path() /
                      fs::u8path("exe 中文 Русский " +
                                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        for (bool optimized : {false, true}) {
            const auto original = editorFixture(optimized);
            const auto doc = inspectExe(original.bytes);
            require(doc.sourceEncoding == "CP932" && doc.charsets == std::vector<uint8_t>{128} &&
                        doc.fontOperands.size() == 1,
                    "Font charset analysis differs");
            require(doc.texts.size() == 2 && doc.texts[0].editable && doc.texts[0].references.size() == 2,
                    "Shared prompt references missing");
            require(doc.classifiers.size() == 1, "Native classifier missing");
            ExeChanges change;
            change.texts[doc.texts[0].id] = "A much longer confirmation message that must survive relocation "
                                            "and reopening without truncation: %s";
            const auto edited = editExe(doc, change);
            const auto reopened = inspectExe(edited);
            require(reopened.texts.size() == 2 &&
                        std::any_of(reopened.texts.begin(), reopened.texts.end(),
                                    [&](const auto &t) { return t.text == change.texts.begin()->second; }),
                    "Long translation not restored");
            require(
                std::equal(original.bytes.begin() + 0x3400, original.bytes.end(), edited.begin() + 0x3400),
                "Overlay was changed");
            require(!reopened.expandedRules, "Text-only edit unexpectedly changed classifier code");
            require(editExe(reopened, {}) == edited, "Unchanged EXE was rewritten");
            change.texts[doc.texts[0].id] = "missing placeholder";
            reject([&] { editExe(doc, change); });
            change.texts.clear();
            change.texts["unknown"] = "text";
            reject([&] { editExe(doc, change); });
            change.texts.clear();
            change.rules = defaultExeRules("CP932");
            const auto expanded = editExe(doc, change);
            const auto expandedDoc = inspectExe(expanded);
            require(expandedDoc.expandedRules && expandedDoc.rules.noLineStart == change.rules->noLineStart,
                    "Expanded rules were not restored");
            require(editExe(expandedDoc, change) == expanded, "Identical expanded rules changed the file");
            ExeChanges second;
            second.texts[expandedDoc.texts[0].id] = "changed %s";
            const auto twice = inspectExe(editExe(expandedDoc, second));
            require(twice.expandedRules, "Repeated editing lost rules");
            auto invalid = expanded;
            const runtime::PeImage saved(invalid);
            const auto owned = std::find_if(saved.sections.begin(), saved.sections.end(),
                                            [](const auto &s) { return s.name == ".ltool"; });
            require(owned != saved.sections.end(), "New section missing");
            invalid[owned->offset + 8] = 42;
            reject([&] { inspectExe(invalid); });
            for (const auto &encoding : exeEncodings()) {
                ExeChanges request;
                request.encoding = encoding;
                request.rules = ExeRules{";", "("};
                for (const auto &item : doc.texts)
                    request.texts[item.id] = item.text.find("%s") != std::string::npos ? "save %s" : "OK";
                const auto next = editExe(doc, request);
                const auto result = inspectExe(next);
                require(result.sourceEncoding == encoding && result.rules.noLineStart == ";" &&
                            result.rules.noLineEnd == "(",
                        "Legacy target encoding or rules differ");
#if defined(_WIN32) && !defined(_WIN64)
                execute(next);
#endif
            }
            for (const auto *encoding : {"UTF-8", "65001", "GB18030"}) {
                ExeChanges request;
                request.encoding = encoding;
                reject([&] { editExe(doc, request); });
            }
            change.rules = ExeRules{";", ";"};
            reject([&] { editExe(doc, change); });
            change.rules = ExeRules{std::string(1, '\0'), ""};
            reject([&] { editExe(doc, change); });
            change.rules = ExeRules{u8"😀", ""};
            reject([&] { editExe(doc, change); });
            reject([&] { inspectExe(original.bytes, "UTF-8"); });
            unsigned polls = 0;
            reject([&] { inspectExe(original.bytes, "auto", [&] { return ++polls > 2; }); });
        }
        for (bool getter : {false, true}) {
            const auto fixture = expandedEditorFixture(getter);
            const auto doc = inspectExe(fixture.bytes, "CP932");
            require(doc.fontsComplete && doc.fontCalls == 1 &&
                        doc.fontOperands.size() + doc.fontBridges.size() == 1,
                    "Font argument getter was not handled");
            require(doc.texts.size() == 7, "Embedded title/scene/text fields missing");
            auto changes = defaultExeConfiguration(doc, "GBK");
            require(changes.texts.at("text:3340") == u8"确认", "Default stock prompt translation missing");
            changes.texts["text:3506"] = u8"中文游戏标题";
            changes.texts["text:3806"] = u8"中文场景标题";
            changes.texts["text:33a0"] = "English text";
            changes.texts["text:33e0"] = std::string(100, 'T');
            const auto edited = editExe(doc, changes);
            const auto next = inspectExe(edited);
            require(next.sourceEncoding == "GBK" && next.texts.size() == 7, "Expanded text did not reopen");
            require(std::any_of(next.texts.begin(), next.texts.end(),
                                [](const auto &t) { return t.text == "English text"; }),
                    "ASCII translation disappeared");
            require(editExe(next, {}) == edited, "Reopened extended EXE was not stable");
            if (getter) {
                require(next.fontBridges.size() == 1 && next.fontBridges[0].charset == 134,
                        "Saved font bridge missing");
#if defined(_WIN32) && !defined(_WIN64)
                execute(edited);
#endif
                auto altered = edited;
                runtime::PeImage image(altered);
                const auto at = size_t(image.range(next.fontBridges[0].target, 1) - image.bytes().data());
                altered[at] ^= 1;
                reject([&] { inspectExe(altered); });
            }
            changes.texts["text:3506"] = std::string(31, 'X');
            reject([&] { editExe(doc, changes); });
            changes.texts.erase("text:3506");
            changes.texts["text:3806"] = std::string(52, 'X');
            reject([&] { editExe(doc, changes); });
            changes.texts.erase("text:3806");
            changes.texts["text:33a0"] = std::string(100, 'X');
            reject([&] { editExe(doc, changes); });
            for (const auto &enc : exeEncodings()) {
                auto defaults = defaultExeConfiguration(doc, enc);
                require(defaults.encoding == enc && defaults.rules.has_value(),
                        "Default configuration incomplete");
                for (const auto &[id, text] : defaults.texts)
                    convertEncoding(text, "UTF-8", enc, true);
            }
        }
        for (bool bx : {false, true}) {
            const auto fixture = editorFixture(true, true, bx);
            const auto doc = inspectExe(fixture.bytes);
            require(doc.classifiers.size() == 1 && doc.classifiers[0].packedPairs == (bx ? 3u : 1u),
                    "Packed ASCII classifier variant missing");
            ExeChanges request;
            request.rules = ExeRules{";", "("};
            const auto result = editExe(doc, request);
            require(inspectExe(result).classifiers[0].packedPairs == (bx ? 3u : 1u),
                    "Packed ASCII behavior was lost");
#if defined(_WIN32) && !defined(_WIN64)
            execute(result);
#endif
        }
        auto pointers = editorFixture();
        std::fill(pointers.bytes.begin() + pointers.file(0x1000),
                  pointers.bytes.begin() + pointers.file(0x1200), 0xcc);
        pointers.put(0x98 + 16, 0x1800);
        pointers.put(pointers.file(0x3380), 0x403300);
        const std::vector<uint8_t> pointerCall{0xff, 0x35, 0x80, 0x33, 0x40, 0, 0x68, 0x40, 0x33,
                                               0x40, 0,    0xe8, 0,    0,    0, 0,    0xc3};
        std::copy(pointerCall.begin(), pointerCall.end(), pointers.bytes.begin() + pointers.file(0x1800));
        pointers.put(pointers.file(0x180c), uint32_t(0x1200 - 0x1810));
        const auto pointerDoc = inspectExe(pointers.bytes, "CP932");
        require(pointerDoc.texts.size() == 2 && pointerDoc.texts[0].editable,
                "Verified prompt pointer table was not recognized");
        ExeChanges pointerChange;
        pointerChange.texts[pointerDoc.texts[0].id] = "table translation %s";
        require(inspectExe(editExe(pointerDoc, pointerChange)).texts.size() == 2,
                "Pointer-table translation did not reopen");
        auto raw = editorFixture();
        raw.put(raw.file(0x3380), 0x403300);
        auto readOnly = inspectExe(raw.bytes);
        require(!readOnly.texts[0].editable, "Unverified pointer was treated as a safe reference");
        ExeChanges request;
        request.texts[readOnly.texts[0].id] = "change %s";
        reject([&] { editExe(readOnly, request); });
        raw = editorFixture();
        raw.put(0x98 + 96 + 4 * 8, 0x3400);
        raw.put(0x98 + 96 + 4 * 8 + 4, 0x100);
        const auto signedDoc = inspectExe(raw.bytes);
        require(!signedDoc.writable, "Signed image is writable");
        reject([&] { editExe(signedDoc, {}); });
        raw = editorFixture();
        raw.put(0x178 + 40 + 20, 0x400);
        reject([&] { inspectExe(raw.bytes); });
        raw = editorFixture();
        raw.put(0x98 + 60, 0x1c8);
        const auto noHeader = inspectExe(raw.bytes);
        request = {};
        request.texts[noHeader.texts[0].id] = "changed %s";
        reject([&] { editExe(noHeader, request); });
        const auto fixture = editorFixture();
        const auto doc = inspectExe(fixture.bytes);
        request = {};
        request.texts[doc.texts[0].id] = "saved %s";
        const auto source = root / "original.exe", output = root / "output.exe";
        writePathIfChanged(source, fixture.bytes);
        reject([&] { saveEditedExe(source, source, doc, request); });
        fs::create_hard_link(source, output);
        reject([&] { saveEditedExe(source, output, doc, request); });
        fs::remove(output);
#ifndef _WIN32
        fs::create_symlink(source, output);
        reject([&] { saveEditedExe(source, output, doc, request); });
        fs::remove(output);
#endif
        const std::vector<uint8_t> keep{'K', 'E', 'E', 'P'};
        writePathIfChanged(output, keep);
        fs::permissions(output, fs::perms::owner_read);
        reject([&] { saveEditedExe(source, output, doc, request); });
        require(readFile(output) == keep, "Failed save damaged previous output");
        fs::permissions(output, fs::perms::owner_read | fs::perms::owner_write);
#ifdef __linux__
        if (std::getenv("LD_PRELOAD"))
            for (const auto *fault : {"write", "close", "replace"}) {
                setenv("LIARSOFT_TEST_IO_FAILURE", fault, 1);
                reject([&] { saveEditedExe(source, output, doc, request); });
                unsetenv("LIARSOFT_TEST_IO_FAILURE");
                require(readFile(output) == keep, "Fault damaged output");
            }
#endif
        saveEditedExe(source, output, doc, request);
        const auto time = fs::last_write_time(output);
        saveEditedExe(source, output, doc, request);
        require(fs::last_write_time(output) == time, "Repeated build rewrote unchanged output");
        auto modified = fixture.bytes;
        modified.back() ^= 1;
        writePathIfChanged(source, modified);
        reject([&] { saveEditedExe(source, output, doc, request); });
        for (size_t length : {size_t(0), size_t(64), size_t(0x100), size_t(0x2450)})
            reject([&] { inspectExe({fixture.bytes.begin(), fixture.bytes.begin() + length}); });
        fs::remove_all(root);
        std::cout << "EXE editing: references, long text, encodings, expanded rules, re-editing, "
                     "cancellation and safe writes passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        std::error_code ignored;
        fs::permissions(root / "output.exe", fs::perms::owner_all, ignored);
        fs::remove_all(root, ignored);
        return 1;
    }
}
