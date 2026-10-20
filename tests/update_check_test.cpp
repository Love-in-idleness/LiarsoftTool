#include "update_check.h"

#include <iostream>
#include <stdexcept>

int main() {
    try {
        const std::string prefix = "https://github.com/Love-in-idleness/LiarsoftTool/releases/tag/";
        const std::string current = liarsoft::CurrentVersion;
        const std::string future = "v" + std::to_string(std::stoul(current) + 8) + ".0.0";
        for (const auto& item : {std::pair<std::string, bool>{current, false},
                                {"v" + current, false}, {"v0.0.0", false}, {future, true}}) {
            if (liarsoft::updateInfoFromReleaseUrl(prefix + item.first).available != item.second)
                throw std::runtime_error("Incorrect numeric version comparison");
        }
        if (liarsoft::updateInfoFromReleaseUrl(prefix + "v" + current).latestVersion != current)
            throw std::runtime_error("Version display includes tag prefix");
        for (const std::string tag : {"", "v", "v2", "v2.2", "v2.2.4.0", "v2.2.4-rc1",
                                     "v2.02.4", "v2.-1.4", "v4294967296.0.0", "v2.2.4?x=1"}) {
            bool rejected = false;
            try { liarsoft::updateInfoFromReleaseUrl(prefix + tag); }
            catch (const std::runtime_error&) { rejected = true; }
            if (!rejected) throw std::runtime_error("Malformed tag accepted: " + tag);
        }
        for (const std::string url : {"http://github.com/Love-in-idleness/LiarsoftTool/releases/tag/v9.0.0",
                                     "https://example.com/releases/tag/v9.0.0", liarsoft::LatestReleaseUrl}) {
            bool rejected = false;
            try { liarsoft::updateInfoFromReleaseUrl(url); }
            catch (const std::runtime_error&) { rejected = true; }
            if (!rejected) throw std::runtime_error("Unrecognized release URL accepted");
        }
        std::cout << "OK: stable version comparison and release URL validation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
