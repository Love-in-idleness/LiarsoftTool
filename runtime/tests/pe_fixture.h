#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct PeFixture {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x3400);
    size_t next = 0x1800;
    unsigned imports = 0;
    void put(size_t offset, uint32_t value, size_t width = 4) {
        for (size_t i = 0; i < width; ++i) bytes.at(offset + i) = uint8_t(value >> (i * 8));
    }
    size_t file(size_t rva) const { return rva - 0x1000 + 0x400; }
    void text(size_t rva, const std::string& value) {
        std::copy(value.begin(), value.end(), bytes.begin() + file(rva));
    }
    explicit PeFixture(bool dll = false) {
        put(0, 0x5a4d, 2); put(60, 0x80); put(0x80, 0x4550);
        put(0x84, 0x14c, 2); put(0x86, 1, 2); put(0x94, 224, 2);
        put(0x96, dll ? 0x2102 : 0x102, 2); put(0x98, 0x10b, 2);
        put(0x98 + 56, 0x4000); put(0x98 + 60, 0x400); put(0x98 + 92, 16);
        const size_t section = 0x178;
        std::memcpy(bytes.data() + section, ".data", 5);
        put(section + 8, 0x3000); put(section + 12, 0x1000);
        put(section + 16, 0x3000); put(section + 20, 0x400);
        put(section + 36, 0xe0000040);
    }
    void addImport(const std::string& name) {
        put(0x98 + 104, 0x1000); put(0x98 + 108, 0x400);
        const auto descriptor = file(0x1000 + imports++ * 20);
        text(next, "kernel32.dll"); put(descriptor + 12, uint32_t(next)); next += 64;
        put(descriptor, uint32_t(next)); put(file(next), uint32_t(next + 32)); next += 16;
        put(descriptor + 16, uint32_t(next)); put(file(next), uint32_t(next + 16)); next += 16;
        text(next + 2, name); next += 128;
    }
    void exportName(const std::string& name) {
        put(0x98 + 96, 0x2800); put(0x98 + 100, 0x100);
        put(file(0x2800) + 24, 1); put(file(0x2800) + 32, 0x2840);
        put(file(0x2840), 0x2880); text(0x2880, name);
    }
};
