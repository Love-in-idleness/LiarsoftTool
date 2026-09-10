#include "gsc_decompiler.h"

#include "encoding.h"
#include "fileio.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <limits>
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

constexpr const char* RAW_HEADER = ";@gsc-raw-v1 ";
constexpr const char* RAW_CHUNK = ";@gsc-raw ";
constexpr const char* RAW_END = ";@gsc-raw-end";
constexpr const char* BYTE_FORMAT = ";@gsc-byte-format ";
constexpr const char* TEXT_ENCODING = ";@gsc-text-encoding ";
constexpr const char* INSTRUCTION_SCHEMA = ";@gsc-schema ";

enum class InstructionSchema { PreCodeX, Early, RScript18, RScript19, Modern };

uint64_t fnv1a64(const std::vector<uint8_t>& data) {
    uint64_t value = 14695981039346656037ull;
    for (const auto byte : data) {
        value ^= byte;
        value *= 1099511628211ull;
    }
    return value;
}

std::string hex64(uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::nouppercase << std::setw(16) << std::setfill('0')
        << value;
    return out.str();
}

std::string rawEnvelope(const std::vector<uint8_t>& data) {
    static constexpr char HEX[] = "0123456789abcdef";
    std::ostringstream out;
    out << RAW_HEADER << "size=" << data.size()
        << " fnv1a64=" << hex64(fnv1a64(data)) << '\n';
    for (size_t offset = 0; offset < data.size(); offset += 48) {
        out << RAW_CHUNK;
        const size_t end = std::min(data.size(), offset + 48);
        for (size_t i = offset; i < end; ++i)
            out << HEX[data[i] >> 4] << HEX[data[i] & 0x0f];
        out << '\n';
    }
    out << RAW_END << '\n';
    return out.str();
}

uint8_t hexDigit(char value) {
    if (value >= '0' && value <= '9') return static_cast<uint8_t>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<uint8_t>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<uint8_t>(value - 'A' + 10);
    throw std::runtime_error("invalid hexadecimal digit in GSC metadata");
}

void writeU32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    if (offset + 4 > data.size())
        throw std::runtime_error("cannot patch truncated GSC header");
    data[offset] = static_cast<uint8_t>(value);
    data[offset + 1] = static_cast<uint8_t>(value >> 8);
    data[offset + 2] = static_cast<uint8_t>(value >> 16);
    data[offset + 3] = static_cast<uint8_t>(value >> 24);
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
        auto result = modernSchemas();
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

const std::unordered_map<uint16_t, std::string>& preCodeXSchemas() {
    static const auto value = [] {
        auto result = legacySchemas();
        result[14] = "H" + std::string(11, 'D');
        result[81] = std::string(4, 'E') + "DD";
        return result;
    }();
    return value;
}

const std::unordered_map<uint16_t, std::string>& rscript19Schemas() {
    static const auto value = [] {
        auto result = modernSchemas();
        result[48] = std::string(2, 'E');
        result[62] = "E";
        result[63] = std::string(3, 'E');
        result[64] = "E";
        result[105] = "E";
        return result;
    }();
    return value;
}

const std::unordered_map<uint16_t, std::string>& rscript18Schemas() {
    static const auto value = [] {
        auto result = rscript19Schemas();
        result[38] = std::string(3, 'E');
        return result;
    }();
    return value;
}

const std::unordered_map<uint16_t, std::string>& schemasFor(
    InstructionSchema schema) {
    if (schema == InstructionSchema::PreCodeX) return preCodeXSchemas();
    if (schema == InstructionSchema::Early) return legacySchemas();
    if (schema == InstructionSchema::RScript18) return rscript18Schemas();
    if (schema == InstructionSchema::RScript19) return rscript19Schemas();
    return modernSchemas();
}

