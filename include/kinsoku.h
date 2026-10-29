#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace liarsoft::runtime {

struct KinsokuRules {
    std::u16string noLineStart, noLineEnd;
    uint32_t classify(char16_t character) const;
};

KinsokuRules parseKinsoku(std::string_view json);

}
