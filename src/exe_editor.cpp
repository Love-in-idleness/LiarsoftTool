#include "exe_editor.h"
#include "../third_party/hde32/hde32.h"
#include "encoding.h"
#include "exe_patch.h"
#include "fileio.h"
#include "kinsoku.h"
#include "kinsoku_match.h"
#include "pe_image.h"
#include "utf8_text.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace liarsoft {
namespace {
using runtime::PeImage;
using runtime::PeSection;
constexpr size_t maxImage = 128 * 1024 * 1024;
constexpr uint8_t charsets[] = {128, 134, 136, 129, 238, 204, 0, 161, 162, 177, 178, 186, 163};
void poll(const ExeCancel &cancel) {
    if (cancel && cancel())
        throw std::runtime_error("EXE operation cancelled");
}
uint32_t number(const std::vector<uint8_t> &b, size_t pos, size_t length = 4) {
    runtime::checkRange(pos, length, b.size());
    uint32_t n = 0;
    for (size_t i = 0; i < length; ++i)
        n |= uint32_t(b[pos + i]) << (i * 8);
    return n;
}
void put(std::vector<uint8_t> &b, size_t pos, uint32_t n, size_t length = 4) {
    runtime::checkRange(pos, length, b.size());
    for (size_t i = 0; i < length; ++i)
        b[pos + i] = uint8_t(n >> (i * 8));
}
void append(std::vector<uint8_t> &b, uint32_t n) {
    const auto pos = b.size();
    b.resize(pos + 4);
    put(b, pos, n);
}
void string(std::vector<uint8_t> &b, const std::string &s) {
    if (s.size() > UINT32_MAX)
        throw std::runtime_error("EXE text is too large");
    append(b, uint32_t(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}
uint32_t align(uint64_t n, uint32_t alignment) {
    if (!alignment || (alignment & (alignment - 1)) || n > UINT32_MAX - alignment)
        throw std::runtime_error("Unsupported PE alignment or image extent");
    return uint32_t((n + alignment - 1) & ~(uint64_t(alignment) - 1));
}
size_t file(const PeImage &p, uint32_t rva, size_t length = 1) {
    return size_t(p.range(rva, length) - p.bytes().data());
}
const PeSection *section(const PeImage &p, uint32_t rva) {
    for (const auto &s : p.sections)
        if (rva >= s.rva && uint64_t(rva) - s.rva < std::min(s.rawSize, s.virtualSize))
            return &s;
    return nullptr;
}
std::string literal(const PeImage &image, uint32_t rva) {
    const auto *s = section(image, rva);
    if (!s)
        return {};
    const auto at = file(image, rva);
    const auto length = std::min<size_t>(16385, s->offset + std::min(s->rawSize, s->virtualSize) - at);
    const auto end = std::find(image.bytes().begin() + at, image.bytes().begin() + at + length, 0);
    if (end == image.bytes().begin() + at + length)
        return {};
    return {image.bytes().begin() + at, end};
}
uint8_t charset(const std::string &encoding) {
    const auto name = normalizeEncodingName(encoding);
    const auto &names = exeEncodings();
    const auto i = std::find(names.begin(), names.end(), name);
    if (i == names.end())
        throw std::runtime_error("Unsupported EXE encoding: " + encoding);
    return charsets[i - names.begin()];
}
std::string encoding(uint8_t c) {
    for (size_t i = 0; i < std::size(charsets); ++i)
        if (charsets[i] == c)
            return exeEncodings()[i];
    return {};
}
std::u16string wide(const std::string &s) { return runtime::decodeUtf8(s); }
std::string utf8(const std::u16string &s) {
    std::string out;
    for (uint32_t c : s) {
        if (c >= 0xd800 && c <= 0xdfff)
            throw std::runtime_error("EXE rules require BMP characters");
        if (c < 0x80)
            out += char(c);
        else if (c < 0x800) {
            out += char(0xc0 | (c >> 6));
            out += char(0x80 | (c & 63));
        } else {
            out += char(0xe0 | (c >> 12));
            out += char(0x80 | ((c >> 6) & 63));
            out += char(0x80 | (c & 63));
        }
    }
    return out;
}
std::vector<uint16_t> codes(const std::string &text, const std::string &enc) {
    if (text.size() > 256 * 1024)
        throw std::runtime_error("EXE rule text exceeds 256 KiB");
    std::vector<uint16_t> values;
    std::set<uint16_t> seen;
    for (auto c : wide(text)) {
        if (!c)
            throw std::runtime_error("NUL is not a line-break rule character");
        const auto bytes = convertEncoding(utf8({c}), "UTF-8", enc, true);
        if (bytes.empty() || bytes.size() > 2 || convertEncoding(bytes, enc, "UTF-8", true) != utf8({c}))
            throw std::runtime_error("Rule character cannot round-trip in " + enc);
        const auto value = bytes.size() == 2 ? uint16_t(uint8_t(bytes[0]) * 256 + uint8_t(bytes[1]))
                                             : uint16_t(int16_t(int8_t(bytes[0])));
        if (seen.insert(value).second)
            values.push_back(value);
    }
    return values;
}
void validate(const ExeRules &rules, const std::string &enc) {
    const auto a = codes(rules.noLineStart, enc), b = codes(rules.noLineEnd, enc);
    for (auto c : b)
        if (std::find(a.begin(), a.end(), c) != a.end())
            throw std::runtime_error("Line-start and line-end rules overlap");
}
struct Instruction {
    uint32_t rva;
    size_t offset;
    hde32s decoded;
};
struct Code {
    std::map<uint32_t, Instruction> items;
    std::set<uint32_t> roots;
};
Code instructions(const PeImage &image, uint32_t base, uint32_t entry, const ExeCancel &cancel) {
    Code result;
    std::deque<uint32_t> pending{entry};
    // Native callback/vtable roots complement direct calls from the PE entry point.
    for (const auto &s : image.sections)
        if (!(s.flags & 0x20000000)) {
            for (size_t pos = s.offset; pos + 4 <= size_t(s.offset) + s.rawSize; pos += 4) {
                const auto value = number(image.bytes(), pos);
                const auto *target = value >= base ? section(image, value - base) : nullptr;
                if (target && (target->flags & 0x20000000))
                    pending.push_back(value - base);
            }
        }
    // MSVC emits alignment padding between functions not reachable through direct calls.
    for (const auto &s : image.sections)
        if (s.flags & 0x20000000) {
            const auto end = size_t(s.offset) + std::min(s.rawSize, s.virtualSize);
            for (size_t pos = s.offset + 16; pos + 8 <= end; pos += 16) {
                const auto &b = image.bytes();
                if (b[pos] != 0x90 && b[pos] != 0xcc &&
                    std::all_of(b.begin() + pos - 5, b.begin() + pos,
                                [](uint8_t c) { return c == 0x90 || c == 0xcc; }))
                    pending.push_back(s.rva + uint32_t(pos - s.offset));
            }
        }
    while (!pending.empty()) {
        poll(cancel);
        uint32_t rva = pending.front();
        pending.pop_front();
        result.roots.insert(rva);
        while (!result.items.count(rva)) {
            if (!(result.items.size() & 4095))
                poll(cancel);
            if (result.items.size() >= 500000)
                throw std::runtime_error("EXE instruction analysis budget exceeded");
            const auto *s = section(image, rva);
            if (!s || !(s->flags & 0x20000000))
                break;
            const auto offset = file(image, rva);
            std::array<uint8_t, 32> buffer{};
            const auto remaining = std::min<size_t>(s->rawSize - (offset - s->offset), buffer.size());
            std::copy_n(image.bytes().data() + offset, remaining, buffer.data());
            hde32s h{};
            hde32_disasm(buffer.data(), &h);
            if (!h.len || h.len > remaining || h.flags & F_ERROR)
                break;
            result.items.emplace(rva, Instruction{rva, offset, h});
            if ((h.flags & F_IMM32) && !(h.flags & F_RELATIVE) && h.imm.imm32 >= base) {
                const auto address = h.imm.imm32 - base;
                const auto *t = section(image, address);
                if (t && (t->flags & 0x20000000) &&
                    (h.opcode == 0x68 || (h.opcode >= 0xb8 && h.opcode <= 0xbf)))
                    pending.push_back(address);
            }
            if (h.opcode == 0xff && h.modrm_reg == 4 && h.modrm_mod == 0 && h.modrm_rm == 4 &&
                h.sib_base == 5 && h.sib_scale == 2 && h.disp.disp32 >= base) {
                const auto table = h.disp.disp32 - base;
                const auto *ts = section(image, table);
                if (ts)
                    for (size_t n = 0; n < 256; ++n) {
                        const auto at = uint64_t(table) + n * 4;
                        if (at + 4 > uint64_t(ts->rva) + std::min(ts->rawSize, ts->virtualSize))
                            break;
                        const auto value = number(image.bytes(), file(image, uint32_t(at), 4));
                        const auto *target = value >= base ? section(image, value - base) : nullptr;
                        if (!target || !(target->flags & 0x20000000))
                            break;
                        pending.push_back(value - base);
                    }
            }
            if (h.flags & F_RELATIVE) {
                const int64_t displacement = h.flags & F_IMM8 ? int8_t(h.imm.imm8) : int32_t(h.imm.imm32);
                const auto target = int64_t(rva) + h.len + displacement;
                if (target >= 0 && target <= UINT32_MAX)
                    pending.push_back(uint32_t(target));
            }
            if (h.opcode == 0xc3 || h.opcode == 0xc2 || h.opcode == 0xcc || h.opcode == 0xe9 ||
                h.opcode == 0xeb || (h.opcode == 0xff && h.modrm_reg == 4))
                break;
            rva += h.len;
        }
    }
    return result;
}
std::vector<const Instruction *> before(const Code &code, uint32_t rva, unsigned count = 48) {
    std::vector<const Instruction *> found;
    auto it = code.items.lower_bound(rva);
    while (it != code.items.begin() && found.size() < count) {
        --it;
        const auto &instruction = it->second;
        if (instruction.rva + instruction.decoded.len != rva)
            break;
        found.push_back(&instruction);
        rva = instruction.rva;
        if (instruction.decoded.opcode == 0xc3 || instruction.decoded.opcode == 0xc2 ||
            instruction.decoded.opcode == 0xe8 || instruction.decoded.opcode == 0xe9 ||
            (instruction.decoded.opcode == 0xff && instruction.decoded.modrm_reg >= 2))
            break;
    }
    return found;
}
bool push(const hde32s &h) {
    return h.opcode == 0x68 || h.opcode == 0x6a || (h.opcode >= 0x50 && h.opcode <= 0x57) ||
           (h.opcode == 0xff && h.modrm_reg == 6);
}
std::vector<const Instruction *> pushes(const Code &code, uint32_t call, size_t count) {
    std::vector<const Instruction *> result;
    for (auto *i : before(code, call)) {
        if (push(i->decoded))
            result.push_back(i);
        if (result.size() == count) {
            std::reverse(result.begin(), result.end());
            return result;
        }
        if (i->decoded.opcode == 0x83 && i->decoded.modrm == 0xc4)
            break;
    }
    return {};
}
std::string called(const Instruction &i, const Code &code, const std::map<uint32_t, std::string> &imports,
                   uint32_t base) {
    const auto &h = i.decoded;
    if (h.opcode != 0xff || h.modrm_reg != 2)
        return {};
    if (h.modrm == 0x15) {
        const auto found = imports.find(h.disp.disp32);
        return found == imports.end() ? "" : found->second;
    }
    if (h.modrm_mod == 3) {
        const auto reg = h.modrm_rm;
        for (auto *prev : before(code, i.rva, 24)) {
            const auto &p = prev->decoded;
            if (p.opcode == 0x8b && p.modrm_reg == reg && p.modrm_mod == 0 && p.modrm_rm == 5) {
                const auto found = imports.find(p.disp.disp32);
                return found == imports.end() ? "" : found->second;
            }
            if ((p.opcode >= 0xb8 && p.opcode <= 0xbf && p.opcode - 0xb8 == reg) ||
                (p.opcode == 0x8b && p.modrm_reg == reg))
                break;
        }
    }
    (void)base;
    return {};
}
std::vector<std::string> formats(const std::string &s) {
    std::vector<std::string> out;
    for (size_t p = 0; p < s.size(); ++p)
        if (s[p] == '%' && p + 1 < s.size()) {
            if (s[p + 1] == '%') {
                ++p;
                continue;
            }
            size_t end = p + 1;
            std::string shape;
            while (end < s.size() &&
                   std::string("-+ #0.123456789*hlLjztI$").find(s[end]) != std::string::npos) {
                if (s[end] == '*' || std::string("hlLjztI$").find(s[end]) != std::string::npos)
                    shape += s[end];
                ++end;
            }
            if (end < s.size() && std::string("diuoxXfFeEgGaAcCsSpn").find(s[end]) != std::string::npos) {
                out.push_back(s.substr(p, end - p + 1));
                p = end;
            }
        }
    return out;
}
std::string id(uint32_t rva) {
    std::ostringstream out;
    out << "text:" << std::hex << rva;
    return out.str();
}
struct Reader {
    const std::vector<uint8_t> &b;
    size_t pos, end;
    uint32_t get() {
        if (pos > end || end - pos < 4)
            throw std::runtime_error("Truncated EXE editor metadata");
        auto n = number(b, pos);
        pos += 4;
        return n;
    }
    std::string text() {
        auto n = get();
        if (n > end - pos)
            throw std::runtime_error("Invalid EXE metadata string");
        std::string s(b.begin() + pos, b.begin() + pos + n);
        pos += n;
        wide(s);
        return s;
    }
};
std::vector<uint8_t> classifier(const ExeClassifier &block, const ExeRules &rules, const std::string &enc,
                                uint32_t origin) {
    std::vector<uint8_t> code{0x9c, 0x60};
    std::vector<std::pair<size_t, uint32_t>> patches;
    auto jump = [&](uint32_t target) {
        code.push_back(0xe9);
        const auto offset = code.size();
        append(code, 0);
        patches.push_back({offset, target});
    };
    auto group = [&](const std::vector<uint16_t> &values, uint32_t target, bool bx = false) {
        std::vector<size_t> branches;
        for (auto value : values) {
            code.insert(code.end(), {0x66, 0x81, 0xfe, uint8_t(value), uint8_t(value >> 8), 0x0f, 0x84});
            branches.push_back(code.size());
            append(code, 0);
        }
        if (bx) {
            code.insert(code.end(), {0x66, 0x3b, 0xf3, 0x0f, 0x84});
            branches.push_back(code.size());
            append(code, 0);
        }
        if (branches.empty())
            return;
        code.push_back(0xe9);
        const auto next = code.size();
        append(code, 0);
        const auto match = code.size();
        code.insert(code.end(), {0x61, 0x9d});
        jump(target);
        put(code, next, uint32_t(code.size() - next - 4));
        for (auto branch : branches)
            put(code, branch, uint32_t(match - branch - 4));
    };
    if (block.packedPairs & 1)
        group(block.packedPairs & 2 ? std::vector<uint16_t>{0x213f, 0x3f21}
                                    : std::vector<uint16_t>{0x213f, 0x3f21, 0x2121},
              block.noStart, bool(block.packedPairs & 2));
    group(codes(rules.noLineStart, enc), block.noStart);
    group(codes(rules.noLineEnd, enc), block.noEnd);
    code.insert(code.end(), {0x61, 0x9d});
    jump(block.normal);
    for (auto [offset, target] : patches)
        put(code, offset, uint32_t(target - (origin + offset + 4)));
    return code;
}
std::vector<uint8_t> fontBridge(const ExeFontBridge &font, uint32_t origin, uint32_t targetCharset) {
    // Position-independent tail call: replace only CreateFontA's charset argument.
    std::vector<uint8_t> code{0xc7, 0x44, 0x24, 0x24};
    append(code, targetCharset);
    code.insert(code.end(), {0x50, 0xe8, 0, 0, 0, 0, 0x58, 0x8b, 0x80});
    append(code, font.slot - origin - 14);
    code.insert(code.end(), {0x87, 0x04, 0x24, 0xc3});
    return code;
}
void readMetadata(const PeImage &image, ExeDocument &doc) {
    for (const auto &s : image.sections)
        if (s.name == ".ltool") {
            const auto &b = image.bytes();
            const auto off = s.offset;
            if (s.rawSize < 24 || std::memcmp(b.data() + off, "LSTEXE3", 8) || number(b, off + 8) != 3)
                throw std::runtime_error("Invalid EXE editor section");
            const auto rva = number(b, off + 12), length = number(b, off + 16);
            if (length > 2 * 1024 * 1024 || rva < s.rva || length > s.virtualSize ||
                rva - s.rva > s.virtualSize - length)
                throw std::runtime_error("Invalid EXE metadata extent");
            const auto start = file(image, rva, length);
            Reader reader{b, start, start + length};
            const auto enc = reader.text();
            charset(enc);
            if (doc.sourceEncoding.empty())
                doc.sourceEncoding = enc;
            doc.rules = {reader.text(), reader.text()};
            validate(doc.rules, enc);
            const auto count = reader.get();
            if (count > length / 32)
                throw std::runtime_error("Invalid EXE classifier count");
            doc.expandedRules = count != 0;
            for (unsigned n = 0; n < count; ++n) {
                ExeClassifier block{reader.get(), reader.get(), reader.get(), reader.get(), {}};
                const auto size = reader.get();
                if (size != 5 || size > reader.end - reader.pos)
                    throw std::runtime_error("Invalid saved classifier");
                block.original.assign(b.begin() + reader.pos, b.begin() + reader.pos + size);
                reader.pos += size;
                block.packedPairs = reader.get();
                if (block.packedPairs > 3 || block.packedPairs == 2)
                    throw std::runtime_error("Invalid packed-character classifier");
                const auto target = reader.get(), codeLength = reader.get();
                const auto code = classifier(block, doc.rules, enc, target);
                const auto actual = file(image, target, codeLength);
                if (code.size() != codeLength || !std::equal(code.begin(), code.end(), b.begin() + actual))
                    throw std::runtime_error("Generated EXE classifier was modified");
                const auto entry = file(image, block.entry, 5);
                if (b[entry] != 0xe9 || uint32_t(block.entry + 5 + number(b, entry + 1)) != target)
                    throw std::runtime_error("EXE classifier redirect was modified");
                const auto *originalSection = section(image, block.entry);
                if (!originalSection || !(originalSection->flags & 0x20000000) || target < s.rva ||
                    codeLength > s.virtualSize || target - s.rva > s.virtualSize - codeLength)
                    throw std::runtime_error("Invalid classifier code placement");
                auto restored = b;
                std::copy(block.original.begin(), block.original.end(), restored.begin() + entry);
                normalizeExeKinsoku(restored);
                const auto matches = runtime::findKinsokuBlocks(
                    restored.data() + originalSection->offset,
                    std::min(originalSection->rawSize, originalSection->virtualSize));
                if (std::none_of(matches.begin(), matches.end(), [&](const auto &candidate) {
                        return candidate.offset + originalSection->rva == block.entry &&
                               candidate.normal + originalSection->rva == block.normal &&
                               candidate.noStart + originalSection->rva == block.noStart &&
                               candidate.noEnd + originalSection->rva == block.noEnd &&
                               candidate.packedPairs == block.packedPairs;
                    }))
                    throw std::runtime_error("Saved classifier does not match original code");
                doc.classifiers.push_back(std::move(block));
            }
            const auto fonts = reader.get();
            if (fonts > length / 16)
                throw std::runtime_error("Invalid font bridge count");
            const auto imports = image.imports();
            for (uint32_t n = 0; n < fonts; ++n) {
                ExeFontBridge font{reader.get(), reader.get(), reader.get(), reader.get()};
                const auto expected = fontBridge(font, font.target, font.charset);
                const auto at = file(image, font.target, expected.size());
                const auto entry = file(image, font.entry, 6);
                const auto *original = section(image, font.entry);
                if (font.charset > 255 || font.target < s.rva ||
                    uint64_t(font.target) + expected.size() > uint64_t(s.rva) + s.virtualSize || !original ||
                    !(original->flags & 0x20000000) || original->name == ".ltool" || b[entry] != 0xe8 ||
                    b[entry + 5] != 0x90 || font.entry + 5 + number(b, entry + 1) != font.target ||
                    !std::equal(expected.begin(), expected.end(), b.begin() + at) ||
                    std::none_of(imports.begin(), imports.end(), [&](const auto &i) {
                        auto dll = i.dll;
                        std::transform(dll.begin(), dll.end(), dll.begin(),
                                       [](unsigned char c) { return char(std::tolower(c)); });
                        return i.slot == font.slot && dll == "gdi32.dll" && i.name == "CreateFontA";
                    }))
                    throw std::runtime_error("Invalid saved font bridge");
                doc.fontBridges.push_back(font);
            }
            const auto texts = reader.get();
            if (texts > length / 12)
                throw std::runtime_error("Invalid saved text count");
            for (uint32_t n = 0; n < texts; ++n) {
                ExeText text;
                text.rva = reader.get();
                text.capacity = reader.get();
                text.role = reader.text();
                if (!text.capacity || text.capacity > 16385 || text.role != "Embedded text")
                    throw std::runtime_error("Invalid saved EXE text field");
                file(image, text.rva, text.capacity);
                doc.texts.push_back(std::move(text));
            }
            if (reader.pos != reader.end)
                throw std::runtime_error("Trailing EXE editor metadata");
        }
}
void nativeRules(const PeImage &image, const runtime::KinsokuBlock &block, const PeSection &s,
                 ExeDocument &doc) {
    if (doc.sourceEncoding.empty())
        return;
    uint16_t loaded = 0;
    for (size_t pos = block.offset; pos < block.normal;) {
        std::array<uint8_t, 32> buffer{};
        const auto available = std::min<size_t>(buffer.size(), block.normal - pos);
        std::copy_n(image.bytes().data() + s.offset + pos, available, buffer.data());
        hde32s h{};
        hde32_disasm(buffer.data(), &h);
        if (!h.len || h.len > available || h.flags & F_ERROR)
            throw std::runtime_error("Invalid native classifier instruction");
        if (h.opcode == 0xb8)
            loaded = uint16_t(h.imm.imm32);
        std::optional<uint16_t> value;
        if (h.opcode == 0x81 && h.modrm == 0xfe)
            value = h.imm.imm16;
        if (h.opcode == 0x83 && h.modrm == 0xfe)
            value = uint16_t(int16_t(int8_t(h.imm.imm8)));
        if (h.opcode == 0x3b && h.modrm == 0xf0)
            value = loaded;
        if (h.opcode == 0x3b && h.modrm == 0xf3)
            value = 0x2121;
        if (value) {
            const auto jump = pos + h.len;
            auto target = jump;
            const auto *bytes = image.bytes().data() + s.offset;
            if (bytes[jump] == 0x74)
                target = jump + 2 + int8_t(bytes[jump + 1]);
            else if (bytes[jump] == 0x0f && bytes[jump + 1] == 0x84)
                target = jump + 6 + int32_t(number(image.bytes(), s.offset + jump + 2));
            std::string encoded;
            if ((*value & 0xff00) && (*value & 0xff00) != 0xff00)
                encoded += char(*value >> 8);
            encoded += char(*value);
            try {
                const auto decoded = convertEncoding(encoded, doc.sourceEncoding, "UTF-8", true);
                if (wide(decoded).size() == 1) {
                    auto &out = target == block.noStart ? doc.rules.noLineStart : doc.rules.noLineEnd;
                    if ((target == block.noStart || target == block.noEnd) &&
                        out.find(decoded) == std::string::npos)
                        out += decoded;
                }
            } catch (const std::runtime_error &) {
            }
        }
        pos += h.len;
    }
}
} // namespace
const std::vector<std::string> &exeEncodings() {
    static const std::vector<std::string> names{"CP932",  "GBK",    "CP950",  "CP949",  "CP1250",
                                                "CP1251", "CP1252", "CP1253", "CP1254", "CP1255",
                                                "CP1256", "CP1257", "CP1258"};
    return names;
}
ExeRules defaultExeRules(const std::string &enc) {
    charset(enc);
    ExeRules out;
    const std::u16string starts =
        u" 　\"',.!?:;%)]}"
        u"、。，．・：；？！〜～゛゜ー々ゝゞヽヾ〻ぁぃぅぇぉっゃゅょゎゕゖァィゥェォッャュョヮヵヶㇰㇱㇲㇳㇴ"
        u"ㇵㇶㇷㇸㇹㇺㇻㇼㇽㇾㇿ）］｝〉》」』】〕〗〙〛’”»›…‥％‰℃°′″–—-\u00a0™®";
    const std::u16string ends = u"([{（［｛〈《「『【〔〖〘〚‘“«‹$€£¥￥№§";
    auto add = [&](const std::u16string &values, std::string &target) {
        for (auto c : values)
            try {
                const auto s = utf8({c});
                codes(s, enc);
                target += s;
            } catch (const std::runtime_error &) {
            };
    };
    add(starts, out.noLineStart);
    add(ends, out.noLineEnd);
    if (normalizeEncodingName(enc) == "CP1251") {
        const auto opening = utf8(u"“");
        const auto p = out.noLineEnd.find(opening);
        if (p != std::string::npos)
            out.noLineEnd.erase(p, opening.size());
        out.noLineStart += opening;
        out.noLineEnd += utf8(u"„");
    }
    validate(out, enc);
    return out;
}
ExeChanges defaultExeConfiguration(const ExeDocument &doc, const std::string &target) {
    const auto enc = normalizeEncodingName(target);
    charset(enc);
    ExeChanges out;
    out.encoding = enc;
    if (!doc.classifiers.empty())
        out.rules = defaultExeRules(enc);
    // Each row identifies one stock engine prompt, never a game-specific caption.
    static const std::array<const char *, 6> prompts[] = {
        {"確認", "确认", "確認", "확인", "Подтверждение", "Confirmation"},
        {"終了確認", "退出确认", "結束確認", "종료 확인", "Выход", "Confirm exit"},
        {"セーブデータを上書きします。\nよろしいですか？", "将覆盖存档。\n是否继续？",
         "將覆寫存檔。\n是否繼續？", "저장 데이터를 덮어씁니다.\n계속하시겠습니까?",
         "Перезаписать сохранение?", "Overwrite the saved game?"},
        {"セーブデータをロードします。\nよろしいですか？", "将读取存档。\n是否继续？",
         "將讀取存檔。\n是否繼續？", "저장 데이터를 불러옵니다.\n계속하시겠습니까?", "Загрузить сохранение?",
         "Load the saved game?"},
        {"タイトル画面に戻ります。\nよろしいですか？", "将返回标题画面。\n是否继续？",
         "將返回標題畫面。\n是否繼續？", "타이틀 화면으로 돌아갑니다.\n계속하시겠습니까?",
         "Вернуться на титульный экран?", "Return to the title screen?"},
        {"本当にゲームを終了しますか？", "确定退出游戏吗？", "確定結束遊戲嗎？", "게임을 종료하시겠습니까?",
         "Выйти из игры?", "Really quit the game?"},
        {"お知らせ", "提示", "提示", "알림", "Сведения", "Notice"},
        {"このゲームは32Bitカラーモードで最適化されています。\nカラーモードを変更していただくとより一層お楽し"
         "みになれます。",
         "本游戏针对32位色彩模式进行了优化。\n请调整系统色彩模式以获得更好的显示效果。",
         "本遊戲針對32位元色彩模式進行了最佳化。\n請調整系統色彩模式以獲得更好的顯示效果。",
         "이 게임은 32비트 색상 모드에 최적화되어 있습니다.\n더 나은 화면을 위해 색상 모드를 변경해 주세요.",
         "Игра оптимизирована для 32-битного цвета.\nИзмените режим цвета для улучшения изображения.",
         "This game is optimized for 32-bit color.\nChange the display color mode for the best picture."},
        {"コンパイルエラー", "编译错误", "編譯錯誤", "컴파일 오류", "Ошибка компил.", "Compile error"},
        {"スクリプトネストオーバーフロー", "脚本嵌套层数溢出", "指令碼巢狀層數溢位",
         "스크립트 중첩 한도 초과", "Слишком много вложений", "Script nesting overflow"},
        {"スクリプトネストアンダーフロー", "脚本嵌套层数下溢", "指令碼巢狀層數不足", "스크립트 중첩 부족",
         "Недостаточно вложений", "Script nesting underflow"},
        {"レイヤー指定違反", "图层参数错误", "圖層參數錯誤", "잘못된 레이어 지정", "Неверный слой",
         "Invalid layer"},
        {"テキストフォーマットエラー", "文本格式错误", "文字格式錯誤", "텍스트 형식 오류",
         "Ошибка формата текста", "Text format error"},
        {"次へ", "下页", "下頁", "다음", "Ещё", "Next"},
        {"File not found!:%04d", "找不到文件！:%04d", "找不到檔案！:%04d", "파일을 찾을 수 없습니다!:%04d",
         "Файл не найден!:%04d", "File not found!:%04d"},
        {"Compiler not found!", "找不到编译器！", "找不到編譯器！", "컴파일러를 찾을 수 없습니다!",
         "Компилятор не найден!", "Compiler not found!"},
    };
    const auto index = enc == "CP932"    ? 0
                       : enc == "GBK"    ? 1
                       : enc == "CP950"  ? 2
                       : enc == "CP949"  ? 3
                       : enc == "CP1251" ? 4
                                         : 5;
    auto normalize = [](std::string s) {
        s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
        return s;
    };
    for (const auto &item : doc.texts)
        if (item.editable) {
            for (const auto &row : prompts)
                if (std::any_of(row.begin(), row.end(),
                                [&](const auto *v) { return normalize(item.text) == v; })) {
                    convertEncoding(row[index], "UTF-8", enc, true);
                    out.texts[item.id] = row[index];
                    break;
                }
        }
    for (const auto &item : doc.texts)
        if (item.editable && enc != "CP932") {
            if (item.text == "ＭＳ ゴシック" || item.text == "ＭＳ 明朝")
                out.texts[item.id] = enc == "GBK"     ? "SimSun"
                                     : enc == "CP950" ? "PMingLiU"
                                     : enc == "CP949" ? "Batang"
                                                      : "Arial";
        }
    return out;
}
ExeRules readExeRules(const std::filesystem::path &path) {
    if (std::filesystem::file_size(path) > 1024 * 1024)
        throw std::runtime_error("Line-break JSON exceeds 1 MiB");
    const auto bytes = readFile(path);
    const auto rules = runtime::parseKinsoku({reinterpret_cast<const char *>(bytes.data()), bytes.size()});
    return {utf8(rules.noLineStart), utf8(rules.noLineEnd)};
}
ExeDocument inspectExe(const std::vector<uint8_t> &bytes, const std::string &source,
                       const ExeCancel &cancel) {
    poll(cancel);
    if (bytes.size() > maxImage)
        throw std::runtime_error("EXE exceeds the 128 MiB analysis limit");
    const PeImage image(bytes);
    if (image.machine != 0x14c || image.wide || image.dll)
        throw std::runtime_error("EXE editing requires a PE32/x86 application");
    ExeDocument doc;
    doc.bytes = bytes;
    const auto nt = number(bytes, 60), opt = nt + 24, base = number(bytes, opt + 28);
    if (number(bytes, opt + 96 + 4 * 8) || number(bytes, opt + 96 + 4 * 8 + 4)) {
        doc.writable = false;
        doc.notices.push_back("Signed executables cannot be edited");
    }
    std::map<uint32_t, std::string> imports;
    for (const auto &entry : image.imports()) {
        auto dll = entry.dll;
        std::transform(dll.begin(), dll.end(), dll.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (dll == "gdi32.dll" || dll == "user32.dll")
            imports.emplace(base + uint32_t(entry.slot), entry.name);
    }
    if (!base || (base & 0xffff) || uint64_t(base) + image.size > uint64_t(UINT32_MAX) + 1)
        throw std::runtime_error("Invalid PE32 image base or virtual extent");
    if (normalizeEncodingName(source) != "auto") {
        charset(source);
        doc.sourceEncoding = normalizeEncodingName(source);
    }
    readMetadata(image, doc);
    auto analysisBytes = bytes;
    for (const auto &font : doc.fontBridges) {
        const auto at = file(image, font.entry, 6);
        analysisBytes[at] = 0xff;
        analysisBytes[at + 1] = 0x15;
        put(analysisBytes, at + 2, base + font.slot);
    }
    const PeImage analysis(analysisBytes);
    const auto code = instructions(analysis, base, number(bytes, opt + 16), cancel);
    std::set<uint32_t> wrappers;
    for (auto root : code.roots) {
        const auto first = code.items.find(root);
        if (first == code.items.end() || first->second.decoded.opcode != 0x8b ||
            first->second.decoded.modrm != 0x81 || !(first->second.decoded.flags & F_DISP32) ||
            first->second.decoded.disp.disp32 < 0x100 || first->second.decoded.disp.disp32 > 0x1000)
            continue;
        for (auto it = code.items.lower_bound(root); it != code.items.end() && it->first < root + 128; ++it) {
            if (called(it->second, code, imports, base) == "MessageBoxA") {
                // RScript confirmation forwards its two stack arguments, with a fixed type.
                bool a = false, b = false;
                for (auto *prev : before(code, it->first, 30))
                    if (prev->decoded.opcode == 0x8b && prev->decoded.modrm_rm == 4 &&
                        prev->decoded.sib == 0x24 && (prev->decoded.flags & F_DISP8)) {
                        a = a || prev->decoded.disp.disp8 == 4;
                        b = b || prev->decoded.disp.disp8 == 8;
                    }
                if (a && b)
                    wrappers.insert(root);
            }
        }
    }
    std::set<uint32_t> literals;
    std::map<uint32_t, std::set<size_t>> promptReferences;
    std::map<uint32_t, size_t> fixedFields;
    std::map<uint32_t, std::string> roles;
    for (const auto &[address, i] : code.items) {
        poll(cancel);
        const auto api = called(i, code, imports, base);
        if (api == "CreateFontA") {
            ++doc.fontCalls;
            const auto saved = std::find_if(doc.fontBridges.begin(), doc.fontBridges.end(),
                                            [&](const auto &f) { return f.entry == address; });
            if (saved != doc.fontBridges.end())
                doc.charsets.push_back(uint8_t(saved->charset));
            else {
                const auto args = pushes(code, address, 14);
                if (args.size() == 14 && args[5]->decoded.opcode == 0x68 &&
                    args[5]->decoded.imm.imm32 <= 255 &&
                    !encoding(uint8_t(args[5]->decoded.imm.imm32)).empty()) {
                    doc.fontOperands.push_back(args[5]->offset + args[5]->decoded.len - 4);
                    doc.charsets.push_back(uint8_t(args[5]->decoded.imm.imm32));
                } else if (i.decoded.opcode == 0xff && i.decoded.modrm == 0x15) {
                    doc.fontBridges.push_back({address, i.decoded.disp.disp32 - base});
                } else
                    doc.fontsComplete = false;
            }
        }
        size_t count = 0;
        if (api == "MessageBoxA")
            count = 4;
        else if (i.decoded.opcode == 0xe8 &&
                 wrappers.count(uint32_t(address + i.decoded.len + int32_t(i.decoded.imm.imm32))))
            count = 2;
        if (count) {
            const auto args = pushes(code, address, count);
            if (args.size() != count)
                continue;
            const size_t first = count == 4 ? 1 : 0, last = count == 4 ? 3 : 2;
            for (size_t n = first; n < last; ++n) {
                const auto *argument = args[n];
                const auto &h = argument->decoded;
                uint32_t value = 0;
                size_t reference = 0;
                if (h.opcode == 0x68) {
                    value = h.imm.imm32;
                    reference = argument->offset + h.len - 4;
                } else if (h.opcode == 0xff && h.modrm == 0x35 && h.disp.disp32 >= base) {
                    const auto *table = section(image, h.disp.disp32 - base);
                    if (!table || (table->flags & 0x20000000))
                        continue;
                    reference = file(image, h.disp.disp32 - base, 4);
                    value = number(bytes, reference);
                }
                if (value < base || !reference)
                    continue;
                const auto rva = value - base;
                const auto *target = section(image, rva);
                if (target && (!(target->flags & 0x20000000) || target->name == ".ltool")) {
                    literals.insert(rva);
                    promptReferences[rva].insert(reference);
                }
            }
        }
    }
    const auto savedTexts = std::move(doc.texts);
    doc.texts.clear();
    const auto confirmedPrompts = literals;
    // Collect relocation operands only at decoded instruction boundaries. They are used
    // for display candidates; filenames, API names and parser delimiters are not translated.
    std::map<uint32_t, std::set<size_t>> references;
    for (const auto &[address, i] : code.items) {
        const auto &h = i.decoded;
        if ((h.flags & F_IMM32) && !(h.flags & F_RELATIVE) &&
            (h.opcode == 0x68 || (h.opcode >= 0xb8 && h.opcode <= 0xbf) || h.opcode == 0xc7)) {
            const auto value = h.imm.imm32;
            const auto *s = value >= base ? section(image, value - base) : nullptr;
            if (s && (!(s->flags & 0x20000000) || s->name == ".ltool"))
                references[value - base].insert(i.offset + h.len - 4);
        }
    }
    for (const auto &[address, i] : code.items) {
        const auto &h = i.decoded;
        if (!((h.opcode == 0xff && h.modrm == 0x35) ||
              (h.opcode == 0x8b && h.modrm_mod == 0 && h.modrm_rm == 5)))
            continue;
        const auto slot = h.disp.disp32;
        const auto *ss = slot >= base ? section(image, slot - base) : nullptr;
        if (!ss || (ss->flags & 0x20000000))
            continue;
        const auto at = file(image, slot - base, 4);
        const auto value = number(bytes, at);
        const auto *target = value >= base ? section(image, value - base) : nullptr;
        if (target && (!(target->flags & 0x20000000) || target->name == ".ltool"))
            references[value - base].insert(at);
    }
    for (const auto &[rva, refs] : references) {
        const auto raw = literal(image, rva);
        // Decode using the explicit source below; raw candidates only seed non-ASCII text.
        const bool nonAscii = std::any_of(raw.begin(), raw.end(), [](unsigned char c) { return c >= 128; });
        const bool sentence =
            raw.find_first_of(" !?") != std::string::npos && raw.find_first_of("\\/") == std::string::npos &&
            std::count_if(raw.begin(), raw.end(), [](unsigned char c) { return std::isalpha(c); }) >= 2 &&
            std::all_of(raw.begin(), raw.end(),
                        [](unsigned char c) { return (c >= 32 && c < 127) || c == 10 || c == 13; });
        if (raw.size() >= 4 && raw.size() <= 16384 && (nonAscii || sentence)) {
            literals.insert(rva);
            promptReferences[rva].insert(refs.begin(), refs.end());
        }
    }
    for (const auto &saved : savedTexts)
        if (references.count(saved.rva)) {
            const auto raw = literal(image, saved.rva);
            if (raw.empty() || raw.size() >= saved.capacity)
                throw std::runtime_error("Invalid saved EXE text extent");
            const auto start = file(image, saved.rva, saved.capacity);
            if (std::any_of(bytes.begin() + start + raw.size(), bytes.begin() + start + saved.capacity,
                            [](uint8_t c) { return c != 0; }))
                throw std::runtime_error("Saved EXE text padding was modified");
            literals.insert(saved.rva);
            fixedFields[saved.rva] = saved.capacity;
            roles[saved.rva] = saved.role;
        }
    // Display API parameters also include ASCII titles, labels and error messages.
    const std::map<std::string, std::pair<size_t, size_t>> displayApis = {
        {"SetWindowTextA", {2, 0}}, {"SetDlgItemTextA", {3, 0}}, {"CreateWindowExA", {12, 9}},
        {"AppendMenuA", {4, 0}},    {"InsertMenuA", {5, 0}},     {"ModifyMenuA", {5, 0}},
        {"MessageBoxA", {4, 2}}};
    for (const auto &[address, i] : code.items) {
        const auto name = called(i, code, imports, base);
        const auto api = displayApis.find(name);
        if (api == displayApis.end())
            continue;
        const auto args = pushes(code, address, api->second.first);
        if (args.size() != api->second.first)
            continue;
        const auto *arg = args[api->second.second];
        if (arg->decoded.opcode != 0x68 || arg->decoded.imm.imm32 < base)
            continue;
        const auto rva = arg->decoded.imm.imm32 - base;
        if (!references.count(rva))
            continue;
        literals.insert(rva);
        promptReferences[rva].insert(references[rva].begin(), references[rva].end());
        roles[rva] = name == "CreateWindowExA" || name == "SetWindowTextA" ? "Window text" : "Control text";
    }
    // RScript APINI stores the window caption in a 31-byte field at offset six.
    // Confirm the containing block is actually referenced before exposing it.
    for (const auto &[rva, refs] : references) {
        const auto *s = section(image, rva);
        if (!s || uint64_t(rva) + 0x194 > uint64_t(s->rva) + std::min(s->virtualSize, s->rawSize))
            continue;
        const auto at = file(image, rva, 0x194);
        if (std::memcmp(bytes.data() + at, "APINI\0", 6))
            continue;
        const auto caption = literal(image, rva + 6);
        if (caption.empty() || caption.size() >= 31)
            continue;
        literals.insert(rva + 6);
        fixedFields[rva + 6] = 31;
        roles[rva + 6] = "Window title (APINI)";
        doc.notices.push_back("External RsInit configuration can override the embedded window title");
    }
    // Scene tables use three uint16 IDs followed by a fixed 52-byte caption.
    // Require both decoded base and caption LEAs used by the original UI.
    std::set<uint32_t> tableLeas;
    for (const auto &[address, i] : code.items) {
        const auto &h = i.decoded;
        if (h.opcode == 0x8d && h.modrm_mod == 0 && h.modrm_rm == 4 && h.sib_base == 5 && h.sib_scale == 1 &&
            h.disp.disp32 >= base)
            tableLeas.insert(h.disp.disp32 - base);
    }
    for (auto table : tableLeas)
        if (tableLeas.count(table + 6)) {
            const auto *s = section(image, table);
            if (!s || (s->flags & 0x20000000))
                continue;
            std::vector<uint32_t> captions;
            for (size_t n = 0; n < 256; ++n) {
                const auto rva = uint64_t(table) + n * 58;
                if (rva + 58 > uint64_t(s->rva) + std::min(s->rawSize, s->virtualSize))
                    break;
                const auto at = file(image, uint32_t(rva), 58);
                if (!number(bytes, at, 2))
                    break;
                const auto end = std::find(bytes.begin() + at + 6, bytes.begin() + at + 58, 0);
                if (end == bytes.begin() + at + 58 || end == bytes.begin() + at + 6)
                    break;
                captions.push_back(uint32_t(rva + 6));
            }
            if (captions.size() < 2)
                continue;
            for (auto rva : captions) {
                literals.insert(rva);
                fixedFields[rva] = 52;
                roles[rva] = "Scene caption";
            }
        }
    std::sort(doc.charsets.begin(), doc.charsets.end());
    doc.charsets.erase(std::unique(doc.charsets.begin(), doc.charsets.end()), doc.charsets.end());
    if (normalizeEncodingName(source) != "auto") {
        charset(source);
        doc.sourceEncoding = normalizeEncodingName(source);
    } else if (doc.sourceEncoding.empty() && doc.charsets.size() == 1 && doc.fontsComplete &&
               std::none_of(doc.fontBridges.begin(), doc.fontBridges.end(),
                            [](const auto &f) { return !f.target; }))
        doc.sourceEncoding = encoding(doc.charsets[0]);
    doc.notices.push_back(
        "Font charset does not establish the Windows ANSI code page; use a matching system locale");

    if (doc.sourceEncoding.empty())
        doc.notices.push_back("Text source encoding is uncertain; select it explicitly");
    if (!doc.fontsComplete)
        doc.notices.push_back("Some font charset arguments could not be verified");
    for (auto rva : literals) {
        const bool fixed = fixedFields.count(rva);
        if (!fixed)
            promptReferences[rva].insert(references[rva].begin(), references[rva].end());
        ExeText text;
        text.id = id(rva);
        text.rva = rva;
        text.capacity = fixed ? fixedFields[rva] : 0;
        text.role = roles.count(rva) ? roles[rva] : "Embedded text";
        const auto raw = literal(image, rva);
        if (raw.empty())
            continue;
        if (doc.sourceEncoding.empty())
            text.reason = "Select the text source encoding";
        else
            try {
                text.text = convertEncoding(raw, doc.sourceEncoding, "UTF-8", true);
            } catch (const std::runtime_error &) {
                text.reason = "Text does not decode in the selected source encoding";
            }
        if (!fixed && !roles.count(rva) && !confirmedPrompts.count(rva) && text.text.empty())
            continue;
        if (!fixed && !roles.count(rva) && !confirmedPrompts.count(rva) && !text.text.empty()) {
            const auto chars = wide(text.text);
            size_t letters = 0;
            for (auto c : chars)
                if ((c >= 0x3040 && c <= 0x30ff) || (c >= 0x4e00 && c <= 0x9fff) ||
                    (c >= 0x400 && c <= 0x52f) || (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z'))
                    ++letters;
            if (letters < 2 || std::any_of(
                                   chars.begin(), chars.end(),
                                   [](char16_t c) {
                                       return (c < 32 && c != 10 && c != 13 && c != 9) ||
                                              (c >= 0xe000 && c <= 0xf8ff);
                                   }))
                continue;
        }
        text.references.assign(promptReferences[rva].begin(), promptReferences[rva].end());
        const auto value = base + rva;
        // Unclassified references, including interior/suffix pointers, make relocation unsafe.
        for (size_t pos = 0; pos + 4 <= bytes.size(); ++pos) {
            if (!(pos & 0xffff))
                poll(cancel);
            const auto v = number(bytes, pos);
            if (v < value || uint64_t(v) - value >= raw.size() + 1)
                continue;
            if (fixed)
                continue;
            if (v != value ||
                std::find(text.references.begin(), text.references.end(), pos) == text.references.end()) {
                text.reason = "Unverified or interior string reference";
                break;
            }
        }
        if (text.references.empty() && !fixed)
            text.reason = "No verified text reference";
        if (!text.capacity && !confirmedPrompts.count(rva) && !roles.count(rva))
            text.capacity = raw.size() + 1;
        text.editable = text.reason.empty();
        doc.texts.push_back(std::move(text));
    }
    if (doc.classifiers.empty()) {
        auto normalized = bytes;
        normalizeExeKinsoku(normalized);
        for (const auto &s : image.sections)
            if (s.flags & 0x20000000) {
                for (const auto &block : runtime::findKinsokuBlocks(normalized.data() + s.offset,
                                                                    std::min(s.rawSize, s.virtualSize))) {
                    ExeClassifier out{s.rva + uint32_t(block.offset),
                                      s.rva + uint32_t(block.normal),
                                      s.rva + uint32_t(block.noStart),
                                      s.rva + uint32_t(block.noEnd),
                                      {}};
                    out.original.assign(bytes.begin() + s.offset + block.offset,
                                        bytes.begin() + s.offset + block.offset + 5);
                    out.packedPairs = block.packedPairs;
                    doc.classifiers.push_back(std::move(out));
                    nativeRules(image, block, s, doc);
                }
            }
    }
    if (doc.fontOperands.empty() && doc.fontBridges.empty())
        doc.notices.push_back("No verified CreateFontA charset operands");
    if (doc.classifiers.empty())
        doc.notices.push_back("No supported native line-break classifier");
    return doc;
}
ExeDocument inspectExeFile(const std::filesystem::path &path, const std::string &source,
                           const ExeCancel &cancel) {
    poll(cancel);
    if (std::filesystem::file_size(path) > maxImage)
        throw std::runtime_error("EXE exceeds the 128 MiB analysis limit");
    return inspectExe(readFile(path), source, cancel);
}
std::vector<uint8_t> editExe(const ExeDocument &document, const ExeChanges &changes,
                             const ExeCancel &cancel) {
    poll(cancel);
    // Rebuild the analysis instead of trusting caller-editable offsets or capability flags.
    const auto doc = inspectExe(document.bytes,
                                document.sourceEncoding.empty() ? "auto" : document.sourceEncoding, cancel);
    if (!doc.writable)
        throw std::runtime_error("Signed executables cannot be edited");
    const auto enc = changes.encoding.empty() ? doc.sourceEncoding : normalizeEncodingName(changes.encoding);
    if (enc.empty())
        throw std::runtime_error("Select the text source encoding explicitly");
    const auto targetCharset = charset(enc);
    for (const auto &[name, text] : changes.texts) {
        if (std::none_of(doc.texts.begin(), doc.texts.end(),
                         [&](const auto &item) { return item.id == name; }))
            throw std::runtime_error("Unknown EXE text ID: " + name);
        if (text.find('\0') != std::string::npos)
            throw std::runtime_error("EXE text contains NUL");
    }
    if (doc.sourceEncoding.empty())
        throw std::runtime_error("Text source encoding is uncertain");
    const bool convert = enc != doc.sourceEncoding;
    if (!changes.encoding.empty() &&
        ((doc.fontOperands.empty() && doc.fontBridges.empty()) || !doc.fontsComplete))
        throw std::runtime_error("No verified font charset operands to convert");
    std::optional<ExeRules> rules = changes.rules;
    if (convert && !rules)
        rules = defaultExeRules(enc);
    if (rules) {
        validate(*rules, enc);
        if (doc.classifiers.empty())
            throw std::runtime_error("No supported native line-break classifier");
    }
    auto out = doc.bytes;
    const PeImage image(out);
    const auto nt = number(out, 60), opt = nt + 24, base = number(out, opt + 28);
    const auto sectionAlignment = number(out, opt + 32), fileAlignment = number(out, opt + 36);
    if (fileAlignment < 512 || fileAlignment > 65536 || sectionAlignment < fileAlignment)
        throw std::runtime_error("Unsupported PE alignment");
    const auto count = number(out, nt + 6, 2), table = opt + number(out, nt + 20, 2);
    const PeSection *owned = nullptr;
    size_t header = 0;
    for (size_t n = 0; n < image.sections.size(); ++n)
        if (image.sections[n].name == ".ltool") {
            if (owned)
                throw std::runtime_error("Multiple EXE editor sections");
            owned = &image.sections[n];
            header = table + n * 40;
        }
    uint32_t newRva = 0, newOffset = 0;
    if (owned) {
        if (uint64_t(owned->offset) + owned->rawSize != out.size() ||
            owned->rva + align(std::max(owned->rawSize, owned->virtualSize), sectionAlignment) != image.size)
            throw std::runtime_error("EXE editor section is not the final image section");
        newRva = owned->rva;
        newOffset = owned->offset;
    } else {
        if (count == 65535)
            throw std::runtime_error("PE section count cannot be extended");
        header = table + count * 40;
        const auto first = std::min_element(image.sections.begin(), image.sections.end(),
                                            [](const auto &a, const auto &b) { return a.offset < b.offset; });
        if (first == image.sections.end() || header + 40 > number(out, opt + 60) ||
            header + 40 > first->offset)
            throw std::runtime_error("No PE section-header space for EXE editing");
        if (std::any_of(out.begin() + header, out.begin() + header + 40, [](uint8_t b) { return b != 0; }))
            throw std::runtime_error("PE section-header padding is occupied");
        newOffset = align(out.size(), fileAlignment);
        newRva = align(image.size, sectionAlignment);
    }
    std::vector<uint8_t> payload;
    if (!owned)
        payload.resize(24);
    const auto startOffset = owned ? out.size() : size_t(newOffset);
    auto currentRva = [&]() { return uint32_t(uint64_t(newRva) + startOffset - newOffset + payload.size()); };
    bool changed = false;
    if (!changes.encoding.empty())
        for (auto operand : doc.fontOperands) {
            if (number(out, operand) != targetCharset) {
                put(out, operand, targetCharset);
                changed = true;
            }
        }
    std::vector<ExeFontBridge> savedFonts;
    for (auto font : doc.fontBridges) {
        const auto nextCharset = changes.encoding.empty() ? font.charset : targetCharset;
        if (!font.target && changes.encoding.empty())
            continue;
        if (font.target && nextCharset == font.charset) {
            savedFonts.push_back(font);
            continue;
        }
        font.target = currentRva();
        font.charset = nextCharset;
        const auto bridge = fontBridge(font, font.target, nextCharset);
        const auto at = file(image, font.entry, 6);
        out[at] = 0xe8;
        put(out, at + 1, font.target - font.entry - 5);
        out[at + 5] = 0x90;
        payload.insert(payload.end(), bridge.begin(), bridge.end());
        savedFonts.push_back(font);
        changed = true;
    }
    for (const auto &item : doc.texts) {
        poll(cancel);
        const auto t = changes.texts.find(item.id);
        if (t == changes.texts.end() && !convert)
            continue;
        if (!item.editable)
            throw std::runtime_error(item.id + ": " + item.reason);
        const auto text = t == changes.texts.end() ? item.text : t->second;
        if (formats(text) != formats(item.text))
            throw std::runtime_error(item.id + ": format placeholders changed");
        const auto encoded = convertEncoding(text, "UTF-8", enc, true);
        if (encoded == image.string(item.rva))
            continue;
        if (item.capacity) {
            if (encoded.size() >= item.capacity)
                throw std::runtime_error(item.id + ": translation exceeds the fixed field capacity (" +
                                         std::to_string(item.capacity - 1) + " bytes)");
            const auto at = file(image, item.rva, item.capacity);
            std::fill(out.begin() + at, out.begin() + at + item.capacity, 0);
            std::copy(encoded.begin(), encoded.end(), out.begin() + at);
            changed = true;
            continue;
        }
        const auto destination = uint64_t(base) + currentRva();
        if (destination > UINT32_MAX)
            throw std::runtime_error("EXE virtual address overflow");
        for (auto reference : item.references) {
            if (number(out, reference) != base + item.rva)
                throw std::runtime_error("EXE text reference changed");
            put(out, reference, uint32_t(destination));
        }
        payload.insert(payload.end(), encoded.begin(), encoded.end());
        payload.push_back(0);
        changed = true;
    }
    std::vector<std::pair<uint32_t, uint32_t>> codeRanges;
    if (rules &&
        (rules->noLineStart != doc.rules.noLineStart || rules->noLineEnd != doc.rules.noLineEnd || convert)) {
        for (const auto &block : doc.classifiers) {
            poll(cancel);
            const auto destination = currentRva();
            const auto code = classifier(block, *rules, enc, destination);
            const auto pos = file(image, block.entry, 5);
            out[pos] = 0xe9;
            put(out, pos + 1, uint32_t(destination - block.entry - 5));
            payload.insert(payload.end(), code.begin(), code.end());
            codeRanges.push_back({destination, uint32_t(code.size())});
        }
        changed = true;
    }
    if (!changed)
        return doc.bytes;
    const bool writeRules = !codeRanges.empty();
    // Re-emit existing classifier bridges as well, so metadata describes one active generation.
    if (!writeRules && doc.expandedRules) {
        for (const auto &block : doc.classifiers) {
            const auto destination = currentRva();
            const auto code = classifier(block, doc.rules, doc.sourceEncoding, destination);
            const auto pos = file(image, block.entry, 5);
            out[pos] = 0xe9;
            put(out, pos + 1, uint32_t(destination - block.entry - 5));
            payload.insert(payload.end(), code.begin(), code.end());
            codeRanges.push_back({destination, uint32_t(code.size())});
        }
    }
    const auto metaRva = currentRva();
    std::vector<uint8_t> metadata;
    string(metadata, enc);
    const auto savedRules = writeRules ? *rules : doc.rules;
    string(metadata, savedRules.noLineStart);
    string(metadata, savedRules.noLineEnd);
    append(metadata, uint32_t(codeRanges.size()));
    for (size_t n = 0; n < codeRanges.size(); ++n) {
        const auto &b = doc.classifiers[n];
        append(metadata, b.entry);
        append(metadata, b.normal);
        append(metadata, b.noStart);
        append(metadata, b.noEnd);
        append(metadata, uint32_t(b.original.size()));
        metadata.insert(metadata.end(), b.original.begin(), b.original.end());
        append(metadata, b.packedPairs);
        append(metadata, codeRanges[n].first);
        append(metadata, codeRanges[n].second);
    }
    append(metadata, uint32_t(savedFonts.size()));
    for (const auto &font : savedFonts) {
        append(metadata, font.entry);
        append(metadata, font.slot);
        append(metadata, font.target);
        append(metadata, font.charset);
    }
    const auto textCount = std::count_if(doc.texts.begin(), doc.texts.end(), [](const auto &t) {
        return t.role == "Embedded text" && t.capacity;
    });
    append(metadata, uint32_t(textCount));
    for (const auto &text : doc.texts)
        if (text.role == "Embedded text" && text.capacity) {
            append(metadata, text.rva);
            append(metadata, uint32_t(text.capacity));
            string(metadata, text.role);
        }
    payload.insert(payload.end(), metadata.begin(), metadata.end());
    if (uint64_t(startOffset) + payload.size() > maxImage)
        throw std::runtime_error("Edited EXE exceeds the image limit");
    const auto virtualSize = uint32_t(startOffset - newOffset + payload.size());
    const auto rawSize = align(virtualSize, fileAlignment);
    out.resize(uint64_t(newOffset) + rawSize);
    std::copy(payload.begin(), payload.end(), out.begin() + startOffset);
    if (!owned) {
        std::memcpy(out.data() + header, ".ltool", 6);
        put(out, header + 12, newRva);
        put(out, header + 20, newOffset);
        put(out, header + 36, 0x60000020);
        put(out, nt + 6, count + 1, 2);
    }
    put(out, header + 8, virtualSize);
    put(out, header + 16, rawSize);
    const auto codeBytes = uint64_t(number(out, opt + 4)) + rawSize - (owned ? owned->rawSize : 0);
    if (codeBytes > UINT32_MAX)
        throw std::runtime_error("PE code size overflow");
    put(out, opt + 4, uint32_t(codeBytes));
    const auto extent = align(uint64_t(newRva) + std::max(virtualSize, rawSize), sectionAlignment);
    if (uint64_t(base) + extent > uint64_t(UINT32_MAX) + 1)
        throw std::runtime_error("Edited PE32 virtual extent overflow");
    put(out, opt + 56, extent);
    std::memcpy(out.data() + newOffset, "LSTEXE3", 8);
    put(out, newOffset + 8, 3);
    put(out, newOffset + 12, metaRva);
    put(out, newOffset + 16, uint32_t(metadata.size()));
    put(out, opt + 64, 0);
    uint64_t checksum = 0;
    for (size_t pos = 0; pos < out.size(); pos += 2) {
        checksum += out[pos] + (pos + 1 < out.size() ? uint32_t(out[pos + 1]) * 256 : 0);
        checksum = (checksum & 0xffff) + (checksum >> 16);
    }
    checksum = (checksum & 0xffff) + (checksum >> 16);
    put(out, opt + 64, uint32_t(checksum + out.size()));
    poll(cancel);
    inspectExe(out, enc, cancel);
    return out;
}
void saveEditedExe(const std::filesystem::path &source, const std::filesystem::path &output,
                   const ExeDocument &doc, const ExeChanges &changes, const ExeCancel &cancel) {
    namespace fs = std::filesystem;
    if (fs::weakly_canonical(source) == fs::weakly_canonical(output) ||
        (fs::exists(output) && fs::equivalent(source, output)))
        throw std::runtime_error("EXE output must be separate from the input and its aliases");
    if (readFile(source) != doc.bytes)
        throw std::runtime_error("Source EXE changed; inspect it again");
    const auto edited = editExe(doc, changes, cancel);
    poll(cancel);
    if (readFile(source) != doc.bytes)
        throw std::runtime_error("Source EXE changed during editing");
    poll(cancel);
    writePathIfChanged(output, edited);
}
std::string describeExe(const ExeDocument &doc) {
    std::ostringstream s;
    s << "Experimental EXE editor\nFont charsets:";
    for (auto c : doc.charsets)
        s << ' ' << unsigned(c) << '(' << encoding(c) << ')';
    s << "\nVerified font calls: " << doc.fontOperands.size() + doc.fontBridges.size() << "/"
      << doc.fontCalls;
    s << "\nText source encoding: " << (doc.sourceEncoding.empty() ? "unconfirmed" : doc.sourceEncoding)
      << "\n";
    for (const auto &note : doc.notices)
        s << "Notice: " << note << '\n';
    for (const auto &text : doc.texts)
        s << text.id << " [" << (text.editable ? "editable" : text.reason) << "; " << text.role
          << (text.capacity ? "; max " + std::to_string(text.capacity - 1) + " bytes" : "") << "] "
          << std::quoted(text.text) << '\n';
    s << "Classifiers: " << doc.classifiers.size()
      << "\nno_line_start: " << std::quoted(doc.rules.noLineStart)
      << "\nno_line_end: " << std::quoted(doc.rules.noLineEnd) << '\n';
    return s.str();
}
} // namespace liarsoft