class ParsedGsc {
public:
    explicit ParsedGsc(const std::string& inputPath) : path(inputPath) {
        const auto data = readFile(inputPath);
        if (data.size() < 28) fail("shorter than the GSC header");
        const uint32_t headerLength = readU32(data, 4);
        if (headerLength == 28) {
            earlyVmEncoding = true;
            parseLegacy(data);
            selectSchema({InstructionSchema::Early, InstructionSchema::PreCodeX,
                          InstructionSchema::RScript18,
                          InstructionSchema::RScript19, InstructionSchema::Modern});
        } else if (headerLength == 36) {
            parseModern(data);
            selectSchema({InstructionSchema::Modern, InstructionSchema::RScript19,
                          InstructionSchema::RScript18, InstructionSchema::Early,
                          InstructionSchema::PreCodeX});
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
                    kinds = earlyVmEncoding ? "HH" : "HS";
                } else {
                    kinds = earlyVmEncoding ? "HHH" : "HSS";
                }
            } else {
                const auto& activeSchemas = schemasFor(instructionSchema);
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
            else if (instruction.opcode == 14)
                for (size_t i = 2; i <= 6; ++i)
                    targets.insert(static_cast<size_t>(instruction.operands[i]));
        }
        std::vector<size_t> invalid;
        std::set_difference(targets.begin(), targets.end(), boundaries.begin(),
                            boundaries.end(), std::back_inserter(invalid));
        if (!invalid.empty()) fail("jump target is not an instruction: 0x" + hex(invalid[0]));
        return targets;
    }

    uint64_t operandPenalty() const {
        uint64_t result = 0;
        for (const auto& instruction : instructions()) {
            for (size_t i = 0; i < instruction.kinds.size(); ++i) {
                if (instruction.kinds[i] == 'E')
                    result += static_cast<uint32_t>(instruction.operands[i]) >> 16;
            }
        }
        return result;
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

    size_t dataBlockCount() const { return indexB.size() / 4; }

    fs::path path;
    std::vector<uint8_t> code;

    InstructionSchema schema() const { return instructionSchema; }
    size_t headerSize() const { return earlyVmEncoding ? 28 : 36; }

private:
    std::vector<uint8_t> indexA;
    std::vector<uint8_t> strings;
    std::vector<uint8_t> indexB;
    std::vector<uint8_t> indexC;
    std::vector<std::vector<uint8_t>> legacyStrings;
    InstructionSchema instructionSchema = InstructionSchema::Modern;
    bool earlyVmEncoding = false;

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

    void validateReferences() const {
        for (const auto& instruction : instructions()) {
            if (instruction.opcode == 14) {
                stringBytes(static_cast<size_t>(instruction.operands[1]));
                for (size_t i = 7; i <= 11; ++i)
                    stringBytes(static_cast<size_t>(instruction.operands[i]));
            } else if (instruction.opcode == 32) {
                stringBytes(static_cast<size_t>(instruction.operands[5]));
            } else if (instruction.opcode == 81) {
                stringBytes(static_cast<size_t>(instruction.operands[4]));
                stringBytes(static_cast<size_t>(instruction.operands[5]));
            } else if (instruction.opcode == 82) {
                stringBytes(static_cast<size_t>(instruction.operands[4]));
            } else if (instruction.opcode == 121 || instruction.opcode == 150 ||
                       instruction.opcode == 151) {
                stringBytes(static_cast<size_t>(instruction.operands[1]));
            }
        }
    }

    void selectSchema(std::initializer_list<InstructionSchema> candidates) {
        std::optional<uint64_t> bestPenalty;
        InstructionSchema bestSchema = *candidates.begin();
        std::string firstFailure;
        for (const auto candidate : candidates) {
            instructionSchema = candidate;
            try {
                jumpTargets();
                validateReferences();
                const auto penalty = operandPenalty();
                if (!bestPenalty || penalty < *bestPenalty) {
                    bestPenalty = penalty;
                    bestSchema = candidate;
                }
            } catch (const std::runtime_error& error) {
                if (firstFailure.empty()) firstFailure = error.what();
            }
        }
        if (!bestPenalty) throw std::runtime_error(firstFailure);
        instructionSchema = bestSchema;
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
        const uint64_t coreEnd = 36ull + header[2] + header[3] + header[4] +
                                 header[5] + header[6] * 2ull;
        if (header[0] > data.size() || coreEnd > header[0])
            fail("invalid modern GSC header");
        size_t pos = 36;
        code = section(data, pos, header[2]);
        indexA = section(data, pos, header[3]);
        strings = section(data, pos, header[4]);
        indexB = section(data, pos, header[5]);
        indexC = section(data, pos, header[6] * 2ull);
        section(data, pos, header[0] - pos); // optional debug data
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
    {14,"select"},{15,"gosub"},{16,"return"},{17,"save"},{18,"data"},{19,"subscript"},
    {20,"gload"},
    {21,"gcls"},{22,"gmove"},{23,"quake"},{24,"flash"},{26,"queue"},
    {25,"call"},{27,"action"},{28,"update"},{29,"zupdate"},{30,"load"},
    {32,"font"},{33,"move"},{34,"movi"},{35,"phase"},{36,"cls"},
    {37,"enabl"},{38,"locmode"},{39,"draw"},
    {40,"depth"},{41,"group"},{42,"tbox"},{43,"effect"},
    {44,"effectdep"},{45,"tone"},{46,"tonedep"},{47,"locgrid"},
    {48,"face"},{49,"mode"},{50,"backup"},{51,"resume"},{52,"stop"},
    {53,"autobackup"},{55,"makesave"},{56,"sysmode"},{57,"logflash"},
    {58,"endscene"},{59,"swap"},{60,"bgm_on"},{61,"bgm_off"},
    {62,"se"},{63,"se_on"},{64,"se_off"},{65,"movie"},{66,"voice"},
    {67,"voice_off"},{68,"se_wait"},{69,"voice_wait"},{70,"setclk"},
    {71,"setclksys"},{72,"resetclk"},{73,"click"},{74,"autoreset"},
    {75,"setlink"},{77,"setclksub"},{80,"TCL"},{81,"TXT"},{82,"TXA"},
    {83,"txcls"},{90,"tboxloc"},
    {91,"texloc"},{92,"tboxback"},{93,"cmploc"},{94,"tboxcmp"},
    {95,"texcolor"},{96,"texsize"},{97,"texfont"},{98,"texmode"},
    {99,"texindent"},{100,"waitloc"},{101,"faceloc"},{102,"tclsmode"},
    {103,"waitlod"},{104,"waitcol"},{105,"facedep"},{106,"namloc"},
    {107,"texpich"},{108,"texruby"},{110,"getloc"},{111,"muldev"},
    {112,"root"},{113,"pow"},{114,"getflag"},{115,"menuon"},{116,"menuset"},
    {117,"menuget"},{120,"fontsize"},{121,"folder"},{130,"numload"},
    {131,"numreng"},{132,"numenable"},{134,"numloc"},{135,"numset"},
    {136,"num"},{140,"gmenuon"},{141,"gmenuset"},{142,"gmenuget"},
    {150,"strset"},{151,"stradd"},{152,"numstr"},{153,"strnum"},
    {154,"strcpy"},{155,"strcat"},{156,"strinput"},{157,"strinprop"},
    {158,"strsave"},{159,"strload"},{200,"insub"},{201,"metamor"},
    {202,"flagset"},{210,"dynsel"},{211,"dynans"},{212,"dynnext"},
    {213,"dyndo"},{220,"map"},{221,"mapload"},{222,"mapcls"},{223,"mapobj"},
    {225,"locmap"},{230,"bganim"},{231,"await"},{255,"excmd"},
};

std::string expression(uint32_t value) {
    const uint32_t depth = value / 0x10000;
    const uint16_t raw = static_cast<uint16_t>(value);
    const int32_t shown = depth ? raw : static_cast<int16_t>(raw);
    return std::string(depth, '@') + std::to_string(shown);
}

std::string hex6(size_t value) {
    std::ostringstream out;
    out << std::hex << std::nouppercase << std::setw(6) << std::setfill('0') << value;
    return out.str();
}

struct SourceToken {
    std::string value;
    bool quoted = false;
};

std::string quoteString(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default: out << static_cast<char>(ch); break;
        }
    }
    out << '"';
    return out.str();
}

