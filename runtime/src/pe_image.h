#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace liarsoft::runtime {

inline void checkRange(uint64_t offset, uint64_t length, uint64_t size) {
    if (offset > size || length > size - offset)
        throw std::runtime_error("PE range is outside the image");
}

struct PeImport { std::string dll, name; size_t slot; uint64_t target; };

// The same traversal reads file RVAs and mapped-image RVAs. The latter still
// validates actual API addresses and memory protections before installing hooks.
template<class Image>
std::vector<PeImport> walkImports(const Image& image, size_t rva, size_t length, bool wide) {
    std::vector<PeImport> result;
    if (!rva && !length) return result;
    if (!rva || length < 20) throw std::runtime_error("Invalid import directory");
    image.range(rva, length);
    const size_t width = wide ? 8 : 4;
    for (size_t offset = 0; offset <= length - 20; offset += 20) {
        const auto descriptor = rva + offset;
        const auto lookup = image.template value<uint32_t>(descriptor);
        const auto dll = image.template value<uint32_t>(descriptor + 12);
        const auto address = image.template value<uint32_t>(descriptor + 16);
        if (!lookup && !dll && !address && !image.template value<uint64_t>(descriptor + 4)) return result;
        if (!lookup || !dll || !address) throw std::runtime_error("Import descriptor has no named lookup table");
        const auto dllName = image.string(dll);
        if (dllName.empty()) throw std::runtime_error("Empty import DLL name");
        for (size_t index = 0;; ++index) {
            if (index > image.size / width) throw std::runtime_error("Unterminated import thunk table");
            const size_t displacement = index * width;
            checkRange(lookup, displacement, image.size);
            checkRange(address, displacement, image.size);
            const auto entry = wide ? image.template value<uint64_t>(lookup + displacement)
                                    : image.template value<uint32_t>(lookup + displacement);
            const size_t slot = address + displacement;
            const auto target = wide ? image.template value<uint64_t>(slot) : image.template value<uint32_t>(slot);
            if (!entry) {
                if (target) throw std::runtime_error("Unterminated import address table");
                break;
            }
            if (!target) throw std::runtime_error("Empty import address slot");
            if (entry & (wide ? uint64_t(1) << 63 : uint64_t(1) << 31)) continue;
            if (entry > UINT32_MAX - 2) throw std::runtime_error("Invalid import name RVA");
            image.range(size_t(entry), 3);
            const auto name = image.string(size_t(entry) + 2);
            if (name.empty()) throw std::runtime_error("Empty imported API name");
            result.push_back({dllName, name, slot, target});
        }
    }
    throw std::runtime_error("Unterminated import descriptor table");
}

struct PeSection {
    std::string name;
    uint32_t rva, virtualSize, offset, rawSize, flags;
};

class PeImage {
public:
    explicit PeImage(std::vector<uint8_t> bytes);
    static PeImage read(const std::filesystem::path& path);
    uint16_t machine = 0;
    bool wide = false, dll = false;
    size_t size = 0;
    std::vector<PeSection> sections;
    const uint8_t* range(size_t rva, size_t length) const;
    std::string string(size_t rva) const;
    template<class T> T value(size_t rva) const {
        T output;
        std::memcpy(&output, range(rva, sizeof(T)), sizeof(T));
        return output;
    }
    std::vector<PeImport> imports() const;
    bool exports(const std::string& name) const;
    std::vector<uint8_t> sectionBytes(const PeSection& section) const;
    const std::vector<uint8_t>& bytes() const { return bytes_; }
private:
    std::vector<uint8_t> bytes_;
    size_t headers_ = 0;
    uint32_t directories_[3][2]{};
};

}
