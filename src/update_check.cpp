#include "update_check.h"

#include <curl/curl.h>
#include <array>
#include <charconv>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace liarsoft {
namespace {
std::array<unsigned, 3> versionParts(std::string_view version) {
    std::array<unsigned, 3> parts{};
    for (unsigned i = 0; i < parts.size(); ++i) {
        const auto end = version.find('.');
        const auto part = version.substr(0, end);
        if (part.empty() || (part.size() > 1 && part.front() == '0'))
            throw std::runtime_error("Unrecognized release version");
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), parts[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size() ||
            (i < 2) != (end != std::string_view::npos))
            throw std::runtime_error("Unrecognized release version");
        if (end != std::string_view::npos) version.remove_prefix(end + 1);
    }
    return parts;
}
} // namespace

UpdateInfo updateInfoFromReleaseUrl(const std::string& url) {
    constexpr std::string_view prefix =
        "https://github.com/Love-in-idleness/LiarsoftTool/releases/tag/";
    if (url.compare(0, prefix.size(), prefix) != 0)
        throw std::runtime_error("GitHub did not return a recognized release URL");
    std::string version = url.substr(prefix.size());
    if (!version.empty() && version.front() == 'v') version.erase(0, 1);
    return {version, versionParts(version) > versionParts(CurrentVersion)};
}

UpdateInfo checkForUpdate() {
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK) throw std::runtime_error("Could not initialize HTTPS");
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("Could not create HTTPS request");
    auto set = [&](CURLoption option, auto value) {
        const auto result = curl_easy_setopt(curl.get(), option, value);
        if (result != CURLE_OK) throw std::runtime_error(curl_easy_strerror(result));
    };
    char error[CURL_ERROR_SIZE] = {};
    set(CURLOPT_ERRORBUFFER, error);
    set(CURLOPT_URL, LatestReleaseUrl);
    set(CURLOPT_USERAGENT, "LiarsoftTool/" LIARSOFTTOOL_VERSION);
    set(CURLOPT_NOBODY, 1L);
    set(CURLOPT_FOLLOWLOCATION, 1L);
    set(CURLOPT_MAXREDIRS, 5L);
    set(CURLOPT_PROTOCOLS_STR, "https");
    set(CURLOPT_REDIR_PROTOCOLS_STR, "https");
    set(CURLOPT_CONNECTTIMEOUT, 5L);
    set(CURLOPT_TIMEOUT, 15L);
    set(CURLOPT_NOSIGNAL, 1L);
#ifdef _WIN32
    set(CURLOPT_SSL_OPTIONS, long(CURLSSLOPT_NATIVE_CA));
#endif
    const auto result = curl_easy_perform(curl.get());
    if (result != CURLE_OK)
        throw std::runtime_error(std::string("Update check failed: ") +
                                 (error[0] ? error : curl_easy_strerror(result)));
    long status = 0;
    char* url = nullptr;
    if (curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status) != CURLE_OK ||
        curl_easy_getinfo(curl.get(), CURLINFO_EFFECTIVE_URL, &url) != CURLE_OK || !url)
        throw std::runtime_error("Could not read GitHub response");
    if (status != 200)
        throw std::runtime_error("Update check failed: GitHub HTTP " + std::to_string(status));
    return updateInfoFromReleaseUrl(url);
}
} // namespace liarsoft
