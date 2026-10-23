#include "gsc_decompiler.h"
#include <iostream>
#include <stdexcept>

using namespace liarsoft;
namespace {
const std::string header = ";@gsc-byte-format modern-36\n;@gsc-schema modern\n";
void error(const std::string& text, size_t line, const std::string& part) {
    const auto diagnostics = checkTsc(text);
    if (diagnostics.size() != 1 || diagnostics[0].severity != "error" ||
        diagnostics[0].line != line || diagnostics[0].message.find(part) == std::string::npos)
        throw std::runtime_error("Incorrect diagnostic: " + part);
    try { restoreGscFromTsc(text); }
    catch (const std::exception& failure) {
        if (failure.what() == diagnostics[0].message) return;
    }
    throw std::runtime_error("Check and compiler errors differ");
}
}
int main() {
    if (!checkTsc(header + "*end\n").empty()) throw std::runtime_error("Valid TSC rejected");
    error(header + "*missing\n", 3, "unknown command");
    error(header + "\"\"\n", 3, "expected command");
    error(header + "; comment\r\n*missing\r\n", 4, "unknown command");
    error(header + "*end 1\n", 3, "operand count");
    error(header + "*wait abc\n", 3, "numeric operand");
    error(header + "*wait \"1\"\n", 3, "cannot be quoted");
    error(header + "*wait 4294967296\n", 3, "out of range");
    error(header + "*goto absent\n", 3, "unknown label");
    error(header + ":same\n:same\n*end\n", 4, "duplicate label");
    error(header + "*font 1 0 0 0 0 \"unfinished\n", 3, "unterminated string");
    error(header + "*font 1 0 0 0 0 \"bad\\q\"\n", 3, "unsupported string escape");
    error(header + "*font 1 0 0 0 0 \"😀\"\n", 3, "CP932");
    error(header + ";@gsc-schema modern\n*end\n", 3, "duplicate");
    error(";@gsc-byte-format invalid\n;@gsc-schema modern\n*end\n", 1, "byte format");
    error(";@gsc-byte-format legacy-28\n;@gsc-schema modern\n*end\n", 0, "36-byte");
    error("", 0, "metadata");
    error(";@gsc-raw-v1 malformed\n", 1, "malformed");
    std::cout << "OK: compiler-backed TSC checks and honest source positions\n";
}
