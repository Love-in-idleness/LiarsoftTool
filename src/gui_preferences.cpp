#include "gui_preferences.h"
#include "gui_common.h"
#include "fileio.h"
#include <glib.h>
#include <libintl.h>
#include <clocale>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace liarsoft::gui {
static std::string settingsPath() {
    return std::string(g_get_user_config_dir()) + "/liarsofttool/settings.ini";
}

Preferences loadPreferences() {
    Preferences result;
    auto* key = g_key_file_new();
    GError* error = nullptr;
    if (g_key_file_load_from_file(key, settingsPath().c_str(), G_KEY_FILE_NONE, &error)) {
        result.theme = g_key_file_get_uint64(key, "Interface", "Theme", nullptr);
        auto* language = g_key_file_get_string(key, "Interface", "Language", nullptr);
        if (language) { result.language = language; g_free(language); }
        if (result.theme > 2) result.theme = 0;
        if (result.language != "en" && result.language != "ru" && result.language != "zh_CN")
            result.language.clear();
    }
    g_key_file_unref(key);
    if (error) {
        const bool absent = g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
        const std::string message = error->message;
        g_error_free(error);
        if (!absent) throw std::runtime_error(message);
    }
    return result;
}

void savePreferences(const Preferences& preferences) {
    auto* key = g_key_file_new();
    g_key_file_set_uint64(key, "Interface", "Theme", preferences.theme);
    g_key_file_set_string(key, "Interface", "Language", preferences.language.c_str());
    auto* text = g_key_file_to_data(key, nullptr, nullptr);
    g_key_file_unref(key);
    const std::string contents(text);
    g_free(text);
    const auto path = nativePathFromUtf8(settingsPath());
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    writeFileIfChanged(path, reinterpret_cast<const uint8_t*>(contents.data()), contents.size());
}

std::string effectiveLanguage(const std::string& preference) {
    if (!preference.empty()) return preference;
    for (const char* const* names = g_get_language_names(); *names; ++names) {
        const std::string name(*names);
        if (name.compare(0, 2, "zh") == 0) return "zh_CN";
        if (name.compare(0, 2, "ru") == 0) return "ru";
        if (name.compare(0, 2, "en") == 0) return "en";
    }
    return "en";
}

void initializeTranslations(const std::string& language, const char* executable) {
    std::setlocale(LC_ALL, "");
    // glibc also suppresses gettext in C.UTF-8; use a message locale when available.
    const auto* locale = std::setlocale(LC_MESSAGES, nullptr);
    if (!locale || std::string(locale).compare(0, 1, "C") == 0 || std::string(locale) == "POSIX") {
        const auto requested = language == "zh_CN" ? "zh_CN.UTF-8" : language == "ru" ? "ru_RU.UTF-8" : "en_US.UTF-8";
        if (!std::setlocale(LC_MESSAGES, requested))
            std::setlocale(LC_MESSAGES, "en_US.UTF-8");
    }
    g_setenv("LANGUAGE", language.c_str(), TRUE);
#ifdef _WIN32
    // libintl reads the CRT environment, not only Win32's environment block.
    _putenv_s("LANGUAGE", language.c_str());
    // On Windows libintl resolves LC_ALL before its emulated LC_MESSAGES locale.
    _putenv_s("LC_ALL", "");
    _putenv_s("LC_MESSAGES", language == "zh_CN" ? "zh_CN.UTF-8" : language == "ru" ? "ru_RU.UTF-8" : "en_US.UTF-8");
    g_unsetenv("LC_ALL");
    g_setenv("LC_MESSAGES", language == "zh_CN" ? "zh_CN.UTF-8" : language == "ru" ? "ru_RU.UTF-8" : "en_US.UTF-8", TRUE);
#endif
    namespace fs = std::filesystem;
    auto directory = fs::absolute(fs::u8path(executable)).parent_path();
    auto catalogs = directory / "locale";
    if (!fs::is_directory(catalogs)) catalogs = directory.parent_path() / "share/locale";
    bindtextdomain("liarsofttool", catalogs.u8string().c_str());
    bind_textdomain_codeset("liarsofttool", "UTF-8");
    textdomain("liarsofttool");
}
}
