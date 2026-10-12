#include "gscfile.h"
#include "gsc_decompiler.h"
#include "fileio.h"
#include "transfile.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>

class ShortReadBuffer : public std::stringbuf {
    bool atEnd = false;
public:
    explicit ShortReadBuffer(const std::string& data) : std::stringbuf(data) {}
    pos_type seekoff(off_type offset, std::ios_base::seekdir direction,
                     std::ios_base::openmode mode) override {
        const auto position = std::stringbuf::seekoff(offset, direction, mode);
        if (direction == std::ios::end) atEnd = true;
        if (direction == std::ios::beg) atEnd = false;
        return direction == std::ios::cur && atEnd ? position + off_type(1) : position;
    }
};

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

template<class F> static void rejects(F action) {
    try { action(); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Malformed GSC or failed stream was accepted");
}

int main() {
    using namespace liarsoft;
    namespace fs = std::filesystem;
    const auto file = fs::temp_directory_path() / ("liarsoft-gscfile-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        const auto original = restoreGscFromTsc(
            ";@gsc-byte-format modern-36\n;@gsc-schema modern\n"
            "*TXT 0 0 0 0 \"Name\" \"Text\" 0\n*end\n");
        auto gsc = GscFile::fromBytes(original, "CP932");
        require(gsc.strings == std::vector<std::string>({"Name", "Text"}), "Incorrect TXT strings");
        require(gsc.toBytes() == original, "Canonical GSC bytes changed");
        writeFileIfChanged(file.string(), original);
        std::ifstream stream(file, std::ios::binary);
        require(GscFile::fromStream(stream, "CP932").toBytes() == original, "Stream/file mismatch");
        stream.close();
        rejects([&] { GscFile::fromStream(stream, "CP932"); });
        ShortReadBuffer shortBuffer(std::string(original.begin(), original.end()));
        std::ifstream shortStream;
        shortStream.std::basic_ios<char>::rdbuf(&shortBuffer);
        shortStream.clear();
        rejects([&] { GscFile::fromStream(shortStream, "CP932"); });
        require(GscFile::fromFile(file.string(), "CP932").toBytes() == original, "Checked file read mismatch");
        for (size_t length = 0; length < original.size(); ++length)
            rejects([&] { GscFile::fromBytes({original.begin(), original.begin() + length}, "CP932"); });
        for (size_t field : {0u, 4u, 8u, 12u, 16u}) {
            for (int32_t value : {-1, std::numeric_limits<int32_t>::max()}) {
                auto bad = original;
                write_int32_be(bad.data(), field, value);
                rejects([&] { GscFile::fromBytes(bad, "CP932"); });
            }
        }
        const size_t index = gsc.headerLength + gsc.commandLength;
        for (int32_t value : {-1, gsc.stringDefinitionLength, std::numeric_limits<int32_t>::max()}) {
            auto bad = original;
            write_int32_be(bad.data(), index + 4, value);
            rejects([&] { GscFile::fromBytes(bad, "CP932"); });
        }
        auto bad = original;
        bad[index + gsc.stringDeclarationLength + gsc.stringDefinitionLength - 1] = 'x';
        rejects([&] { GscFile::fromBytes(bad, "CP932"); });
        auto shared = original;
        write_int32_be(shared.data(), index + 8, 1);
        require(GscFile::fromBytes(shared, "CP932").strings ==
                std::vector<std::string>({"Name", "Name"}), "Shared string offsets were rejected");
        auto nonemptyZero = original;
        nonemptyZero[index + gsc.stringDeclarationLength] = 'X';
        require(GscFile::fromBytes(nonemptyZero, "CP932").strings == gsc.strings,
                "A nonempty index-zero string was rejected");
        write_int32_be(nonemptyZero.data(), index, 1);
        require(GscFile::fromBytes(nonemptyZero, "CP932").strings == gsc.strings,
                "A valid reserved offset was rejected");
        write_int32_be(nonemptyZero.data(), index, -1);
        rejects([&] { GscFile::fromBytes(nonemptyZero, "CP932"); });
        for (const auto* format : {"modern-36", "legacy-28"}) {
            const auto empty = restoreGscFromTsc(std::string(";@gsc-byte-format ") + format +
                "\n;@gsc-schema early\n*end\n");
            auto noStrings = GscFile::fromBytes(empty, "CP932");
            require(noStrings.strings.empty(), "Empty GSC gained a string");
            const auto saved = noStrings.toBytes();
            require(GscFile::fromBytes(saved, "CP932").strings.empty(), "Empty GSC cannot be rebuilt");
        }
        gsc.strings[0] = u8"😀";
        rejects([&] { gsc.save(file.string()); });
        require(readFile(file) == original, "Encoding error overwrote the original GSC");
        gsc.strings[0] = std::string("a\0b", 3);
        rejects([&] { gsc.toBytes(); });
        std::vector<uint8_t> bytes(4);
        BigEndianReader reader(bytes);
        reader.readByte();
        rejects([&] { reader.readBytes(std::numeric_limits<size_t>::max()); });
        BigEndianWriter writer;
        writer.writeInt32(0);
        rejects([&] { writer.patchInt32(std::numeric_limits<size_t>::max(), 0); });
        fs::remove(file);
    } catch (const std::exception& error) {
        fs::remove(file);
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
