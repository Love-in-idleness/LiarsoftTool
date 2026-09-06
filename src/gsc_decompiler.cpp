#include "gsc_decompiler.h"

#include "encoding.h"
#include "fileio.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
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
constexpr const char* STRUCTURE_HEADER = ";@gsc-structure-v1 ";
constexpr const char* STRUCTURE_INSTRUCTION = ";@gsc-instruction ";
constexpr const char* STRUCTURE_SECTION = ";@gsc-section ";
constexpr const char* STRUCTURE_END = ";@gsc-structure-end";
constexpr const char* TEXT_ENCODING = ";@gsc-text-encoding ";
constexpr const char* INSTRUCTION_SCHEMA = ";@gsc-instruction-schema ";

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

std::vector<uint8_t> decodeHex(const std::string& hex,
                               const std::string& context) {
    if (hex.empty() || hex.size() % 2)
        throw std::runtime_error("malformed " + context + " bytes");
    std::vector<uint8_t> result;
    result.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2)
        result.push_back(static_cast<uint8_t>((hexDigit(hex[i]) << 4) |
                                              hexDigit(hex[i + 1])));
    return result;
}

void appendHexLines(std::ostringstream& out, const std::string& name,
                    const std::vector<uint8_t>& data) {
    static constexpr char HEX[] = "0123456789abcdef";
    if (data.empty()) {
        out << STRUCTURE_SECTION << name << " -\n";
        return;
    }
    for (size_t offset = 0; offset < data.size(); offset += 48) {
        out << STRUCTURE_SECTION << name << ' ';
        const size_t end = std::min(data.size(), offset + 48);
        for (size_t i = offset; i < end; ++i)
            out << HEX[data[i] >> 4] << HEX[data[i] & 0x0f];
        out << '\n';
    }
}

void writeU32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    if (offset + 4 > data.size())
        throw std::runtime_error("cannot patch truncated GSC header");
    data[offset] = static_cast<uint8_t>(value);
    data[offset + 1] = static_cast<uint8_t>(value >> 8);
    data[offset + 2] = static_cast<uint8_t>(value >> 16);
    data[offset + 3] = static_cast<uint8_t>(value >> 24);
}

