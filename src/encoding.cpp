#include "encoding.h"
#include <stdexcept>
#include <algorithm>
#include <cctype>
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

std::string normalizeEncodingName(const std::string& encoding) {
    std::string lower = encoding;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "cp932" || lower == "932" || lower == "windows-31j" ||
        lower == "shift_jis" || lower == "shift-jis" || lower == "sjis" ||
        lower == "shiftjis" || lower == "ms932")
        return "CP932";
    if (lower == "gbk" || lower == "gb2312" || lower == "gb18030") return "GBK";
    if (lower == "cp1251" || lower == "windows-1251" || lower == "cyrillic")
        return "CP1251";
    if (lower == "utf-8" || lower == "utf8") return "UTF-8";
    return encoding;
}

#ifdef _WIN32

// ---- Windows implementation using MultiByteToWideChar / WideCharToMultiByte ----

static UINT codePageFromName(const std::string& name) {
    const std::string canonical = normalizeEncodingName(name);
    if (canonical == "CP932") return 932;
    if (canonical == "GBK") return 936;
    if (canonical == "CP1251") return 1251;
    if (canonical == "UTF-8") return CP_UTF8;
    throw std::runtime_error("Unsupported encoding: " + name);
}

std::string convertEncoding(const std::string& input,
                            const std::string& fromEnc,
                            const std::string& toEnc) {
    if (input.empty()) return {};
    const std::string canonicalFrom = normalizeEncodingName(fromEnc);
    const std::string canonicalTo = normalizeEncodingName(toEnc);
    UINT cpFrom = codePageFromName(canonicalFrom);
    UINT cpTo   = codePageFromName(canonicalTo);

    // Convert source → UTF-16 (wide)
    int wlen = MultiByteToWideChar(cpFrom, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (wlen == 0) throw std::runtime_error("MultiByteToWideChar failed for " + canonicalFrom);
    std::vector<wchar_t> wide(wlen);
    MultiByteToWideChar(cpFrom, 0, input.data(), static_cast<int>(input.size()), wide.data(), wlen);

    // Convert UTF-16 → destination
    int mlen = WideCharToMultiByte(cpTo, 0, wide.data(), wlen, nullptr, 0, nullptr, nullptr);
    if (mlen == 0) throw std::runtime_error("WideCharToMultiByte failed for " + canonicalTo);
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

    const std::string canonicalFrom = normalizeEncodingName(fromEnc);
    const std::string canonicalTo = normalizeEncodingName(toEnc);

    iconv_t cd = iconv_open(canonicalTo.c_str(), canonicalFrom.c_str());
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        throw std::runtime_error(std::string("iconv_open failed: ") +
                                 std::strerror(errno) +
                                 " (" + canonicalFrom + " -> " + canonicalTo + ")");
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
