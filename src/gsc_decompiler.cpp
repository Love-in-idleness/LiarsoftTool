#include "gsc_decompiler.h"

#include "encoding.h"
#include "fileio.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace liarsoft {
namespace {

struct Instruction {
    size_t offset;
    uint16_t opcode;
    std::string kinds;
    std::vector<int64_t> operands;
    size_t size;
};

uint16_t readU16(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 2 > data.size()) throw std::runtime_error("unexpected end of GSC");
    return static_cast<uint16_t>(data[offset]) |
           static_cast<uint16_t>(data[offset + 1] << 8);
}

uint32_t readU32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("unexpected end of GSC");
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open: " + path);
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) throw std::runtime_error("Cannot read size: " + path);
    input.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    input.read(reinterpret_cast<char*>(data.data()), size);
    if (input.gcount() != size) throw std::runtime_error("Cannot read: " + path);
    return data;
}

const std::unordered_map<uint16_t, std::string>& modernSchemas() {
    static const auto value = [] {
        auto r = [](char kind, size_t count) { return std::string(count, kind); };
        const std::string E = "E", H = "H", S = "S", D = "D";
        return std::unordered_map<uint16_t, std::string>{
            {3,D},{4,D},{5,D},{8,""},{9,H},{10,""},{11,""},{12,E+D},{13,E},
            {14,H+r('D',11)+r('E',3)},{15,E+D+r('E',10)},{16,E},{17,""},
            {18,E+D},{19,E},{20,r('E',2)},{21,E},{22,r('E',4)},
            {23,r('E',4)},{24,r('E',2)},{25,D+E},{26,""},{27,""},
            {28,r('E',3)},{29,r('E',2)},{30,r('E',6)},
            {32,r('E',5)+D},{33,r('E',5)},{34,r('E',5)},{35,r('E',2)},
            {36,r('E',2)},{37,r('E',2)},{38,r('E',4)},{39,r('E',3)},
            {40,r('E',2)},{41,r('E',2)},{42,r('E',2)},{43,r('E',2)},
            {44,E},{45,r('E',2)},{46,E},{47,r('E',2)},{48,r('E',3)},
            {49,r('E',2)},{50,""},{51,""},{52,""},{53,E},{55,""},
            {56,r('E',5)},{57,""},{58,""},{59,r('E',4)},
            {60,r('E',3)},{61,r('E',2)},{62,r('E',2)},{63,r('E',4)},
            {64,r('E',2)},{65,E},{66,D+r('E',3)},{67,E},{68,""},{69,""},
            {70,r('E',4)},{71,r('E',4)},{72,E},{73,r('E',3)},{74,E},
            {75,r('E',5)},{77,r('E',4)},{80,E},
            {81,E+D+E+E+D+D+E},{82,r('E',4)+D+E},{83,E},{84,E+D},
            {90,r('E',3)},{91,r('E',5)},{92,r('E',2)},{93,r('E',2)},
            {94,E},{95,r('E',2)},{96,r('E',2)},{97,r('E',2)},
            {98,r('E',2)},{99,r('E',3)},{100,r('E',3)},
            {101,r('E',2)},{102,E},{103,r('E',2)},{104,r('E',4)},
            {105,r('E',2)},{106,r('E',5)},{107,r('E',3)},
            {108,r('E',4)},{110,r('E',3)},{111,r('E',3)},{112,E},
            {113,r('E',2)},{114,r('E',2)},{115,r('E',2)},
            {116,r('E',2)},{117,r('E',2)},{120,r('E',2)},{121,E+D},
            {130,r('E',4)},{131,r('E',5)},{132,r('E',2)},
            {134,r('E',3)},{135,r('E',5)},{136,r('E',3)},
            {140,r('E',2)},{141,r('E',2)},{142,r('E',2)},
            {150,E+D},{151,E+D},{152,r('E',2)},{153,r('E',2)},
            {154,r('E',2)},{155,r('E',2)},{156,r('E',3)},
            {157,r('E',5)},{158,r('E',2)},{159,r('E',2)},
            {200,D+r('E',10)},{201,r('E',5)},{202,r('E',3)},
            {210,D+E},{211,D+r('E',3)},{212,D},{213,r('E',3)},
            {220,r('E',3)},{221,r('E',2)},{222,""},{223,r('E',2)},
            {225,r('E',5)},{230,E},{231,E},{255,r('E',5)},
        };
    }();
    return value;
}