void writeU16(std::vector<uint8_t>& data, size_t offset, uint16_t value) {
    if (offset + 2 > data.size())
        throw std::runtime_error("cannot patch truncated GSC instruction");
    data[offset] = static_cast<uint8_t>(value);
    data[offset + 1] = static_cast<uint8_t>(value >> 8);
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

    fs::path path;
    std::vector<uint8_t> code;

    InstructionSchema schema() const { return instructionSchema; }

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
            if (instruction.opcode == 81) {
                stringBytes(static_cast<size_t>(instruction.operands[4]));
                stringBytes(static_cast<size_t>(instruction.operands[5]));
            } else if (instruction.opcode == 82) {
                stringBytes(static_cast<size_t>(instruction.operands[4]));
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

std::string structuredEnvelope(const std::vector<uint8_t>& raw,
                               const ParsedGsc& gsc) {
    const size_t headerSize = readU32(raw, 4);
    size_t pos = headerSize;
    const auto slice = [&](size_t& offset, size_t size) {
        if (offset > raw.size() || size > raw.size() - offset)
            throw std::runtime_error("truncated GSC section");
        std::vector<uint8_t> result(
            raw.begin() + static_cast<ptrdiff_t>(offset),
            raw.begin() + static_cast<ptrdiff_t>(offset + size));
        offset += size;
        return result;
    };

    const auto header = std::vector<uint8_t>(raw.begin(), raw.begin() + headerSize);
    pos += readU32(raw, 8); // code is represented by instructions below.
    std::vector<std::pair<std::string, std::vector<uint8_t>>> sections;
    if (headerSize == 28) {
        sections.push_back({"declaration", slice(pos, readU32(raw, 12))});
        sections.push_back({"strings", slice(pos, readU32(raw, 16))});
        sections.push_back({"data-index", slice(pos, readU32(raw, 20))});
        sections.push_back({"data", slice(pos, readU32(raw, 24) * 2ull)});
    } else {
        sections.push_back({"string-index", slice(pos, readU32(raw, 12))});
        sections.push_back({"strings", slice(pos, readU32(raw, 16))});
        sections.push_back({"data-index", slice(pos, readU32(raw, 20))});
        sections.push_back({"data", slice(pos, readU32(raw, 24) * 2ull)});
    }
    sections.push_back({"extra", slice(pos, raw.size() - pos)});

    std::ostringstream out;
    out << STRUCTURE_HEADER << "size=" << raw.size()
        << " fnv1a64=" << hex64(fnv1a64(raw)) << '\n';
    appendHexLines(out, "header", header);
    for (const auto& instruction : gsc.instructions()) {
        out << STRUCTURE_INSTRUCTION << std::hex << std::nouppercase
            << std::setw(6) << std::setfill('0') << instruction.offset << ' '
            << std::setw(4) << instruction.opcode << std::dec << ' '
            << (instruction.kinds.empty() ? "-" : instruction.kinds);
        for (const auto operand : instruction.operands) out << ' ' << operand;
        out << '\n';
    }
    for (const auto& section : sections)
        appendHexLines(out, section.first, section.second);
    out << STRUCTURE_END << '\n';
    return out.str();
}

const std::unordered_map<uint16_t, std::string> NAMES = {
    {8,"end"},{9,"rnd"},{10,"hit"},{11,"hitc"},{12,"jump"},{13,"wait"},
    {14,"select"},{15,"gosub"},{16,"return"},{17,"save"},{19,"subscript"},
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
    {112,"root"},{114,"getflag"},{115,"menuon"},{116,"menuset"},
    {117,"menuget"},{120,"fontsize"},{121,"folder"},{130,"numload"},
    {131,"numreng"},{132,"numenable"},{134,"numloc"},{135,"numset"},
    {136,"num"},
    {150,"strset"},{151,"stradd"},{152,"numstr"},{153,"strnum"},
    {154,"strcpy"},{155,"strcat"},{156,"strinput"},{157,"strinprop"},
    {158,"strsave"},{159,"strload"},{200,"insub"},{201,"metamor"},
    {202,"flagset"},{210,"dynsel"},{211,"dynans"},{212,"dynnext"},
    {213,"dyndo"},{221,"mapload"},{222,"mapcls"},{223,"mapobj"},
    {225,"locmap"},{230,"bganim"},{231,"await"},{255,"excmd"},
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
        try {
            for (const auto value :
                 gsc.dataBlock(static_cast<size_t>(instruction.operands[1])))
                out << ' ' << value;
        } catch (const std::runtime_error&) {
            out << " block=" << instruction.operands[1];
        }
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

std::vector<uint8_t> restoreStructuredEnvelope(const std::string& tscText) {
    std::istringstream input(tscText);
    std::string line;
    size_t expectedSize = 0;
    uint64_t expectedHash = 0;
    bool reading = false;
    bool complete = false;
    std::vector<uint8_t> code;
    std::unordered_map<std::string, std::vector<uint8_t>> sections;

    const auto parseHexNumber = [](const std::string& value,
                                   const std::string& context) {
        try {
            size_t used = 0;
            const auto result = std::stoull(value, &used, 16);
            if (used != value.size()) throw std::invalid_argument("hex");
            return result;
        } catch (const std::exception&) {
            throw std::runtime_error("invalid " + context);
        }
    };
    const auto append16 = [](std::vector<uint8_t>& output, uint16_t value) {
        output.push_back(static_cast<uint8_t>(value));
        output.push_back(static_cast<uint8_t>(value >> 8));
    };
    const auto append32 = [&](std::vector<uint8_t>& output, uint32_t value) {
        append16(output, static_cast<uint16_t>(value));
        append16(output, static_cast<uint16_t>(value >> 16));
    };

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.compare(0, std::char_traits<char>::length(STRUCTURE_HEADER),
                         STRUCTURE_HEADER) == 0) {
            if (reading || complete)
                throw std::runtime_error("duplicate ;@gsc-structure-v1 metadata");
            std::istringstream fields(
                line.substr(std::char_traits<char>::length(STRUCTURE_HEADER)));
            std::string sizeField, hashField, extra;
            if (!(fields >> sizeField >> hashField) || (fields >> extra) ||
                sizeField.rfind("size=", 0) != 0 ||
                hashField.rfind("fnv1a64=", 0) != 0)
                throw std::runtime_error("malformed ;@gsc-structure-v1 header");
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
                throw std::runtime_error(
                    "invalid ;@gsc-structure-v1 size or checksum");
            }
            reading = true;
            continue;
        }
        if (!reading) continue;
        if (line == STRUCTURE_END) {
            reading = false;
            complete = true;
            continue;
        }
        if (line.compare(0, std::char_traits<char>::length(STRUCTURE_SECTION),
                         STRUCTURE_SECTION) == 0) {
            std::istringstream fields(
                line.substr(std::char_traits<char>::length(STRUCTURE_SECTION)));
            std::string name, bytes, extra;
            if (!(fields >> name >> bytes) || (fields >> extra))
                throw std::runtime_error("malformed ;@gsc-section metadata");
            if (bytes != "-") {
                auto decoded = decodeHex(bytes, ";@gsc-section");
                auto& section = sections[name];
                section.insert(section.end(), decoded.begin(), decoded.end());
            } else {
                sections.emplace(name, std::vector<uint8_t>());
            }
            continue;
        }
        if (line.compare(0, std::char_traits<char>::length(STRUCTURE_INSTRUCTION),
                         STRUCTURE_INSTRUCTION) == 0) {
            std::istringstream fields(line.substr(
                std::char_traits<char>::length(STRUCTURE_INSTRUCTION)));
            std::string offsetField, opcodeField, kinds;
            if (!(fields >> offsetField >> opcodeField >> kinds))
                throw std::runtime_error("malformed ;@gsc-instruction metadata");
            const auto offset = parseHexNumber(offsetField, "GSC instruction offset");
            const auto opcode = parseHexNumber(opcodeField, "GSC opcode");
            if (offset != code.size() || opcode > 0xffff)
                throw std::runtime_error("non-contiguous GSC instructions");
            append16(code, static_cast<uint16_t>(opcode));
            if (kinds == "-") kinds.clear();
            for (const char kind : kinds) {
                if (kind != 'H' && kind != 'S' && kind != 'D' && kind != 'E')
                    throw std::runtime_error("invalid GSC operand kind");
                std::string token;
                if (!(fields >> token))
                    throw std::runtime_error("missing structured GSC operand");
                int64_t value;
                try {
                    size_t used = 0;
                    value = std::stoll(token, &used, 10);
                    if (used != token.size()) throw std::invalid_argument("operand");
                } catch (const std::exception&) {
                    throw std::runtime_error("invalid structured GSC operand");
                }
                if (kind == 'S') {
                    if (value < -32768 || value > 32767)
                        throw std::runtime_error("structured GSC operand is out of range");
                    append16(code, static_cast<uint16_t>(value));
                } else if (kind == 'H') {
                    if (value < 0 || value > 0xffff)
                        throw std::runtime_error("structured GSC operand is out of range");
                    append16(code, static_cast<uint16_t>(value));
                } else {
                    if (value < 0 || static_cast<uint64_t>(value) > 0xffffffffull)
                        throw std::runtime_error("structured GSC operand is out of range");
                    append32(code, static_cast<uint32_t>(value));
                }
            }
            std::string extra;
            if (fields >> extra)
                throw std::runtime_error("extra structured GSC operand");
            continue;
        }
        throw std::runtime_error("unexpected line inside ;@gsc-structure metadata");
    }
    if (reading) throw std::runtime_error("missing ;@gsc-structure-end marker");
    if (!complete) throw std::runtime_error("TSC has no ;@gsc-structure-v1 metadata");

    const auto headerAt = sections.find("header");
    if (headerAt == sections.end() || headerAt->second.size() < 28)
        throw std::runtime_error("missing structured GSC header");
    const auto& header = headerAt->second;
    const size_t headerSize = readU32(header, 4);
    if ((headerSize != 28 && headerSize != 36) || header.size() != headerSize ||
        readU32(header, 8) != code.size())
        throw std::runtime_error("structured GSC header does not match its code");

    const auto require = [&](const std::string& name, size_t size) -> const std::vector<uint8_t>& {
        const auto found = sections.find(name);
        if (found == sections.end() || found->second.size() != size)
            throw std::runtime_error("structured GSC section size mismatch: " + name);
        return found->second;
    };
    std::vector<uint8_t> result = header;
    result.insert(result.end(), code.begin(), code.end());
    if (headerSize == 28) {
        for (const auto& item : std::vector<std::pair<std::string, size_t>>{
                 {"declaration", readU32(header, 12)},
                 {"strings", readU32(header, 16)},
                 {"data-index", readU32(header, 20)},
                 {"data", readU32(header, 24) * 2ull}}) {
            const auto& section = require(item.first, item.second);
            result.insert(result.end(), section.begin(), section.end());
        }
    } else {
        for (const auto& item : std::vector<std::pair<std::string, size_t>>{
                 {"string-index", readU32(header, 12)},
                 {"strings", readU32(header, 16)},
                 {"data-index", readU32(header, 20)},
                 {"data", readU32(header, 24) * 2ull}}) {
            const auto& section = require(item.first, item.second);
            result.insert(result.end(), section.begin(), section.end());
        }
    }
    if (result.size() > expectedSize)
        throw std::runtime_error("structured GSC exceeds its declared size");
    const auto& extra = require("extra", expectedSize - result.size());
    result.insert(result.end(), extra.begin(), extra.end());
    if (result.size() != expectedSize || fnv1a64(result) != expectedHash)
        throw std::runtime_error("structured GSC checksum mismatch");
    return result;
}

