#include "gscfile.h"
#include "encoding.h"
#include "fileio.h"
#include <limits>

namespace liarsoft {


static std::vector<uint8_t> encodeToBytes(const std::string& str,
                                          const std::string& encoding) {
    // Convert from UTF-8 (internal) to target encoding
    if (str.find('\0') != std::string::npos)
        throw std::runtime_error("GSC string contains a NUL character");
    std::string encoded = convertEncoding(str, "UTF-8", encoding, true);
    return std::vector<uint8_t>(encoded.begin(), encoded.end());
}

static std::string decodeFromBytes(const std::vector<uint8_t>& bytes,
                                   const std::string& encoding) {
    std::string raw(bytes.begin(), bytes.end());
    return convertEncoding(raw, encoding, "UTF-8");
}

// ---- GscFile implementation ----

GscFile GscFile::fromFile(const std::string& path, const std::string& enc) {
    return fromBytes(readFile(path), enc);
}

GscFile GscFile::fromBytes(const std::vector<uint8_t>& bytes, const std::string& enc) {
    GscFile gsc;
    gsc.encoding = enc;
    BigEndianReader reader(bytes);

    // -- HEADER (always starts with 5 mandatory int32 fields)
    gsc.fileLength              = reader.readInt32();
    gsc.headerLength            = reader.readInt32();
    gsc.commandLength           = reader.readInt32();
    gsc.stringDeclarationLength = reader.readInt32();
    gsc.stringDefinitionLength  = reader.readInt32();

    if (gsc.headerLength < 20 || gsc.headerLength > 36 || gsc.headerLength % 4 ||
        gsc.commandLength < 0 || gsc.stringDeclarationLength < 4 ||
        gsc.stringDeclarationLength % 4 || gsc.stringDefinitionLength < 1 ||
        gsc.fileLength < gsc.headerLength ||
        static_cast<uint64_t>(gsc.fileLength) > bytes.size())
        throw std::runtime_error("Invalid GSC header or section length");
    const uint64_t sectionsEnd = uint64_t(gsc.headerLength) + gsc.commandLength +
        gsc.stringDeclarationLength + gsc.stringDefinitionLength;
    if (sectionsEnd > bytes.size())
        throw std::runtime_error("GSC sections extend beyond the input file");

    // Read remaining header int32s based on headerLength
    // Standard header is 36 bytes (9 fields). Non-standard may be 28 (7 fields) or other.
    int totalHeaderFields = gsc.headerLength / 4;
    int extraFields = totalHeaderFields - 5; // beyond the 5 mandatory ones
    if (extraFields > 0) gsc.unknown1 = reader.readInt32(); else gsc.unknown1 = 0;
    if (extraFields > 1) gsc.unknown2 = reader.readInt32(); else gsc.unknown2 = 0;
    if (extraFields > 2) gsc.unknown3 = reader.readInt32(); else gsc.unknown3 = 0;
    if (extraFields > 3) gsc.unknown4 = reader.readInt32(); else gsc.unknown4 = 0;

    // -- COMMAND SECTION
    gsc.commandSection = reader.readBytes(gsc.commandLength);

    const auto declaration = reader.readBytes(gsc.stringDeclarationLength);
    const auto definition = reader.readBytes(gsc.stringDefinitionLength);
    BigEndianReader offsets(declaration);
    if (offsets.readInt32() != 0 || definition.front() != 0)
        throw std::runtime_error("Invalid GSC empty-string sentinel");
    // Index zero is reserved. Other indices may share a string's byte offset.
    while (offsets.remaining()) {
        const int32_t start = offsets.readInt32();
        if (start < 0 || static_cast<size_t>(start) >= definition.size())
            throw std::runtime_error("GSC string offset is outside its section");
        const auto first = definition.begin() + start;
        const auto end = std::find(first, definition.end(), 0);
        if (end == definition.end())
            throw std::runtime_error("GSC string is not NUL-terminated");
        gsc.stringLengths.push_back(static_cast<int32_t>(end - definition.begin() + 1));
        gsc.strings.push_back(decodeFromBytes({first, end}, enc));
    }

    // -- END SEQUENCE (remaining bytes)
    gsc.endSequence = reader.readBytes(reader.remaining());

    return gsc;
}

GscFile GscFile::fromStream(std::ifstream& stream, const std::string& enc) {
    return fromBytes(readStream(stream), enc);
}

void GscFile::save(const std::string& path) const {
    writeFileIfChanged(path, toBytes());
}

std::vector<uint8_t> GscFile::toBytes() const {
    BigEndianWriter writer;

    const auto maximum = static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
    if (headerLength < 20 || headerLength > 36 || headerLength % 4 ||
        strings.size() > maximum / 4 - 1 || commandSection.size() > maximum)
        throw std::runtime_error("Invalid or oversized GSC sections");
    uint64_t definitionLength = 1;
    std::vector<std::vector<uint8_t>> encodedStrings;
    for (const auto& str : strings) {
        encodedStrings.push_back(encodeToBytes(str, encoding));
        definitionLength += encodedStrings.back().size() + 1ull;
        if (definitionLength > maximum)
            throw std::runtime_error("GSC string section is too large");
    }
    const int32_t declarationLength = static_cast<int32_t>((strings.size() + 1) * 4);
    if (uint64_t(headerLength) + commandSection.size() + declarationLength +
            definitionLength + endSequence.size() > maximum)
        throw std::runtime_error("GSC file is too large");

    // -- HEADER (will patch fileLength later)
    writer.writeInt32(0); // placeholder for FileLength
    writer.writeInt32(headerLength);
    writer.writeInt32(static_cast<int32_t>(commandSection.size()));
    writer.writeInt32(declarationLength);
    writer.writeInt32(static_cast<int32_t>(definitionLength));
    // Write remaining header fields only if original header had them
    int totalFields = headerLength / 4;
    if (totalFields > 5) writer.writeInt32(unknown1);
    if (totalFields > 6) writer.writeInt32(unknown2);
    if (totalFields > 7) writer.writeInt32(unknown3);
    if (totalFields > 8) writer.writeInt32(unknown4);

    // -- COMMAND SECTION
    writer.writeBytes(commandSection);

    // -- STRING DECLARATION
    writer.writeInt32(0);
    int bytes = 1;
    for (const auto& encoded : encodedStrings) {
        writer.writeInt32(bytes);
        bytes += static_cast<int>(encoded.size()) + 1;
    }

    writer.writeByte(0);

    // -- STRINGS
    for (const auto& encoded : encodedStrings) {
        writer.writeBytes(encoded);
        writer.writeByte(0); // null terminator
    }

    // -- END SEQUENCE
    writer.writeBytes(endSequence);

    // -- PATCH FILE LENGTH
    writer.patchInt32(0, static_cast<int32_t>(writer.position()));

    return writer.data();
}

} // namespace liarsoft