const std::unordered_map<uint16_t, std::string>& legacySchemas() {
    static const auto value = [] {
        const std::unordered_map<uint16_t, size_t> counts = {
            {8,0},{9,0},{10,0},{11,0},{12,1},{13,1},{15,1},{16,1},{17,0},
            {18,1},{19,1},{20,2},{21,0},{22,4},{23,2},{24,2},{25,0},
            {26,0},{27,0},{28,3},{29,2},{30,6},{32,5},{33,5},{34,5},
            {35,2},{36,2},{37,2},{38,3},{39,3},{40,2},{41,2},{42,2},
            {43,2},{44,1},{45,2},{46,1},{47,2},{48,2},{49,2},{50,0},
            {51,0},{52,0},{53,1},{55,0},{56,5},{57,0},{58,0},{59,4},
            {60,2},{61,1},{62,1},{63,3},{64,1},{65,1},{66,4},{67,1},
            {68,0},{69,0},{70,4},{71,4},{72,1},{73,2},{74,1},{75,4},
            {77,4},{80,1},{83,1},{90,3},{91,5},{92,2},{93,2},{94,1},
            {95,2},{96,2},{97,2},{98,2},{99,3},{100,3},{101,2},{102,1},
            {103,2},{104,4},{110,3},{111,3},{112,1},{113,2},{120,2},
            {130,4},{131,5},{132,2},{134,3},{135,5},{136,3},{152,2},
            {153,2},{154,2},{155,2},
        };
        std::unordered_map<uint16_t, std::string> result;
        for (const auto& item : counts) result[item.first] = std::string(item.second, 'E');
        result[3]="D"; result[4]="D"; result[5]="D"; result[9]="H";
        result[18]="ED"; result[14]="H" + std::string(11, 'D') + std::string(3, 'E');
        result[25]="D"; result[32]=std::string(5, 'E') + "D";
        result[81]=std::string(4, 'E') + "DDE";
        result[82]=std::string(4, 'E') + "DE";
        result[121]="ED"; result[150]="ED"; result[151]="ED";
        return result;
    }();
    return value;
}

class ParsedGsc {
public:
    explicit ParsedGsc(const std::string& inputPath) : path(inputPath) {
        const auto data = readFile(inputPath);
        if (data.size() < 28) fail("shorter than the GSC header");
        const uint32_t headerLength = readU32(data, 4);
        if (headerLength == 28) {
            legacyDialect = true;
            parseLegacy(data);
        } else if (headerLength == 36) {
            parseModern(data);
        } else {
            fail("unsupported GSC header size " + std::to_string(headerLength));
        }
    }

    std::vector<Instruction> instructions() const {
        std::vector<Instruction> result;
        size_t pos = 0;
        while (pos < code.size()) {
            if (pos + 2 > code.size()) fail("trailing byte at code+" + hex(pos));
            const uint16_t opcode = readU16(code, pos);
            std::string kinds;
            if (opcode & 0xf000) {
                if ((opcode & 0xf000) == 0xf000) {
                    kinds = legacyDialect ? "HH" : "HS";
                } else {
                    kinds = legacyDialect ? "HHH" : "HSS";
                }
            } else {
                const auto& activeSchemas = legacyDialect ? legacySchemas() : modernSchemas();
                const auto found = activeSchemas.find(opcode);
                if (found == activeSchemas.end())
                    fail("unknown opcode 0x" + hex(opcode, 4) + " at code+" + hex(pos));
                kinds = found->second;
            }
            size_t cursor = pos + 2;
            std::vector<int64_t> operands;
            for (char kind : kinds) {
                const size_t size = kind == 'H' || kind == 'S' ? 2 : 4;
                if (cursor + size > code.size())
                    fail("truncated opcode 0x" + hex(opcode, 4) + " at code+" + hex(pos));
                if (kind == 'S') {
                    operands.push_back(static_cast<int16_t>(readU16(code, cursor)));
                } else if (size == 2) {
                    operands.push_back(readU16(code, cursor));
                } else {
                    operands.push_back(readU32(code, cursor));
                }
                cursor += size;
            }
            result.push_back({pos, opcode, kinds, std::move(operands), cursor - pos});
            pos = cursor;
        }
        return result;
    }

