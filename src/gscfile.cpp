#include "gscfile.h"
#include "encoding.h"
#include <cerrno>
#include <cstring>
#include <iostream>

namespace liarsoft {


static std::vector<uint8_t> encodeToBytes(const std::string& str,
                                          const std::string& encoding) {
    // Convert from UTF-8 (internal) to target encoding
    std::string encoded = convertEncoding(str, "UTF-8", encoding);
    return std::vector<uint8_t>(encoded.begin(), encoded.end());
}

static std::string decodeFromBytes(const std::vector<uint8_t>& bytes,
                                   const std::string& encoding) {
    std::string raw(bytes.begin(), bytes.end());
    return convertEncoding(raw, encoding, "UTF-8");
}

// ---- GscFile implementation ----

GscFile GscFile::fromFile(const std::string& path, const std::string& enc) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Cannot open file: " + path);
    }
    return fromStream(stream, enc);
}

GscFile GscFile::fromBytes(const std::vector<uint8_t>& bytes, const std::string& enc) {
    GscFile gsc;
    gsc.encoding = enc;
    BigEndianReader reader(bytes);

    // -- HEADER (9 x int32)
    gsc.fileLength              = reader.readInt32();
    gsc.headerLength            = reader.readInt32();
    gsc.commandLength           = reader.readInt32();
    gsc.stringDeclarationLength = reader.readInt32();
    gsc.stringDefinitionLength  = reader.readInt32();
    gsc.unknown1                = reader.readInt32();
    gsc.unknown2                = reader.readInt32();
    gsc.unknown3                = reader.readInt32();
    gsc.unknown4                = reader.readInt32();

    // -- COMMAND SECTION
    gsc.commandSection = reader.readBytes(gsc.commandLength);

    // -- STRING DECLARATION
    reader.readBytes(8); // skip first 8 bytes

    int stringCount = gsc.stringDeclarationLength / 4 - 2 + 1;
    if (stringCount < 0) stringCount = 0;

    gsc.stringLengths.resize(stringCount);
    for (int i = 0; i < stringCount - 1; ++i) {
        gsc.stringLengths[i] = reader.readInt32();
    }
    if (stringCount > 0) {
        gsc.stringLengths[stringCount - 1] = gsc.stringDefinitionLength;
    }

    reader.readByte(); // skip 0 byte

    // -- STRINGS
    gsc.strings.resize(stringCount);
    int read = 1; // cumulative bytes read (the initial 1 from the C# code)
    for (int i = 0; i < stringCount; ++i) {
        int toRead = gsc.stringLengths[i];
        int byteCount = toRead - read - 1; // minus trailing null
        if (byteCount < 0) byteCount = 0;

        std::vector<uint8_t> strBytes = reader.readBytes(byteCount);
        reader.readByte(); // trailing 0

        // Decode from the specified encoding to UTF-8 (internal)
        gsc.strings[i] = decodeFromBytes(strBytes, enc);
        read = toRead;
    }

    // -- END SEQUENCE (remaining bytes)
    gsc.endSequence = reader.readBytes(reader.remaining());

    return gsc;
}

GscFile GscFile::fromStream(std::ifstream& stream, const std::string& enc) {
    // Read entire file into memory
    stream.seekg(0, std::ios::end);
    size_t size = stream.tellg();
    stream.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(size);
    stream.read(reinterpret_cast<char*>(bytes.data()), size);

    if (stream.gcount() != static_cast<std::streamsize>(size)) {
        throw std::runtime_error("Failed to read entire file");
    }

    return fromBytes(bytes, enc);
}

int32_t GscFile::calcStringDefinitionLength() const {
    int32_t len = 1;
    for (const auto& str : strings) {
        // Convert from internal UTF-8 to the target encoding to get byte count
        auto encoded = encodeToBytes(str, encoding);
        len += static_cast<int32_t>(encoded.size()) + 1; // +1 for null terminator
    }
    return len;
}

void GscFile::save(const std::string& path) const {
    auto bytes = toBytes();

    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Cannot write file: " + path);
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::vector<uint8_t> GscFile::toBytes() const {
    BigEndianWriter writer;

    int32_t calcStrDefLen = calcStringDefinitionLength();

    // -- HEADER (will patch fileLength later)
    writer.writeInt32(0); // placeholder for FileLength
    writer.writeInt32(headerLength);
    writer.writeInt32(commandLength);
    writer.writeInt32(stringDeclarationLength);
    writer.writeInt32(calcStrDefLen);
    writer.writeInt32(unknown1);
    writer.writeInt32(unknown2);
    writer.writeInt32(unknown3);
    writer.writeInt32(unknown4);

    // -- COMMAND SECTION
    writer.writeBytes(commandSection);

    // -- STRING DECLARATION
    writer.writeInt32(0);
    writer.writeInt32(1);

    int bytes = 1;
    for (size_t i = 0; i < strings.size() - 1; ++i) {
        auto encoded = encodeToBytes(strings[i], encoding);
        bytes += static_cast<int>(encoded.size()) + 1;
        writer.writeInt32(bytes);
    }

    writer.writeByte(0);

    // -- STRINGS
    for (const auto& str : strings) {
        auto encoded = encodeToBytes(str, encoding);
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
