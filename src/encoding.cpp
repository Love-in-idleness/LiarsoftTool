#include "encoding.h"
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <stringapiset.h>
#else
#include <iconv.h>
#include <cerrno>
#endif

namespace liarsoft {

const std::vector<std::string>& supportedEncodings() {
    static const std::vector<std::string> names = {
        "CP932", "GBK", "CP1251", "UTF-8", "GB18030", "CP950", "CP949",
        "CP1250", "CP1252", "CP1253", "CP1254", "CP1255", "CP1256", "CP1257", "CP1258"
    };
    return names;
}

std::string normalizeEncodingName(const std::string& encoding) {
    std::string lower = encoding;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    lower.erase(std::remove_if(lower.begin(), lower.end(),
        [](char c) { return c == '-' || c == '_' || c == ' '; }), lower.end());
    if (lower == "auto") return "auto";
    if (lower == "cp932" || lower == "932" || lower == "windows31j" ||
        lower == "windows932" || lower == "shiftjis" || lower == "sjis" || lower == "ms932")
        return "CP932";
    if (lower == "gbk" || lower == "gb2312" || lower == "cp936" ||
        lower == "windows936" || lower == "ms936" || lower == "936") return "GBK";
    if (lower == "gb18030" || lower == "cp54936" || lower == "windows54936" || lower == "54936")
        return "GB18030";
    if (lower == "big5" || lower == "cp950" || lower == "windows950" || lower == "ms950" || lower == "950")
        return "CP950";
    if (lower == "uhc" || lower == "xuhc" || lower == "cp949" ||
        lower == "windows949" || lower == "ms949" || lower == "949") return "CP949";
    if (lower == "cyrillic") return "CP1251";
    for (int codePage = 1250; codePage <= 1258; ++codePage) {
        const auto number = std::to_string(codePage);
        if (lower == number || lower == "cp" + number || lower == "windows" + number || lower == "ms" + number)
            return "CP" + number;
    }
    if (lower == "utf8" || lower == "cp65001" || lower == "65001") return "UTF-8";
    return encoding;
}

#ifdef _WIN32

// ---- Windows implementation using MultiByteToWideChar / WideCharToMultiByte ----

static UINT codePageFromName(const std::string& name) {
    const std::string canonical = normalizeEncodingName(name);
    if (canonical == "CP932") return 932;
    if (canonical == "GBK") return 936;
    if (canonical == "GB18030") return 54936;
    if (canonical == "CP950") return 950;
    if (canonical == "CP949") return 949;
    if (canonical.size() == 6 && canonical >= "CP1250" && canonical <= "CP1258")
        return static_cast<UINT>(std::stoi(canonical.substr(2)));
    if (canonical == "UTF-8") return CP_UTF8;
    throw std::runtime_error("Unsupported encoding: " + name);
}

std::string convertEncoding(const std::string& input,
                            const std::string& fromEnc,
                            const std::string& toEnc,
                            bool strict) {
    if (input.empty()) return {};
    const std::string canonicalFrom = normalizeEncodingName(fromEnc);
    const std::string canonicalTo = normalizeEncodingName(toEnc);
    UINT cpFrom = codePageFromName(canonicalFrom);
    UINT cpTo   = codePageFromName(canonicalTo);
    if (input.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Input is too large for Windows encoding conversion");

    // Convert source → UTF-16 (wide)
    const DWORD inputFlags = strict ? MB_ERR_INVALID_CHARS : 0;
    int wlen = MultiByteToWideChar(cpFrom, inputFlags, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (wlen == 0) throw std::runtime_error("MultiByteToWideChar failed for " + canonicalFrom);
    std::vector<wchar_t> wide(wlen);
    if (MultiByteToWideChar(cpFrom, inputFlags, input.data(), static_cast<int>(input.size()), wide.data(), wlen) != wlen)
        throw std::runtime_error("MultiByteToWideChar failed for " + canonicalFrom);

    // Convert UTF-16 → destination
    // UTF-8 and GB18030 only permit 0 or WC_ERR_INVALID_CHARS, not best-fit flags.
    const bool unicodeOutput = cpTo == CP_UTF8 || cpTo == 54936;
    const DWORD outputFlags = unicodeOutput ? (strict ? WC_ERR_INVALID_CHARS : 0) : WC_NO_BEST_FIT_CHARS;
    BOOL usedDefault = FALSE;
    BOOL* defaultResult = unicodeOutput ? nullptr : &usedDefault;
    int mlen = WideCharToMultiByte(cpTo, outputFlags, wide.data(), wlen, nullptr, 0,
                                   nullptr, defaultResult);
    if (mlen == 0) throw std::runtime_error("WideCharToMultiByte failed for " + canonicalTo +
                                          " (Windows error " + std::to_string(GetLastError()) + ")");
    std::vector<char> multi(mlen);
    usedDefault = FALSE;
    if (WideCharToMultiByte(cpTo, outputFlags, wide.data(), wlen, multi.data(), mlen,
                           nullptr, defaultResult) != mlen)
        throw std::runtime_error("WideCharToMultiByte failed for " + canonicalTo);
    if (strict && usedDefault)
        throw std::runtime_error("text cannot be represented in " + canonicalTo +
                                 ": some characters would become '?'");

    return std::string(multi.data(), mlen);
}

#else

// ---- Unix implementation using iconv ----

std::string convertEncoding(const std::string& input,
                            const std::string& fromEnc,
                            const std::string& toEnc,
                            bool strict) {
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
                if (strict) {
                    const size_t bad = input.size() - inLeft;
                    iconv_close(cd);
                    throw std::runtime_error(
                        "text cannot be represented in " + canonicalTo +
                        ": invalid character at input byte " + std::to_string(bad));
                }
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

    // CP1255/CP1258 decoders may retain a final letter to compose a combining mark.
    for (;;) {
        char tail[64];
        char* outPtr = tail;
        size_t outLeft = sizeof(tail);
        const auto result = iconv(cd, nullptr, nullptr, &outPtr, &outLeft);
        output.append(tail, sizeof(tail) - outLeft);
        if (result != static_cast<size_t>(-1)) break;
        if (errno == E2BIG) continue;
        const auto error = std::string("iconv conversion error: ") + std::strerror(errno);
        iconv_close(cd);
        throw std::runtime_error(error);
    }

    iconv_close(cd);
    return output;
}

#endif

} // namespace liarsoft