    std::set<size_t> jumpTargets() const {
        const auto decoded = instructions();
        std::set<size_t> boundaries;
        for (const auto& instruction : decoded) boundaries.insert(instruction.offset);
        // Early CodeX scripts also jump one-past-the-end to leave a script.
        boundaries.insert(code.size());
        std::set<size_t> targets;
        for (const auto& instruction : decoded) {
            if (instruction.opcode >= 3 && instruction.opcode <= 5)
                targets.insert(static_cast<size_t>(instruction.operands[0]));
        }
        std::vector<size_t> invalid;
        std::set_difference(targets.begin(), targets.end(), boundaries.begin(),
                            boundaries.end(), std::back_inserter(invalid));
        if (!invalid.empty()) fail("jump target is not an instruction: 0x" + hex(invalid[0]));
        return targets;
    }

    std::string string(size_t index, const std::string& encoding) const {
        const auto bytes = stringBytes(index);
        return convertEncoding(std::string(bytes.begin(), bytes.end()), encoding, "UTF-8");
    }

    std::vector<int16_t> dataBlock(size_t index) const {
        if (indexB.size() % 4 || indexC.size() % 2) fail("malformed data-block tables");
        const size_t count = indexB.size() / 4;
        if (index >= count) fail("data block " + std::to_string(index) + " is outside its table");
        const uint32_t offset = readU32(indexB, index * 4);
        const size_t wordCount = indexC.size() / 2;
        if (offset >= wordCount) fail("data block starts outside its table");
        const uint16_t values = readU16(indexC, offset * 2);
        if (static_cast<uint64_t>(offset) + 1 + values > wordCount)
            fail("data block is truncated");
        std::vector<int16_t> result;
        for (size_t i = 0; i < values; ++i)
            result.push_back(static_cast<int16_t>(readU16(indexC, (offset + 1 + i) * 2)));
        return result;
    }

    fs::path path;
    std::vector<uint8_t> code;

private:
    std::vector<uint8_t> indexA;
    std::vector<uint8_t> strings;
    std::vector<uint8_t> indexB;
    std::vector<uint8_t> indexC;
    std::vector<std::vector<uint8_t>> legacyStrings;
    bool legacyDialect = false;

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error(path.string() + ": " + message);
    }

    static std::string hex(uint64_t value, int width = 0) {
        std::ostringstream out;
        out << std::hex << std::nouppercase << std::setfill('0');
        if (width) out << std::setw(width);
        out << value;
        return out.str();
    }

    static std::vector<uint8_t> section(const std::vector<uint8_t>& data,
                                        size_t& pos, size_t size) {
        if (pos > data.size() || size > data.size() - pos)
            throw std::runtime_error("truncated GSC section");
        std::vector<uint8_t> result(data.begin() + static_cast<ptrdiff_t>(pos),
                                    data.begin() + static_cast<ptrdiff_t>(pos + size));
        pos += size;
        return result;
    }

    void parseLegacy(const std::vector<uint8_t>& data) {
        std::vector<uint32_t> header;
        for (size_t i = 0; i < 7; ++i) header.push_back(readU32(data, i * 4));
        const uint64_t endCode = 28ull + header[2];
        const uint64_t endDeclaration = endCode + header[3];
        if (header[3] < 4 || header[3] % 4 || endDeclaration + header[4] > data.size())
            fail("malformed legacy GSC sections");
        size_t pos = 28;
        code = section(data, pos, header[2]);
        const auto declaration = section(data, pos, header[3]);
        const auto stringData = section(data, pos, header[4]);
        for (size_t i = 0; i < declaration.size(); i += 4) {
            const uint32_t start = readU32(declaration, i);
            if (start >= stringData.size()) fail("malformed legacy string offset");
            auto end = std::find(stringData.begin() + start, stringData.end(), 0);
            if (end == stringData.end()) fail("unterminated legacy string");
            legacyStrings.emplace_back(stringData.begin() + start, end);
        }
        if (header[5] % 4 || static_cast<uint64_t>(pos) + header[5] +
                                  header[6] * 2ull > data.size())
            fail("malformed legacy data blocks");
        indexB = section(data, pos, header[5]);
        indexC = section(data, pos, header[6] * 2ull);
    }

    void parseModern(const std::vector<uint8_t>& data) {
        std::vector<uint32_t> header;
        for (size_t i = 0; i < 9; ++i) header.push_back(readU32(data, i * 4));
        const uint64_t expected = 36ull + header[2] + header[3] + header[4] +
                                  header[5] + header[6] * 2ull +
                                  header[7] * 2ull + header[8];
        if (header[0] != data.size() || expected != data.size())
            fail("invalid modern GSC header");
        size_t pos = 36;
        code = section(data, pos, header[2]);
        indexA = section(data, pos, header[3]);
        strings = section(data, pos, header[4]);
        indexB = section(data, pos, header[5]);
        indexC = section(data, pos, header[6] * 2ull);
        section(data, pos, header[7]); // offsets
        section(data, pos, header[7]); // lines
        section(data, pos, header[8]); // names
        validateStringOffsets();
    }

    void validateStringOffsets() const {
        if (indexA.size() % 4) fail("string-offset table has odd size");
        for (size_t i = 0; i < indexA.size(); i += 4) {
            const uint32_t offset = readU32(indexA, i);
            if (offset >= strings.size()) fail("string starts outside the string table");
            if (std::find(strings.begin() + offset, strings.end(), 0) == strings.end())
                fail("string is not terminated");
        }
    }

    std::vector<uint8_t> stringBytes(size_t index) const {
        if (!legacyStrings.empty()) {
            if (index >= legacyStrings.size()) fail("legacy string index is outside its table");
            return legacyStrings[index];
        }
        const size_t count = indexA.size() / 4;
        if (index >= count) fail("string index is outside its table");
        const uint32_t start = readU32(indexA, index * 4);
        const auto end = std::find(strings.begin() + start, strings.end(), 0);
        return std::vector<uint8_t>(strings.begin() + start, end);
    }
};

