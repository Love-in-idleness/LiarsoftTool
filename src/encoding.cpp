#include "encoding.h"
#include <stdexcept>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <stringapiset.h>
#else
#include <iconv.h>
#include <cerrno>
#endif

namespace liarsoft {

#ifdef _WIN32

// ---- Windows implementation using MultiByteToWideChar / WideCharToMultiByte ----

static UINT codePageFromName(const std::string& name) {
    if (name == "SHIFT_JIS" || name == "SHIFT-JIS" || name == "SJIS") return 932;
    if (name == "GBK" || name == "GB2312" || name == "GB18030") return 936;
    if (name == "UTF-8" || name == "UTF8") return CP_UTF8;
    throw std::runtime_error("Unsupported encoding: " + name);
}

std::string convertEncoding(const std::string& input,
                            const std::string& fromEnc,
                            const std::string& toEnc) {
    if (input.empty()) return {};
    UINT cpFrom = codePageFromName(fromEnc);
    UINT cpTo   = codePageFromName(toEnc);

    // Convert source → UTF-16 (wide)
    int wlen = MultiByteToWideChar(cpFrom, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (wlen == 0) throw std::runtime_error("MultiByteToWideChar failed for " + fromEnc);
    std::vector<wchar_t> wide(wlen);
    MultiByteToWideChar(cpFrom, 0, input.data(), static_cast<int>(input.size()), wide.data(), wlen);

    // Convert UTF-16 → destination
    int mlen = WideCharToMultiByte(cpTo, 0, wide.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (mlen == 0) throw std::runtime_error("WideCharToMultiByte failed for " + toEnc);
    std::vector<char> multi(mlen);
    WideCharToMultiByte(cpTo, 0, wide.data(), wlen, multi.data(), mlen, nullptr, nullptr);

    return std::string(multi.data(), mlen);
}

#else

// ---- Unix implementation using iconv ----

std::string convertEncoding(const std::string& input,
                            const std::string& fromEnc,
                            const std::string& toEnc) {
    if (input.empty()) return {};

    iconv_t cd = iconv_open(toEnc.c_str(), fromEnc.c_str());
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        throw std::runtime_error(std::string("iconv_open failed: ") +
                                 std::strerror(errno) +
                                 " (" + fromEnc + " -> " + toEnc + ")");
    }

    std::string output;
    char* inBuf = const_cast<char*>(input.data());
    size_t inLeft = input.size();

    while (inLeft > 0) {
        size_t outSize = (inLeft + 1) * 4;
        std::vector<char> outVec(outSize);
        char* outPtr = outVec.data();
        size_t outLeft = outSize;

        size_t ret = iconv(cd, &inBuf, &inLeft, &outPtr, &outLeft);
        size_t converted = outSize - outLeft;

        if (ret == static_cast<size_t>(-1)) {
            if (errno == E2BIG) {
                output.append(outVec.data(), converted);
                continue;
            }
            if (errno == EILSEQ || errno == EINVAL) {
                output.append(outVec.data(), converted);
                output += '?';
                inBuf++;
                inLeft--;
                continue;
            }
            iconv_close(cd);
            throw std::runtime_error(std::string("iconv conversion error: ") +
                                     std::strerror(errno));
        }
        output.append(outVec.data(), converted);
    }

    iconv_close(cd);
    return output;
}

#endif

} // namespace liarsoft
