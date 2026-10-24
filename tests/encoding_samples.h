#pragma once
#include <string>
#include <utility>
#include <vector>

inline const std::vector<std::pair<std::string, std::string>>& encodingSamples() {
    static const std::vector<std::pair<std::string, std::string>> samples = {
        {"CP932", u8"日本語"}, {"GBK", u8"中文标点。"}, {"CP1251", u8"Русский текст"},
        {"UTF-8", u8"日本語 中文 Русский 😀"}, {"GB18030", u8"中文 😀 𠀀"},
        {"CP950", u8"繁體中文"}, {"CP949", u8"한국어 뷁"},
        {"CP1250", u8"Łódź"}, {"CP1252", u8"Français"}, {"CP1253", u8"Ελληνικά"},
        {"CP1254", u8"Türkçe"}, {"CP1255", u8"עברית"}, {"CP1256", u8"عربي"},
        {"CP1257", u8"Lietuvių"}, {"CP1258", u8"Viêt"}
    };
    return samples;
}
