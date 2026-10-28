#include "pe_image.h"
#include "fileio.h"
#include <algorithm>

namespace liarsoft::runtime {
PeImage PeImage::read(const std::filesystem::path& path) { return PeImage(readFile(path)); }

PeImage::PeImage(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {
    const auto number = [&](size_t pos, size_t width) -> uint32_t {
        checkRange(pos, width, bytes_.size());
        uint32_t value = 0;
        for (size_t i = 0; i < width; ++i) value |= uint32_t(bytes_[pos + i]) << (8 * i);
        return value;
    };
    if (number(0, 2) != 0x5a4d) throw std::runtime_error("Invalid PE DOS header");
    const size_t nt = number(60, 4);
    if (nt < 64 || number(nt, 4) != 0x4550) throw std::runtime_error("Invalid PE signature");
    checkRange(nt, 24, bytes_.size());
    machine = uint16_t(number(nt + 4, 2));
    const auto count = number(nt + 6, 2), optionalLength = number(nt + 20, 2);
    dll = (number(nt + 22, 2) & 0x2000) != 0;
    const size_t opt = nt + 24;
    checkRange(opt, optionalLength, bytes_.size());
    const auto magic = number(opt, 2);
    if (magic != 0x10b && magic != 0x20b) throw std::runtime_error("Unsupported PE optional header");
    wide = magic == 0x20b;
    const size_t directory = wide ? 112 : 96;
    if (optionalLength < directory) throw std::runtime_error("Truncated PE optional header");
    if ((machine == 0x14c && wide) || (machine == 0x8664 && !wide))
        throw std::runtime_error("Inconsistent PE architecture");
    size = number(opt + 56, 4);
    headers_ = number(opt + 60, 4);
    const auto entries = number(opt + directory - 4, 4);
    if (entries > 16 || entries > (optionalLength - directory) / 8 ||
        !size || headers_ > size || headers_ > bytes_.size() || opt + optionalLength > headers_)
        throw std::runtime_error("Invalid PE header extent");
    checkRange(opt + optionalLength, uint64_t(count) * 40, headers_);
    for (size_t i = 0; i < std::min<size_t>(entries, 3); ++i) {
        directories_[i][0] = number(opt + directory + i * 8, 4);
        directories_[i][1] = number(opt + directory + i * 8 + 4, 4);
        checkRange(directories_[i][0], directories_[i][1], size);
    }
    for (size_t i = 0; i < count; ++i) {
        const auto pos = opt + optionalLength + i * 40;
        const auto* name = reinterpret_cast<const char*>(bytes_.data() + pos);
        size_t n = 0;
        while (n < 8 && name[n]) ++n;
        PeSection section{std::string(name, n), number(pos + 12, 4), number(pos + 8, 4),
                          number(pos + 20, 4), number(pos + 16, 4), number(pos + 36, 4)};
        checkRange(section.offset, section.rawSize, bytes_.size());
        checkRange(section.rva, std::max(section.rawSize, section.virtualSize), size);
        const auto span = std::max(section.rawSize, section.virtualSize);
        if (span && section.rva < headers_) throw std::runtime_error("PE section overlaps headers");
        for (const auto& prior : sections)
            if (span && std::max(prior.rawSize, prior.virtualSize) &&
                section.rva < uint64_t(prior.rva) + std::max(prior.rawSize, prior.virtualSize) &&
                prior.rva < uint64_t(section.rva) + span)
                throw std::runtime_error("Overlapping PE sections");
        sections.push_back(std::move(section));
    }
}

const uint8_t* PeImage::range(size_t rva, size_t length) const {
    checkRange(rva, length, size);
    if (rva < headers_) {
        checkRange(rva, length, headers_);
        return bytes_.data() + rva;
    }
    for (const auto& section : sections) {
        if (rva < section.rva || rva - section.rva >= section.rawSize) continue;
        checkRange(rva - section.rva, length, section.rawSize);
        return bytes_.data() + section.offset + rva - section.rva;
    }
    throw std::runtime_error("PE RVA has no file-backed data");
}

std::string PeImage::string(size_t rva) const {
    std::string output;
    while (rva < size) {
        const char ch = char(*range(rva++, 1));
        if (!ch) return output;
        output += ch;
    }
    throw std::runtime_error("Unterminated PE string");
}

std::vector<PeImport> PeImage::imports() const {
    return walkImports(*this, directories_[1][0], directories_[1][1], wide);
}

bool PeImage::exports(const std::string& name) const {
    const size_t rva = directories_[0][0], length = directories_[0][1];
    if (!rva && !length) return false;
    if (!rva || length < 40) throw std::runtime_error("Invalid PE export directory");
    range(rva, 40);
    const auto count = value<uint32_t>(rva + 24), table = value<uint32_t>(rva + 32);
    if (count > size / 4) throw std::runtime_error("Invalid PE export name count");
    range(table, size_t(count) * 4);
    bool found = false;
    for (size_t i = 0; i < count; ++i)
        if (string(value<uint32_t>(table + i * 4)) == name) found = true;
    return found;
}

std::vector<uint8_t> PeImage::sectionBytes(const PeSection& section) const {
    const size_t length = std::min(section.rawSize, section.virtualSize);
    checkRange(section.offset, length, bytes_.size());
    return {bytes_.begin() + section.offset, bytes_.begin() + section.offset + length};
}
}