std::vector<SourceToken> tokenizeSource(const std::string& line, size_t lineNo) {
    std::vector<SourceToken> result;
    size_t pos = 0;
    while (pos < line.size()) {
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
            ++pos;
        if (pos == line.size() || line[pos] == ';') break;
        if (line[pos] != '"') {
            const size_t begin = pos;
            while (pos < line.size() &&
                   !std::isspace(static_cast<unsigned char>(line[pos])) &&
                   line[pos] != ';')
                ++pos;
            result.push_back({line.substr(begin, pos - begin), false});
            continue;
        }
        ++pos;
        std::string value;
        bool closed = false;
        while (pos < line.size()) {
            const char ch = line[pos++];
            if (ch == '"') {
                closed = true;
                break;
            }
            if (ch != '\\') {
                value.push_back(ch);
                continue;
            }
            if (pos == line.size())
                throw std::runtime_error("line " + std::to_string(lineNo) +
                                         ": incomplete string escape");
            const char escaped = line[pos++];
            if (escaped == 'n') value.push_back('\n');
            else if (escaped == 'r') value.push_back('\r');
            else if (escaped == 't') value.push_back('\t');
            else if (escaped == '\\' || escaped == '"') value.push_back(escaped);
            else throw std::runtime_error("line " + std::to_string(lineNo) +
                                          ": unsupported string escape");
        }
        if (!closed)
            throw std::runtime_error("line " + std::to_string(lineNo) +
                                     ": unterminated string");
        result.push_back({std::move(value), true});
    }
    return result;
}

