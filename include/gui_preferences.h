#pragma once
#include <string>

namespace liarsoft::gui {
struct Preferences {
    unsigned theme = 0;
    bool experimentalExe = false;
    std::string language;
};
Preferences loadPreferences();
void savePreferences(const Preferences& preferences);
std::string effectiveLanguage(const std::string& preference);
void initializeTranslations(const std::string& language, const char* executable);
}
