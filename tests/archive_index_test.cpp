#include "archive_index.h"
#include "fileio.h"
#include "lwg_decoder.h"
#include "xflarchive.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace liarsoft;
namespace fs = std::filesystem;
#define require(condition) do { if (!(condition)) throw std::runtime_error("Archive index check failed: " #condition); } while (false)
template<class F> static void rejected(F action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Invalid archive/range was accepted");
}
static void patch(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) data.at(offset + i) = static_cast<uint8_t>(value >> (8 * i));
}
int main() {
    const auto directory = fs::temp_directory_path() / ("liarsoft-index-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(directory);
    try {
        // Duplicate names and empty entries must remain separate, including LWG metadata.
        std::vector<uint8_t> lwg(28 + 19 * 2 + 3, 0);
        patch(lwg, 0, 0x0001474c); patch(lwg, 4, 600); patch(lwg, 8, 800);
        patch(lwg, 12, 2); patch(lwg, 20, 38);
        patch(lwg, 24, uint32_t(-4)); patch(lwg, 28, 7); lwg[32] = 2;
        patch(lwg, 37, 3); lwg[41] = 1; lwg[42] = 'a';
        patch(lwg, 43, 10); patch(lwg, 47, 20); lwg[60] = 1; lwg[61] = 'a';
        patch(lwg, 62, 3); lwg[66] = 1; lwg[67] = 2; lwg[68] = 3;
        const auto memory = readArchiveIndex(lwg);
        require(memory.format == "LWG" && memory.width == 800 && memory.height == 600);
        require(memory.entries.size() == 2 && memory.entries[0].name == memory.entries[1].name);
        require(memory.entries[0].x == -4 && memory.entries[0].y == 7 && memory.entries[0].flag == 2);
        require(memory.entries[1].size == 0);
        const auto decoded = LwgDecoder::decode(lwg, "CP932");
        require(decoded.entries[0].data == std::vector<uint8_t>({1, 2, 3}));
        XflArchive outer;
        outer.encoding = "CP932";
        outer.entries.push_back({"nested.lwg", lwg});
        outer.entries.push_back({"zempty.lwg", {}});
        auto bytes = outer.toBytes();
        // The writer rejects duplicates, but the read-only browser must retain them.
        std::copy(bytes.begin() + 12, bytes.begin() + 44, bytes.begin() + 52);
        const auto file = directory / "outer.xfl";
        writeFileIfChanged(file.string(), bytes);
        const auto time = fs::last_write_time(file);
        ResourceSource root(file);
        const auto index = readArchiveIndex(root);
        require(index.entries.size() == 2 && index.entries[1].size == 0);
        auto nested = root.subrange(index.entries[0].offset, index.entries[0].size);
        const auto nestedIndex = readArchiveIndex(nested);
        const auto& selected = nestedIndex.entries[0];
        require(nested.subrange(selected.offset, selected.size).readAll() == decoded.entries[0].data);
        require(root.subrange(index.entries[1].offset, 0).readAll().empty());
        require(fs::last_write_time(file) == time && readFile(file) == bytes);
        rejected([&] { root.subrange(root.size(), 1); });
        rejected([&] { root.subrange(UINT64_MAX, 1); });
        rejected([&] { nested.read(0, std::numeric_limits<size_t>::max()); });
        for (size_t size = 0; size < 28; ++size) {
            rejected([&] { readArchiveIndex(std::vector<uint8_t>(lwg.begin(), lwg.begin() + size)); });
        }
        auto broken = lwg; patch(broken, 33, UINT32_MAX);
        rejected([&] { readArchiveIndex(broken); });
        broken = bytes; patch(broken, 4, UINT32_MAX);
        rejected([&] { readArchiveIndex(broken); });
        broken = bytes; patch(broken, 44, UINT32_MAX);
        rejected([&] { readArchiveIndex(broken); });
        // Sparse payload: indexing must not allocate or read the 1 GiB body.
        auto sparseHeader = std::vector<uint8_t>(bytes.begin(), bytes.begin() + 92);
        patch(sparseHeader, 48, 1024 * 1024 * 1024);
        const auto sparse = directory / "large.xfl";
        writeFileIfChanged(sparse.string(), sparseHeader);
        fs::resize_file(sparse, sparseHeader.size() + 1024ULL * 1024 * 1024);
        require(readArchiveIndex(ResourceSource(sparse)).entries[0].size == 1024ULL * 1024 * 1024);
        fs::resize_file(file, 4);
        rejected([&] { nested.readAll(); });
        fs::remove_all(directory);
        std::cout << "OK: lazy nested index, duplicates, empty entries, offsets, sparse payload, no input writes\n";
    } catch (...) { fs::remove_all(directory); throw; }
}