uint32_t parseOperand(const std::string& token, char kind, size_t lineNo) {
    size_t depth = 0;
    if (kind == 'E') while (depth < token.size() && token[depth] == '@') ++depth;
    if (depth > 0xffff)
        throw std::runtime_error("line " + std::to_string(lineNo) +
                                 ": expression indirection is too deep");
    const auto digits = token.substr(depth);
    if (digits.empty())
        throw std::runtime_error("line " + std::to_string(lineNo) +
                                 ": missing numeric operand");
    size_t used = 0;
    int64_t value;
    try {
        value = std::stoll(digits, &used, 0);
    } catch (const std::exception&) {
        throw std::runtime_error("line " + std::to_string(lineNo) +
                                 ": invalid numeric operand: " + token);
    }
    if (used != digits.size())
        throw std::runtime_error("line " + std::to_string(lineNo) +
                                 ": invalid numeric operand: " + token);
    if (kind == 'E') {
        if ((!depth && (value < -32768 || value > 32767)) ||
            (depth && (value < 0 || value > 65535)))
            throw std::runtime_error("line " + std::to_string(lineNo) +
                                     ": expression operand is out of range");
        return static_cast<uint32_t>(depth * 0x10000ull |
                                     static_cast<uint16_t>(value));
    }
    if (kind == 'S') {
        if (value < -32768 || value > 32767)
            throw std::runtime_error("line " + std::to_string(lineNo) +
                                     ": signed operand is out of range");
        return static_cast<uint16_t>(value);
    }
    const uint64_t maximum = kind == 'H' ? 0xffffull : 0xffffffffull;
    if (value < 0 || static_cast<uint64_t>(value) > maximum)
        throw std::runtime_error("line " + std::to_string(lineNo) +
                                 ": unsigned operand is out of range");
    return static_cast<uint32_t>(value);
}

std::string schemaName(InstructionSchema schema) {
    if (schema == InstructionSchema::PreCodeX) return "pre-codex";
    if (schema == InstructionSchema::Early) return "early";
    if (schema == InstructionSchema::RScript18) return "rscript18";
    if (schema == InstructionSchema::RScript19) return "rscript19";
    return "modern";
}

InstructionSchema parseSchemaName(const std::string& value) {
    if (value == "pre-codex") return InstructionSchema::PreCodeX;
    if (value == "early") return InstructionSchema::Early;
    if (value == "rscript18") return InstructionSchema::RScript18;
    if (value == "rscript19") return InstructionSchema::RScript19;
    if (value == "modern") return InstructionSchema::Modern;
    throw std::runtime_error("unsupported GSC instruction schema: " + value);
}

bool isStringOperand(uint16_t opcode, size_t index) {
    if (opcode == 14) return index == 1 || (index >= 7 && index <= 11);
    if (opcode == 32) return index == 5;
    if (opcode == 81) return index == 4 || index == 5;
    if (opcode == 82) return index == 4;
    return (opcode == 121 || opcode == 150 || opcode == 151) && index == 1;
}

bool isCodeTarget(uint16_t opcode, size_t index) {
    if (opcode >= 3 && opcode <= 5) return index == 0;
    return opcode == 14 && index >= 2 && index <= 6;
}

std::string sourceInstruction(const ParsedGsc& gsc,
                              const Instruction& instruction,
                              const std::string& encoding) {
    std::ostringstream out;
    if (instruction.opcode & 0xf000) {
        out << "*vm 0x" << std::hex << std::nouppercase
            << std::setw(4) << std::setfill('0') << instruction.opcode << std::dec;
    } else if (instruction.opcode == 3) out << "*jz";
    else if (instruction.opcode == 4) out << "*jnz";
    else if (instruction.opcode == 5) out << "*goto";
    else {
        const auto named = NAMES.find(instruction.opcode);
        if (named != NAMES.end()) out << '*' << named->second;
        else out << "*opcode " << instruction.opcode;
    }
    for (size_t i = 0; i < instruction.operands.size(); ++i) {
        out << ' ';
        if (isStringOperand(instruction.opcode, i))
            out << quoteString(gsc.string(static_cast<size_t>(instruction.operands[i]),
                                          encoding));
        else if (isCodeTarget(instruction.opcode, i))
            out << "L_" << hex6(static_cast<size_t>(instruction.operands[i]));
        else if (instruction.kinds[i] == 'E')
            out << expression(static_cast<uint32_t>(instruction.operands[i]));
        else
            out << instruction.operands[i];
    }
    return out.str();
}

