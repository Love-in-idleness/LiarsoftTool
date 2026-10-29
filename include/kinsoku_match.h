#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace liarsoft::runtime {
struct KinsokuBlock {
    size_t offset, firstPrefix, firstLength, normal, noStart, noEnd;
    unsigned packedPairs = 0; // bit 0: ASCII pairs; bit 1: final comparison uses BX.
};
// Match the complete shared classifier, including all branch destinations.
std::vector<KinsokuBlock> findKinsokuBlocks(const uint8_t *bytes, size_t length);
} // namespace liarsoft::runtime