std::vector<uint8_t> applyTextEdits(const std::vector<uint8_t>& raw,
                                    const std::string& tscText,
                                    const std::string& fallbackEncoding) {
    std::vector<std::pair<size_t, std::string>> edits;
    std::vector<std::pair<size_t, std::string>> commandEdits;
    std::optional<size_t> pendingOffset;
    std::string encoding = fallbackEncoding;
    InstructionSchema instructionSchema = InstructionSchema::Modern;
    std::istringstream input(tscText);
    std::string line;
    bool afterRaw = false;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == RAW_END || line == STRUCTURE_END) {
            afterRaw = true;
            continue;
        }
        if (!afterRaw) continue;
        if (line.compare(0, std::char_traits<char>::length(TEXT_ENCODING),
                         TEXT_ENCODING) == 0) {
            encoding = line.substr(std::char_traits<char>::length(TEXT_ENCODING));
            if (encoding.empty())
                throw std::runtime_error("empty ;@gsc-text-encoding metadata");
            continue;
        }
        if (line.compare(0, std::char_traits<char>::length(INSTRUCTION_SCHEMA),
                         INSTRUCTION_SCHEMA) == 0) {
            const auto value = line.substr(
                std::char_traits<char>::length(INSTRUCTION_SCHEMA));
            if (value == "pre-codex") instructionSchema = InstructionSchema::PreCodeX;
            else if (value == "early") instructionSchema = InstructionSchema::Early;
            else if (value == "rscript18") instructionSchema = InstructionSchema::RScript18;
            else if (value == "rscript19") instructionSchema = InstructionSchema::RScript19;
            else if (value != "modern")
                throw std::runtime_error("unsupported GSC instruction schema: " + value);
            continue;
        }
        if (line.size() == 9 && line.compare(0, 3, "; @") == 0) {
            try {
                size_t used = 0;
                pendingOffset = static_cast<size_t>(
                    std::stoull(line.substr(3), &used, 16));
                if (used != 6) throw std::invalid_argument("offset");
            } catch (const std::exception&) {
                throw std::runtime_error("invalid GSC text offset marker");
            }
            continue;
        }
        const auto marker = line.rfind(" ; @");
        if (line.size() >= 10 && line.front() == '*' && marker != std::string::npos &&
            marker + 10 == line.size()) {
            try {
                size_t used = 0;
                const auto offset = static_cast<size_t>(
                    std::stoull(line.substr(marker + 4), &used, 16));
                if (used != 6) throw std::invalid_argument("offset");
                commandEdits.emplace_back(offset, line.substr(1, marker - 1));
            } catch (const std::exception&) {
                throw std::runtime_error("invalid GSC command offset marker");
            }
        }
        if (pendingOffset && !line.empty() && line.front() == '\\')
            edits.emplace_back(*pendingOffset, line);
        pendingOffset.reset();
    }
    if (edits.empty() && commandEdits.empty()) return raw;
    if (raw.size() < 36 || readU32(raw, 4) != 36) return raw;

    const size_t codeSize = readU32(raw, 8);
    const size_t indexSize = readU32(raw, 12);
    const size_t stringsSize = readU32(raw, 16);
    const size_t codeStart = 36;
    const size_t indexStart = codeStart + codeSize;
    const size_t stringsStart = indexStart + indexSize;
    const size_t tailStart = stringsStart + stringsSize;
    if (indexSize % 4 || tailStart > raw.size())
        throw std::runtime_error("malformed modern GSC string sections");

    std::vector<uint8_t> working = raw;
    auto parseNumber = [](const std::string& token, char kind) -> uint32_t {
        size_t depth = 0;
        if (kind == 'E') {
            while (depth < token.size() && token[depth] == '@') ++depth;
        }
        if (depth > 0xffff)
            throw std::runtime_error("GSC expression indirection is too deep");
        const auto digits = token.substr(depth);
        if (digits.empty()) throw std::runtime_error("missing numeric GSC operand");
        size_t used = 0;
        int64_t value = 0;
        try {
            value = std::stoll(digits, &used, 10);
        } catch (const std::exception&) {
            throw std::runtime_error("invalid numeric GSC operand: " + token);
        }
        if (used != digits.size())
            throw std::runtime_error("invalid numeric GSC operand: " + token);
        if (kind == 'E') {
            if ((!depth && (value < -32768 || value > 32767)) ||
                (depth && (value < 0 || value > 65535)))
                throw std::runtime_error("GSC expression operand is out of range");
            return static_cast<uint32_t>(depth * 0x10000ull |
                                         static_cast<uint16_t>(value));
        }
        if (kind == 'S') {
            if (value < -32768 || value > 32767)
                throw std::runtime_error("signed GSC operand is out of range");
            return static_cast<uint16_t>(value);
        }
        const uint64_t maximum = kind == 'H' ? 0xffffull : 0xffffffffull;
        if (value < 0 || static_cast<uint64_t>(value) > maximum)
            throw std::runtime_error("unsigned GSC operand is out of range");
        return static_cast<uint32_t>(value);
    };
    std::unordered_map<std::string, uint16_t> opcodes;
    for (const auto& item : NAMES) opcodes[item.second] = item.first;
    for (const auto& edit : commandEdits) {
        const size_t offset = edit.first;
        if (offset + 2 > codeSize)
            throw std::runtime_error("TSC command offset is outside the GSC code section");
        const size_t instruction = codeStart + offset;
        const uint16_t originalOpcode = readU16(working, instruction);
        std::istringstream fields(edit.second);
        std::string name;
        fields >> name;
        if (name == "goto") {
            std::string label, extra;
            if (originalOpcode < 3 || originalOpcode > 5 || !(fields >> label) ||
                (fields >> extra) || label.compare(0, 2, "L_") != 0)
                throw std::runtime_error("malformed or misplaced TSC goto");
            try {
                size_t used = 0;
                const auto target = std::stoull(label.substr(2), &used, 16);
                if (used != label.size() - 2 || target > 0xffffffffull)
                    throw std::invalid_argument("target");
                writeU32(working, instruction + 2, static_cast<uint32_t>(target));
            } catch (const std::exception&) {
                throw std::runtime_error("invalid TSC goto label");
            }
            continue;
        }
        if (originalOpcode == 81 && name == "voice") {
            std::string value, extra;
            if (!(fields >> value) || (fields >> extra))
                throw std::runtime_error("malformed TXT voice command");
            writeU32(working, instruction + 6, parseNumber(value, 'E'));
            continue;
        }
        const auto named = opcodes.find(name);
        if (named == opcodes.end() || named->second != originalOpcode)
            throw std::runtime_error("TSC command does not match its original GSC opcode");
        const auto& schemas = schemasFor(instructionSchema);
        const auto schema = schemas.find(originalOpcode);
        if (schema == schemas.end())
            throw std::runtime_error("GSC command has no selected operand schema");
        size_t cursor = instruction + 2;
        for (const char kind : schema->second) {
            std::string token;
            if (!(fields >> token))
                throw std::runtime_error("TSC command has too few operands");
            const auto value = parseNumber(token, kind);
            if (kind == 'H' || kind == 'S') {
                writeU16(working, cursor, static_cast<uint16_t>(value));
                cursor += 2;
            } else {
                writeU32(working, cursor, value);
                cursor += 4;
            }
        }
        std::string extra;
        if (fields >> extra) throw std::runtime_error("TSC command has too many operands");
    }

    const size_t stringCount = indexSize / 4;
    std::vector<std::vector<uint8_t>> strings;
    strings.reserve(stringCount);
    for (size_t index = 0; index < stringCount; ++index) {
        const size_t start = readU32(raw, indexStart + index * 4);
        if (start >= stringsSize)
            throw std::runtime_error("GSC string starts outside its table");
        const auto begin = raw.begin() + static_cast<ptrdiff_t>(stringsStart + start);
        const auto tableEnd = raw.begin() + static_cast<ptrdiff_t>(tailStart);
        const auto end = std::find(begin, tableEnd, 0);
        if (end == tableEnd) throw std::runtime_error("unterminated GSC string");
        strings.emplace_back(begin, end);
    }

    std::unordered_map<size_t, std::vector<uint8_t>> replacements;
    auto decodedString = [&](size_t index) {
        if (index >= strings.size())
            throw std::runtime_error("TSC text refers to an invalid GSC string index");
        return convertEncoding(
            std::string(strings[index].begin(), strings[index].end()),
            encoding, "UTF-8");
    };
    auto setString = [&](size_t index, const std::string& value) {
        if (index >= strings.size())
            throw std::runtime_error("TSC text refers to an invalid GSC string index");
        const auto encoded = convertEncoding(value, "UTF-8", encoding);
        std::vector<uint8_t> bytes(encoded.begin(), encoded.end());
        const auto found = replacements.find(index);
        if (found != replacements.end() && found->second != bytes)
            throw std::runtime_error("conflicting edits to a shared GSC string");
        replacements[index] = std::move(bytes);
    };

    for (const auto& edit : edits) {
        const size_t offset = edit.first;
        if (offset + 2 > codeSize)
            throw std::runtime_error("TSC text offset is outside the GSC code section");
        const size_t instruction = codeStart + offset;
        const uint16_t opcode = readU16(working, instruction);
        if (opcode == 81) {
            if (offset + 30 > codeSize)
                throw std::runtime_error("truncated TXT instruction");
            const size_t nameIndex = readU32(working, instruction + 18);
            const size_t textIndex = readU32(working, instruction + 22);
            const std::string content = edit.second.substr(1);
            const std::string delimiter = "\"：\"";
            const auto separator = content.find(delimiter);
            std::string name, text;
            if (separator == std::string::npos) {
                text = content;
            } else {
                name = content.substr(0, separator);
                text = content.substr(separator + delimiter.size());
            }
            if (name != decodedString(nameIndex)) setString(nameIndex, name);
            if (text != decodedString(textIndex)) setString(textIndex, text);
        } else if (opcode == 82) {
            if (offset + 26 > codeSize || edit.second.compare(0, 8, "\\append ") != 0)
                throw std::runtime_error("malformed TXA text line");
            const size_t textIndex = readU32(working, instruction + 18);
            const auto text = edit.second.substr(8);
            if (text != decodedString(textIndex)) setString(textIndex, text);
        } else {
            throw std::runtime_error("GSC text marker does not point to TXT or TXA");
        }
    }

    bool changed = false;
    for (const auto& replacement : replacements) {
        if (strings[replacement.first] != replacement.second) changed = true;
        strings[replacement.first] = replacement.second;
    }
    if (!changed) return working;

    std::vector<uint8_t> pool;
    std::vector<uint32_t> offsets;
    for (const auto& value : strings) {
        offsets.push_back(static_cast<uint32_t>(pool.size()));
        pool.insert(pool.end(), value.begin(), value.end());
        pool.push_back(0);
    }
    std::vector<uint8_t> result(working.begin(),
                                working.begin() + static_cast<ptrdiff_t>(indexStart));
    for (const auto offset : offsets) {
        result.push_back(static_cast<uint8_t>(offset));
        result.push_back(static_cast<uint8_t>(offset >> 8));
        result.push_back(static_cast<uint8_t>(offset >> 16));
        result.push_back(static_cast<uint8_t>(offset >> 24));
    }
    result.insert(result.end(), pool.begin(), pool.end());
    result.insert(result.end(), working.begin() + static_cast<ptrdiff_t>(tailStart),
                  working.end());
    writeU32(result, 0, static_cast<uint32_t>(result.size()));
    writeU32(result, 16, static_cast<uint32_t>(pool.size()));
    return result;
}

} // namespace

static std::string decompileListing(const std::string& inputPath,
                                    const std::string& encoding) {
    const ParsedGsc gsc(inputPath);
    const auto instructions = gsc.instructions();
    const auto labels = gsc.jumpTargets();
    std::vector<std::string> lines = {
        std::string(INSTRUCTION_SCHEMA) +
            (gsc.schema() == InstructionSchema::PreCodeX ? "pre-codex" :
             gsc.schema() == InstructionSchema::Early ? "early" :
             gsc.schema() == InstructionSchema::RScript18 ? "rscript18" :
             gsc.schema() == InstructionSchema::RScript19 ? "rscript19" : "modern"),
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

std::string decompileGsc(const std::string& inputPath, const std::string& encoding) {
    const auto raw = readFile(inputPath);
    try {
        const ParsedGsc gsc(inputPath);
        return structuredEnvelope(raw, gsc) + TEXT_ENCODING + encoding + "\n" +
               decompileListing(inputPath, encoding);
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
    if (tscText.find(STRUCTURE_HEADER) != std::string::npos) {
        const auto raw = restoreStructuredEnvelope(tscText);
        return applyTextEdits(raw, tscText, fallbackEncoding);
    }
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
    return applyTextEdits(result, tscText, fallbackEncoding);
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