const std::unordered_map<uint16_t, std::string> NAMES = {
    {8,"end"},{9,"rnd"},{10,"hit"},{11,"hitc"},{12,"jump"},{13,"wait"},
    {14,"select"},{15,"gosub"},{16,"return"},{17,"save"},{20,"gload"},
    {21,"gcls"},{22,"gmove"},{23,"quake"},{24,"flash"},{26,"queue"},
    {27,"action"},{28,"update"},{29,"zupdate"},{30,"load"},{33,"move"},
    {34,"movi"},{36,"cls"},{37,"enabl"},{38,"locmode"},{39,"draw"},
    {40,"depth"},{41,"group"},{42,"tbox"},{43,"effect"},
    {44,"effectdep"},{45,"tone"},{46,"tonedep"},{47,"locgrid"},
    {49,"mode"},{50,"backup"},{52,"stop"},{55,"makesave"},
    {56,"sysmode"},{57,"logflash"},{60,"bgm_on"},{61,"bgm_off"},
    {62,"se"},{63,"se_on"},{64,"se_off"},{65,"movie"},{66,"voice"},
    {67,"voice_off"},{68,"se_wait"},{69,"voice_wait"},{70,"setclk"},
    {71,"setclksys"},{72,"resetclk"},{73,"click"},{74,"autoreset"},
    {75,"setlink"},{80,"TCL"},{81,"TXT"},{82,"TXA"},{90,"tboxloc"},
    {91,"texloc"},{92,"tboxback"},{93,"cmploc"},{94,"tboxcmp"},
    {95,"texcolor"},{96,"texsize"},{97,"texfont"},{98,"texmode"},
    {100,"waitloc"},{103,"waitlod"},{104,"waitcol"},{106,"namloc"},
    {107,"texpich"},{108,"texruby"},{121,"folder"},{132,"numenable"},
    {200,"insub"},{225,"locmap"},
};

const std::unordered_map<uint16_t, std::string> VM_OPERATORS = {
    {1,"="},{2,"||"},{3,"&&"},{4,"=="},{5,">="},{6,">"},{7,"<="},
    {8,"<"},{9,"!="},{10,"+"},{11,"-"},{12,"*"},{13,"/"},
    {14,"%"},{15,"mov"},
};

