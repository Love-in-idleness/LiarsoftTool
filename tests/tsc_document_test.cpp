#include "gsc_decompiler.h"
#include <iostream>
#include <stdexcept>
using namespace liarsoft;
static void require(bool test) { if (!test) throw std::runtime_error("TSC document inspection failed"); }
int main() {
    const std::string source = ";@gsc-byte-format legacy-28\r\n;@gsc-schema early\r\n"
        "; 保留注释😀\r\n:start\r\n*TXT 0 0 0 0 \"\" \"中文😀; \\\"quote\\\"\" 1 ; trailing\r\n"
        "*goto start\r\n*opcode 84 0 0\r\n*end";
    const auto doc = inspectTsc(source);
    require(doc.diagnostics.empty() && doc.byteFormat == "legacy-28" && doc.schema == "early");
    require(doc.lines.size() == 8 && doc.lines.back().end == source.size());
    require(doc.lines[2].kind == "comment" && doc.lines[3].kind == "label");
    const auto& text = doc.lines[4].tokens.at(6);
    require(text.value == "中文😀; \"quote\"" && text.quoted && text.role == "string");
    require(source.substr(text.start, text.end - text.start) == "\"中文😀; \\\"quote\\\"\"");
    require(doc.lines[5].tokens[1].role == "target" && doc.lines[6].opcode == 84);
    require(!checkTsc(source, "CP932").empty());
    for (const auto& bad : {source + "\n*missing", source + "\n*goto nowhere", std::string("\xff"), std::string()})
        require(!inspectTsc(bad).diagnostics.empty());
    const auto partial = inspectTsc(";@gsc-byte-format modern-36\n;@gsc-schema modern\n*wait abc\n");
    require(partial.diagnostics.size() == 1 && partial.diagnostics[0].line == 3);
    require(partial.lines.size() == 3);
    std::cout << "OK: shared parser, lossless byte spans, CRLF, Unicode, labels and partial diagnostics\n";
}
