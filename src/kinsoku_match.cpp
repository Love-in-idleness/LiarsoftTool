#include "kinsoku_match.h"
#include <array>
#include <cstring>
#include <stdexcept>
namespace liarsoft::runtime {
namespace {
constexpr uint16_t ascii[] = {0x20, 0x27, 0x22, 0x2c, 0x2e, 0x3f, 0x21, 0x29, 0x7d, 0x5d};
constexpr uint16_t start[] = {0x8140, 0x8141, 0x8142, 0x8143, 0x8144, 0x8148, 0x8149, 0x815b, 0x8160, 0x8163,
                              0x816a, 0x816c, 0x816e, 0x8170, 0x8172, 0x8174, 0x8176, 0x8178, 0x817a, 0x8166,
                              0x8168, 0x82c1, 0x829f, 0x82a1, 0x82a3, 0x82a5, 0x82a7, 0x82e1, 0x82e3, 0x82e5,
                              0x8340, 0x8342, 0x8344, 0x8346, 0x8348, 0x8383, 0x8385, 0x8387};
constexpr uint16_t finish[] = {0x8165, 0x8167, 0x8169, 0x816b, 0x816d, 0x816f,
                               0x8171, 0x8173, 0x8175, 0x8177, 0x8179, 0x814a};

uint16_t word(const uint8_t *bytes) {
    return uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8);
}
int32_t integer(const uint8_t *bytes) {
    int32_t value;
    std::memcpy(&value, bytes, 4);
    return value;
}

bool compare(const uint8_t *bytes, size_t length, size_t &pos, uint16_t value, size_t &target,
             size_t *prefix = nullptr, bool allowBx = false) {
    const auto begin = pos;
    if (pos > length || length - pos < 6)
        return false;
    if (bytes[pos] == 0x66 && bytes[pos + 1] == 0x83 && bytes[pos + 2] == 0xfe &&
        uint16_t(int16_t(int8_t(bytes[pos + 3]))) == value)
        pos += 4;
    else if (length - pos >= 7 && bytes[pos] == 0x66 && bytes[pos + 1] == 0x81 && bytes[pos + 2] == 0xfe &&
             word(bytes + pos + 3) == value)
        pos += 5;
    else if (length - pos >= 10 && bytes[pos] == 0xb8 && word(bytes + pos + 1) == value &&
             !word(bytes + pos + 3) && bytes[pos + 5] == 0x66 && bytes[pos + 6] == 0x3b &&
             bytes[pos + 7] == 0xf0)
        pos += 8;
    else if (allowBx && bytes[pos] == 0x66 && bytes[pos + 1] == 0x3b && bytes[pos + 2] == 0xf3)
        pos += 3;
    else
        return false;
    if (prefix)
        *prefix = pos - begin;
    int64_t destination;
    if (length - pos >= 6 && bytes[pos] == 0x0f && bytes[pos + 1] == 0x84) {
        destination = int64_t(pos) + 6 + integer(bytes + pos + 2);
        pos += 6;
    } else if (length - pos >= 2 && bytes[pos] == 0x74) {
        destination = int64_t(pos) + 2 + int8_t(bytes[pos + 1]);
        pos += 2;
    } else
        return false;
    if (destination < 0 || uint64_t(destination) >= length)
        return false;
    target = size_t(destination);
    return true;
}
} // namespace

std::vector<KinsokuBlock> findKinsokuBlocks(const uint8_t *bytes, size_t length) {
    if (!bytes && length)
        throw std::runtime_error("Invalid classifier range");
    std::vector<KinsokuBlock> result;
    for (size_t offset = 0; offset < length; ++offset) {
        size_t pos = offset, noStart = 0, noEnd = 0, target = 0, prefix = 0;
        if (!compare(bytes, length, pos, ascii[0], noStart, &prefix))
            continue;
        const auto firstLength = pos - offset;
        bool match = true;
        for (size_t i = 1; i < std::size(ascii) && match; ++i)
            match = compare(bytes, length, pos, ascii[i], target) && target == noStart;
        for (auto value : start) {
            if (!match)
                break;
            match = compare(bytes, length, pos, value, target) && target == noStart;
        }
        if (!match)
            continue;
        size_t extra = pos;
        unsigned packedPairs = 0;
        if (compare(bytes, length, extra, 0x213f, target) && target == noStart) {
            if (!compare(bytes, length, extra, 0x3f21, target) || target != noStart)
                continue;
            const bool bx = length - extra >= 3 && bytes[extra] == 0x66 && bytes[extra + 1] == 0x3b &&
                            bytes[extra + 2] == 0xf3;
            if (!compare(bytes, length, extra, 0x2121, target, nullptr, true) || target != noStart)
                continue;
            packedPairs = bx ? 3 : 1;
            pos = extra;
        }
        for (size_t i = 0; i < std::size(finish) && match; ++i) {
            match = compare(bytes, length, pos, finish[i], target);
            if (!i)
                noEnd = target;
            else
                match = match && target == noEnd;
        }
        if (!match || noStart == noEnd || noStart < pos || noEnd < pos || noStart - pos > 256 ||
            noEnd - pos > 256)
            continue;
        result.push_back({offset, prefix, firstLength, pos, noStart, noEnd, packedPairs});
        offset = pos - 1;
    }
    return result;
}

} // namespace liarsoft::runtime