std::string number(int64_t value) { return std::to_string(value); }

std::string expression(uint32_t value) {
    const uint32_t depth = value / 0x10000;
    const uint16_t raw = static_cast<uint16_t>(value);
    const int32_t shown = depth ? raw : static_cast<int16_t>(raw);
    return std::string(depth, '@') + std::to_string(shown);
}

std::string vmSource(uint16_t opcode, int64_t operand, bool left,
                     const std::unordered_map<int64_t, std::string>* temporaries = nullptr) {
    const int shift = left ? 10 : 8;
    const int mode = (opcode >> shift) & 3;
    if (mode == 0) return number(operand);
    if (mode == 1) {
        if (temporaries) {
            const auto found = temporaries->find(operand);
            if (found != temporaries->end()) return found->second;
        }
        return "tmp" + number(operand);
    }
    if (mode == 2) {
        const int depth = ((left ? opcode >> 4 : opcode) & 0xf) + 1;
        return std::string(depth, '@') + number(operand);
    }
    return "<bad-mode:" + std::to_string(mode) + ":" + number(operand) + ">";
}

std::string vmText(const Instruction& instruction) {
    const uint16_t family = instruction.opcode >> 12;
    if (family == 0xf)
        return "tmp" + number(instruction.operands[0]) + " = " +
               vmSource(instruction.opcode, instruction.operands[1], false);
    return "tmp" + number(instruction.operands[0]) + " = (" +
           vmSource(instruction.opcode, instruction.operands[1], true) + " " +
           VM_OPERATORS.at(family) + " " +
           vmSource(instruction.opcode, instruction.operands[2], false) + ")";
}

std::pair<std::vector<std::string>, std::optional<std::string>>
liftVm(const std::vector<Instruction>& instructions) {
    std::unordered_map<int64_t, std::string> temporaries;
    std::vector<std::string> statements;
    for (const auto& instruction : instructions) {
        const uint16_t family = instruction.opcode >> 12;
        if (family == 0xf) {
            temporaries[instruction.operands[0]] = vmSource(
                instruction.opcode, instruction.operands[1], false, &temporaries);
            continue;
        }
        const auto lhs = vmSource(instruction.opcode, instruction.operands[1], true,
                                  &temporaries);
        const auto rhs = vmSource(instruction.opcode, instruction.operands[2], false,
                                  &temporaries);
        if (family == 1) {
            statements.push_back("!" + lhs + " = " + rhs);
            temporaries[instruction.operands[0]] = rhs;
        } else {
            temporaries[instruction.operands[0]] =
                "(" + lhs + " " + VM_OPERATORS.at(family) + " " + rhs + ")";
        }
    }
    const auto found = temporaries.find(0);
    return {statements, found == temporaries.end()
                            ? std::optional<std::string>()
                            : std::optional<std::string>(found->second)};
}

std::string hex6(size_t value) {
    std::ostringstream out;
    out << std::hex << std::nouppercase << std::setw(6) << std::setfill('0') << value;
    return out.str();
}

std::string commandArgs(const Instruction& instruction) {
    std::ostringstream out;
    for (size_t i = 0; i < instruction.operands.size(); ++i) {
        if (i) out << ' ';
        if (instruction.kinds[i] == 'E')
            out << expression(static_cast<uint32_t>(instruction.operands[i]));
        else
            out << instruction.operands[i];
    }
    return out.str();
}

std::string textCommand(const ParsedGsc& gsc, const Instruction& instruction,
                        const std::string& encoding) {
    const auto& args = instruction.operands;
    if (instruction.opcode == 81) {
        std::ostringstream out;
        if (args[1]) out << "*voice " << expression(static_cast<uint32_t>(args[1])) << '\n';
        const auto name = gsc.string(static_cast<size_t>(args[4]), encoding);
        const auto text = gsc.string(static_cast<size_t>(args[5]), encoding);
        out << '\\';
        if (!name.empty()) out << name << "\"：\"";
        out << text;
        return out.str();
    }
    return "\\append " + gsc.string(static_cast<size_t>(args[4]), encoding);
}

