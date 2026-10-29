#pragma once
#include <cstdint>
#include <vector>
namespace liarsoft {
// Normalize only complete, known comparison groups in an analysis copy.
void normalizeExeKinsoku(std::vector<uint8_t>& data);
}
