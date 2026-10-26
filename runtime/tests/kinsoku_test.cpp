#include "kinsoku.h"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

using liarsoft::runtime::parseKinsoku;
static void require(bool value) { if (!value) throw std::runtime_error("Kinsoku assertion failed"); }
static void rejects(const std::string& json) {
    try { parseKinsoku(json); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Invalid kinsoku JSON was accepted");
}

int main(int argc, char** argv) {
    const auto rules = parseKinsoku(u8R"({"version":1,"no_line_start":"。）」»","no_line_end":"（「«"})");
    for (const auto character : u"。）」»") if (character) require(rules.classify(character) == 1);
    for (const auto character : u"（「«") if (character) require(rules.classify(character) == 2);
    require(rules.classify(u'文') == 0);
    const auto escaped = parseKinsoku(R"({"no_line_start":"\/\"\b\f\n\r\t\u3002","version":1,"no_line_end":"("})");
    require(escaped.classify(u'。') == 1);
    require(escaped.classify(u'(') == 2);
    require(parseKinsoku(R"({"version":1,"no_line_start":"","no_line_end":""})").classify(u'a') == 0);
    for (const auto* json : {
            "", "{}", "[]", "null", R"({"version":2,"no_line_start":"","no_line_end":""})",
            R"({"version":1.0,"no_line_start":"","no_line_end":""})",
            R"({"version":01,"no_line_start":"","no_line_end":""})",
            R"({"version":1,"no_line_start":""})",
            R"({"version":1,"no_line_start":[],"no_line_end":""})",
            R"({"version":1,"no_line_start":"x","no_line_end":"x"})",
            R"({"version":1,"no_line_start":"","no_line_end":"","extra":""})",
            R"({"version":1,"version":1,"no_line_start":"","no_line_end":""})",
            R"({"version":1,"no_line_start":"","no_line_\u0073tart":"","no_line_end":""})",
            R"({"version":1,"no_line_start":"\u0000","no_line_end":""})",
            R"({"version":1,"no_line_start":"\uD800","no_line_end":""})",
            R"({"version":1,"no_line_start":"\uDC00","no_line_end":""})",
            R"({"version":1,"no_line_start":"\uD83D\uDE00","no_line_end":""})",
            R"({"version":1,"no_line_start":"\uFFFFX","no_line_end":""},)",
            R"({"version":1,"no_line_start":"\q","no_line_end":""})",
            R"({"version":1,"no_line_start":"\u123","no_line_end":""})",
            R"({"version":1,"no_line_start":"","no_line_end":"",})",
            R"({"version":1,"no_line_start":"","no_line_end":""} true)",
            u8R"({"version":1,"no_line_start":"😀","no_line_end":""})"}) rejects(json);
    const std::string prefix = R"({"version":1,"no_line_start":")";
    const std::string suffix = R"(","no_line_end":""})";
    for (const auto& invalid : {std::string("\n"), std::string("\xFF", 1), std::string("\xC0\x80", 2),
                               std::string("\xE3\x80", 2), std::string("\xED\xA0\x80", 3)})
        rejects(prefix + invalid + suffix);
    if (argc == 2) {
        std::ifstream input(argv[1], std::ios::binary);
        require(bool(input));
        const auto defaults = parseKinsoku(std::string(std::istreambuf_iterator<char>(input), {}));
        for (const auto character : u"。）、ゃァ”» %〜゛　") if (character) require(defaults.classify(character) == 1);
        for (const auto character : u"（「“«$€№") if (character) require(defaults.classify(character) == 2);
    }
}