std::string instructionText(const ParsedGsc& gsc, const Instruction& instruction,
                            const std::string& encoding) {
    const uint16_t opcode = instruction.opcode;
    if (opcode & 0xf000) return "; vm " + vmText(instruction);
    if (opcode >= 3 && opcode <= 5) {
        const auto target = static_cast<size_t>(instruction.operands[0]);
        if (opcode == 5) return "*goto L_" + hex6(target);
        return std::string("; ") + (opcode == 3 ? "jz" : "jnz") +
               " L_" + hex6(target) + " (tmp0)";
    }
    if (opcode == 81 || opcode == 82) return textCommand(gsc, instruction, encoding);
    if (opcode == 26) return "=";
    if (opcode == 18) {
        std::ostringstream out;
        out << "; data " << expression(static_cast<uint32_t>(instruction.operands[0]));
        for (const auto value : gsc.dataBlock(static_cast<size_t>(instruction.operands[1])))
            out << ' ' << value;
        return out.str();
    }
    const auto found = NAMES.find(opcode);
    if (found == NAMES.end()) return "; opcode " + std::to_string(opcode) + " " + commandArgs(instruction);
    const auto args = commandArgs(instruction);
    return "*" + found->second + (args.empty() ? "" : " " + args);
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) lines.push_back(line);
    if (lines.empty()) lines.push_back("");
    return lines;
}

} // namespace

std::string decompileGsc(const std::string& inputPath, const std::string& encoding) {
    const ParsedGsc gsc(inputPath);
    const auto instructions = gsc.instructions();
    const auto labels = gsc.jumpTargets();
    std::vector<std::string> lines = {
        "; generated from " + gsc.path.filename().string(),
        "; offsets are byte offsets in the GSC code section",
    };
    size_t index = 0;
    while (index < instructions.size()) {
        const auto& instruction = instructions[index];
        if (labels.count(instruction.offset)) {
            lines.push_back("");
            lines.push_back(":L_" + hex6(instruction.offset));
        }
        if (instruction.opcode & 0xf000) {
            size_t end = index;
            while (end < instructions.size() &&
                   (instructions[end].opcode & 0xf000) &&
                   (end == index || !labels.count(instructions[end].offset)))
                ++end;
            std::vector<Instruction> block(instructions.begin() + index,
                                           instructions.begin() + end);
            auto lifted = liftVm(block);
            const std::string sourceRange = "@" + hex6(block.front().offset) +
                ".." + hex6(block.back().offset + block.back().size);
            for (const auto& statement : lifted.first)
                lines.push_back(statement + " ; " + sourceRange);
            if (end < instructions.size() &&
                (instructions[end].opcode == 3 || instructions[end].opcode == 4) &&
                lifted.second) {
                const auto& branch = instructions[end];
                lines.push_back("*if (" + *lifted.second + ") " +
                                (branch.opcode == 3 ? "==" : "!=") +
                                " 0 ; " + sourceRange);
                lines.push_back("*goto L_" +
                                hex6(static_cast<size_t>(branch.operands[0])) +
                                " ; @" + hex6(branch.offset));
                lines.push_back("*endif");
                index = end + 1;
                continue;
            }
            if (lifted.second && lifted.first.empty())
                lines.push_back("; unused expression " + *lifted.second +
                                " ; " + sourceRange);
            index = end;
            continue;
        }
        const auto rendered = instructionText(gsc, instruction, encoding);
        for (const auto& line : splitLines(rendered)) {
            if (!line.empty() && line.front() == '\\') {
                lines.push_back("; @" + hex6(instruction.offset));
                lines.push_back(line);
            } else {
                lines.push_back(line + " ; @" + hex6(instruction.offset));
            }
        }
        ++index;
    }
    if (labels.count(gsc.code.size())) {
        lines.push_back("");
        lines.push_back(":L_" + hex6(gsc.code.size()));
    }
    std::ostringstream output;
    for (const auto& line : lines) output << line << '\n';
    return output.str();
}

void decompileGscToFile(const std::string& inputPath,
                        const std::string& outputPath,
                        const std::string& encoding) {
    const auto output = decompileGsc(inputPath, encoding);
    writeFileIfChanged(outputPath,
        reinterpret_cast<const uint8_t*>(output.data()), output.size());
}

} // namespace liarsoft
