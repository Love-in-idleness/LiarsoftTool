#pragma once

#include <string>

namespace liarsoft {
inline constexpr const char* CurrentVersion = LIARSOFTTOOL_VERSION;
inline constexpr const char* LatestReleaseUrl =
    "https://github.com/Love-in-idleness/LiarsoftTool/releases/latest";

struct UpdateInfo {
    std::string latestVersion;
    bool available;
};

UpdateInfo updateInfoFromReleaseUrl(const std::string& url);
UpdateInfo checkForUpdate();
} // namespace liarsoft