struct SourceInstruction {
    uint16_t opcode = 0;
    std::string kinds;
    std::vector<SourceToken> operands;
    size_t lineNo = 0;
    size_t offset = 0;
};

struct SourceDataBlock {
    std::vector<int16_t> values;
    size_t lineNo = 0;
};

size_t instructionSize(const std::string& kinds) {
    size_t size = 2;
    for (const char kind : kinds) size += kind == 'H' || kind == 'S' ? 2 : 4;
    return size;
}

std::vector<uint8_t> compileStructuredTsc(const std::string& tscText,
                                          const std::string& fallbackEncoding) {
    size_t headerSize = 0;
    std::string encoding = fallbackEncoding;
    std::optional<InstructionSchema> selectedSchema;
    std::vector<SourceInstruction> instructions;
    std::vector<SourceDataBlock> dataBlocks;
    std::unordered_map<std::string, size_t> labels;
    std::unordered_map<std::string, uint16_t> opcodes;
    for (const auto& item : NAMES) opcodes[item.second] = item.first;

    std::istringstream input(tscText);
    std::string line;
    size_t lineNo = 0;
    size_t offset = 0;
    while (std::getline(input, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.compare(0, std::char_traits<char>::length(BYTE_FORMAT), BYTE_FORMAT) == 0) {
            if (headerSize) throw std::runtime_error("duplicate ;@gsc-byte-format metadata");
            const auto value = line.substr(std::char_traits<char>::length(BYTE_FORMAT));
            if (value == "legacy-28") headerSize = 28;
            else if (value == "modern-36") headerSize = 36;
            else throw std::runtime_error("unsupported GSC byte format: " + value);
            continue;
        }
        if (line.compare(0, std::char_traits<char>::length(TEXT_ENCODING), TEXT_ENCODING) == 0) {
            encoding = line.substr(std::char_traits<char>::length(TEXT_ENCODING));
            if (encoding.empty()) throw std::runtime_error("empty GSC text encoding");
            continue;
        }
        if (line.compare(0, std::char_traits<char>::length(INSTRUCTION_SCHEMA), INSTRUCTION_SCHEMA) == 0) {
            if (selectedSchema) throw std::runtime_error("duplicate ;@gsc-schema metadata");
            selectedSchema = parseSchemaName(
                line.substr(std::char_traits<char>::length(INSTRUCTION_SCHEMA)));
            continue;
        }
        const auto tokens = tokenizeSource(line, lineNo);
        if (tokens.empty()) continue;
        const auto& first = tokens.front().value;
        if (first.front() == ':') {
            if (tokens.size() != 1 || first.size() == 1)
                throw std::runtime_error("line " + std::to_string(lineNo) +
                                         ": malformed label");
            if (!labels.emplace(first.substr(1), offset).second)
                throw std::runtime_error("line " + std::to_string(lineNo) +
                                         ": duplicate label");
            continue;
        }
        if (first.front() != '*')
            throw std::runtime_error("line " + std::to_string(lineNo) +
                                     ": expected command or label");
        if (!selectedSchema)
            throw std::runtime_error(";@gsc-schema must precede commands");
        SourceInstruction instruction;
        instruction.lineNo = lineNo;
        instruction.offset = offset;
        const std::string name = first.substr(1);
        size_t operandAt = 1;
        if (name == "datablock") {
            if (tokens.size() < 3)
                throw std::runtime_error("line " + std::to_string(lineNo) +
                                         ": datablock needs index and count");
            const auto index = parseOperand(tokens[1].value, 'D', lineNo);
            const auto count = parseOperand(tokens[2].value, 'D', lineNo);
            if (index != dataBlocks.size() || tokens.size() != count + 3ull)
                throw std::runtime_error("line " + std::to_string(lineNo) +
                                         ": non-sequential or malformed datablock");
            SourceDataBlock block;
            block.lineNo = lineNo;
            for (size_t i = 0; i < count; ++i)
                block.values.push_back(static_cast<int16_t>(
                    parseOperand(tokens[i + 3].value, 'S', lineNo)));
            dataBlocks.push_back(std::move(block));
            continue;
        }
        if (name == "vm") {
            if (tokens.size() < 2)
                throw std::runtime_error("line " + std::to_string(lineNo) +
                                         ": missing VM opcode");
            instruction.opcode = static_cast<uint16_t>(parseOperand(tokens[1].value, 'H', lineNo));
            if (!(instruction.opcode & 0xf000))
                throw std::runtime_error("line " + std::to_string(lineNo) +
                                         ": invalid VM opcode");
            instruction.kinds = (instruction.opcode & 0xf000) == 0xf000
                ? (headerSize == 28 ? "HH" : "HS")
                : (headerSize == 28 ? "HHH" : "HSS");
            operandAt = 2;
        } else if (name == "opcode") {
            if (tokens.size() < 2)
                throw std::runtime_error("line " + std::to_string(lineNo) +
                                         ": missing opcode number");
            instruction.opcode = static_cast<uint16_t>(parseOperand(tokens[1].value, 'H', lineNo));
            operandAt = 2;
        } else if (name == "jz") instruction.opcode = 3;
        else if (name == "jnz") instruction.opcode = 4;
        else if (name == "goto") instruction.opcode = 5;
        else if (name == "data") instruction.opcode = 18;
        else {
            const auto found = opcodes.find(name);
            if (found == opcodes.end())
                throw std::runtime_error("line " + std::to_string(lineNo) +
                                         ": unknown command: " + name);
            instruction.opcode = found->second;
        }
        if (instruction.kinds.empty()) {
            const auto& schemas = schemasFor(*selectedSchema);
            const auto found = schemas.find(instruction.opcode);
            if (found == schemas.end())
                throw std::runtime_error("line " + std::to_string(lineNo) +
                                         ": opcode is unavailable in selected schema");
            instruction.kinds = found->second;
        }
        instruction.operands.assign(tokens.begin() + static_cast<ptrdiff_t>(operandAt),
                                    tokens.end());
        if (instruction.operands.size() != instruction.kinds.size()) {
            throw std::runtime_error("line " + std::to_string(lineNo) +
                                     ": wrong operand count for " + name);
        }
        offset += instructionSize(instruction.kinds);
        instructions.push_back(std::move(instruction));
    }
    if (!headerSize) throw std::runtime_error("TSC has no ;@gsc-byte-format metadata");
    if (!selectedSchema) throw std::runtime_error("TSC has no ;@gsc-schema metadata");
    if (headerSize == 28 && *selectedSchema == InstructionSchema::Modern)
        throw std::runtime_error("modern schema requires a 36-byte GSC header");

    std::vector<std::vector<uint8_t>> strings(1);
    std::unordered_map<std::string, uint32_t> stringIndices{{"", 0}};
    auto addString = [&](const std::string& value) -> uint32_t {
        const auto encoded = convertEncoding(value, "UTF-8", encoding);
        const auto found = stringIndices.find(encoded);
        if (found != stringIndices.end()) return found->second;
        if (strings.size() > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("too many GSC strings");
        const auto index = static_cast<uint32_t>(strings.size());
        strings.emplace_back(encoded.begin(), encoded.end());
        stringIndices.emplace(encoded, index);
        return index;
    };
    std::vector<uint8_t> code;
    std::vector<uint32_t> dataOffsets;
    std::vector<uint16_t> dataWords;
    for (const auto& block : dataBlocks) {
        dataOffsets.push_back(static_cast<uint32_t>(dataWords.size()));
        dataWords.push_back(static_cast<uint16_t>(block.values.size()));
        for (const auto value : block.values) dataWords.push_back(static_cast<uint16_t>(value));
    }
    for (const auto& instruction : instructions) {
        code.push_back(static_cast<uint8_t>(instruction.opcode));
        code.push_back(static_cast<uint8_t>(instruction.opcode >> 8));
        for (size_t i = 0; i < instruction.kinds.size(); ++i) {
            const char kind = instruction.kinds[i];
            uint32_t value;
            const auto& token = instruction.operands[i];
            if (isStringOperand(instruction.opcode, i)) {
                if (!token.quoted)
                    throw std::runtime_error("line " + std::to_string(instruction.lineNo) +
                                             ": string operand must be quoted");
                value = addString(token.value);
            } else if (isCodeTarget(instruction.opcode, i)) {
                const auto found = labels.find(token.value);
                if (found == labels.end())
                    throw std::runtime_error("line " + std::to_string(instruction.lineNo) +
                                             ": unknown label: " + token.value);
                if (found->second > std::numeric_limits<uint32_t>::max())
                    throw std::runtime_error("GSC code is too large");
                value = static_cast<uint32_t>(found->second);
            } else {
                if (token.quoted)
                    throw std::runtime_error("line " + std::to_string(instruction.lineNo) +
                                             ": numeric operand cannot be quoted");
                value = parseOperand(token.value, kind, instruction.lineNo);
            }
            if (kind == 'H' || kind == 'S') {
                code.push_back(static_cast<uint8_t>(value));
                code.push_back(static_cast<uint8_t>(value >> 8));
            } else {
                for (int shift = 0; shift < 32; shift += 8)
                    code.push_back(static_cast<uint8_t>(value >> shift));
            }
        }
    }

    std::vector<uint8_t> stringIndex;
    std::vector<uint8_t> stringPool;
    for (const auto& value : strings) {
        const auto start = static_cast<uint32_t>(stringPool.size());
        for (int shift = 0; shift < 32; shift += 8)
            stringIndex.push_back(static_cast<uint8_t>(start >> shift));
        stringPool.insert(stringPool.end(), value.begin(), value.end());
        stringPool.push_back(0);
    }
    std::vector<uint8_t> dataIndex;
    for (const auto value : dataOffsets)
        for (int shift = 0; shift < 32; shift += 8)
            dataIndex.push_back(static_cast<uint8_t>(value >> shift));
    std::vector<uint8_t> data;
    for (const auto value : dataWords) {
        data.push_back(static_cast<uint8_t>(value));
        data.push_back(static_cast<uint8_t>(value >> 8));
    }
    const size_t modernTrailerSize = headerSize == 36 ? 9 : 0;
    const uint64_t total = headerSize + code.size() + stringIndex.size() +
                           stringPool.size() + dataIndex.size() + data.size() +
                           modernTrailerSize;
    if (total > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("compiled GSC is too large");
    std::vector<uint8_t> result(headerSize, 0);
    writeU32(result, 0, static_cast<uint32_t>(total));
    writeU32(result, 4, static_cast<uint32_t>(headerSize));
    writeU32(result, 8, static_cast<uint32_t>(code.size()));
    writeU32(result, 12, static_cast<uint32_t>(stringIndex.size()));
    writeU32(result, 16, static_cast<uint32_t>(stringPool.size()));
    writeU32(result, 20, static_cast<uint32_t>(dataIndex.size()));
    writeU32(result, 24, static_cast<uint32_t>(dataWords.size()));
    if (headerSize == 36) {
        // The standard CodeX compiler writes two empty four-byte debug tables
        // followed by a one-byte names terminator.
        writeU32(result, 28, 4);
        writeU32(result, 32, 1);
    }
    result.insert(result.end(), code.begin(), code.end());
    result.insert(result.end(), stringIndex.begin(), stringIndex.end());
    result.insert(result.end(), stringPool.begin(), stringPool.end());
    result.insert(result.end(), dataIndex.begin(), dataIndex.end());
    result.insert(result.end(), data.begin(), data.end());
    result.insert(result.end(), modernTrailerSize, 0);
    // Legacy compilers keep this final sentinel outside header[0].
    if (headerSize == 28) result.push_back(0);
    return result;
}

} // namespace

static std::string decompileListing(const std::string& inputPath,
                                    const std::string& encoding) {
    const ParsedGsc gsc(inputPath);
    const auto instructions = gsc.instructions();
    const auto labels = gsc.jumpTargets();
    std::vector<std::string> lines = {
        std::string(BYTE_FORMAT) +
            (gsc.headerSize() == 28 ? "legacy-28" : "modern-36"),
        std::string(TEXT_ENCODING) + encoding,
        std::string(INSTRUCTION_SCHEMA) +
            schemaName(gsc.schema()),
        "; generated from " + gsc.path.filename().string(),
    };
    for (size_t index = 0; index < gsc.dataBlockCount(); ++index) {
        std::ostringstream line;
        const auto values = gsc.dataBlock(index);
        line << "*datablock " << index << ' ' << values.size();
        for (const auto value : values) line << ' ' << value;
        lines.push_back(line.str());
    }
    for (const auto& instruction : instructions) {
        if (labels.count(instruction.offset)) {
            lines.push_back("");
            lines.push_back(":L_" + hex6(instruction.offset));
        }
        lines.push_back(sourceInstruction(gsc, instruction, encoding));
    }
    if (labels.count(gsc.code.size())) {
        lines.push_back("");
        lines.push_back(":L_" + hex6(gsc.code.size()));
    }
    std::ostringstream output;
    for (const auto& line : lines) output << line << '\n';
    return output.str();
}

std::string decompileGsc(const std::string& inputPath, const std::string& encoding) {
    const auto raw = readFile(inputPath);
    try {
        return decompileListing(inputPath, encoding);
    } catch (const std::exception& e) {
        std::string message = e.what();
        std::replace(message.begin(), message.end(), '\n', ' ');
        std::replace(message.begin(), message.end(), '\r', ' ');
        return rawEnvelope(raw) + TEXT_ENCODING + encoding + "\n" +
               "; decompilation unavailable: " + message + "\n";
    }
}

std::vector<uint8_t> restoreGscFromTsc(const std::string& tscText,
                                       const std::string& fallbackEncoding) {
    if (tscText.find(BYTE_FORMAT) != std::string::npos &&
        tscText.find(INSTRUCTION_SCHEMA) != std::string::npos)
        return compileStructuredTsc(tscText, fallbackEncoding);
    std::istringstream input(tscText);
    std::string line;
    std::vector<uint8_t> result;
    size_t expectedSize = 0;
    uint64_t expectedHash = 0;
    bool reading = false;
    bool complete = false;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.compare(0, std::char_traits<char>::length(RAW_HEADER), RAW_HEADER) == 0) {
            if (reading || complete)
                throw std::runtime_error("duplicate ;@gsc-raw-v1 metadata");
            std::istringstream fields(
                line.substr(std::char_traits<char>::length(RAW_HEADER)));
            std::string sizeField, hashField, extra;
            if (!(fields >> sizeField >> hashField) || (fields >> extra) ||
                sizeField.rfind("size=", 0) != 0 ||
                hashField.rfind("fnv1a64=", 0) != 0)
                throw std::runtime_error("malformed ;@gsc-raw-v1 header");
            try {
                size_t used = 0;
                expectedSize = static_cast<size_t>(
                    std::stoull(sizeField.substr(5), &used, 10));
                if (used != sizeField.size() - 5) throw std::invalid_argument("size");
                used = 0;
                expectedHash = std::stoull(hashField.substr(8), &used, 16);
                if (used != hashField.size() - 8 || hashField.size() != 24)
                    throw std::invalid_argument("hash");
            } catch (const std::exception&) {
                throw std::runtime_error("invalid ;@gsc-raw-v1 size or checksum");
            }
            reading = true;
            continue;
        }
        if (!reading) continue;
        if (line == RAW_END) {
            reading = false;
            complete = true;
            continue;
        }
        if (line.compare(0, std::char_traits<char>::length(RAW_CHUNK), RAW_CHUNK) != 0)
            throw std::runtime_error("unexpected line inside ;@gsc-raw metadata");
        const auto hex = line.substr(std::char_traits<char>::length(RAW_CHUNK));
        if (hex.empty() || hex.size() % 2)
            throw std::runtime_error("malformed ;@gsc-raw chunk");
        for (size_t i = 0; i < hex.size(); i += 2)
            result.push_back(static_cast<uint8_t>((hexDigit(hex[i]) << 4) |
                                                  hexDigit(hex[i + 1])));
    }
    if (reading) throw std::runtime_error("missing ;@gsc-raw-end marker");
    if (!complete) throw std::runtime_error("TSC has no ;@gsc-raw-v1 metadata");
    if (result.size() != expectedSize)
        throw std::runtime_error(";@gsc-raw size mismatch");
    if (fnv1a64(result) != expectedHash)
        throw std::runtime_error(";@gsc-raw checksum mismatch");
    return result;
}

void decompileGscToFile(const std::string& inputPath,
                        const std::string& outputPath,
                        const std::string& encoding) {
    const auto output = decompileGsc(inputPath, encoding);
    writeFileIfChanged(outputPath,
        reinterpret_cast<const uint8_t*>(output.data()), output.size());
}

void restoreGscFromTscFile(const std::string& inputPath,
                           const std::string& outputPath,
                           const std::string& fallbackEncoding) {
    const auto text = readFile(inputPath);
    const auto output = restoreGscFromTsc(
        std::string(reinterpret_cast<const char*>(text.data()), text.size()),
        fallbackEncoding);
    writeFileIfChanged(outputPath, output);
}

} // namespace liarsoft
