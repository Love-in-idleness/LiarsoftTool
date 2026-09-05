#include "encoding.h"

#include <iostream>
#include <string>

int main() {
    const std::string extension("\xFB\x40", 2); // Windows CP932 extension.
    for (const char* alias : {"CP932", "WINDOWS-31J", "SHIFT_JIS", "sjis"}) {
        const std::string utf8 = liarsoft::convertEncoding(extension, alias, "UTF-8");
        if (utf8.empty() || liarsoft::convertEncoding(utf8, "UTF-8", alias) != extension) {
            std::cerr << "CP932 round-trip failed for " << alias << std::endl;
            return 1;
        }
        if (liarsoft::normalizeEncodingName(alias) != "CP932") {
            std::cerr << "CP932 alias was not normalized: " << alias << std::endl;
            return 1;
        }
    }
    return 0;
}
