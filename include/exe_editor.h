#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace liarsoft {
using ExeCancel = std::function<bool()>;
struct ExeRules {
    std::string noLineStart, noLineEnd;
};
struct ExeText {
    std::string id, text, reason;
    uint32_t rva = 0;
    std::vector<size_t> references;
    size_t capacity = 0; // Nonzero for verified fixed-width configuration/table fields.
    std::string role;
    bool editable = false;
};
struct ExeClassifier {
    uint32_t entry, normal, noStart, noEnd;
    std::vector<uint8_t> original;
    unsigned packedPairs = 0;
};
struct ExeFontBridge {
    uint32_t entry, slot, target = 0, charset = 0;
};
struct ExeDocument {
    std::vector<uint8_t> bytes;
    std::string sourceEncoding;
    std::vector<uint8_t> charsets;
    std::vector<size_t> fontOperands;
    std::vector<ExeText> texts;
    std::vector<ExeFontBridge> fontBridges;
    std::vector<ExeClassifier> classifiers;
    ExeRules rules;
    std::vector<std::string> notices;
    bool writable = true, expandedRules = false, fontsComplete = true;
    size_t fontCalls = 0;
};
struct ExeChanges {
    std::string encoding;
    std::map<std::string, std::string> texts;
    std::optional<ExeRules> rules;
};
const std::vector<std::string> &exeEncodings();
ExeChanges defaultExeConfiguration(const ExeDocument &document, const std::string &encoding);
ExeRules defaultExeRules(const std::string &encoding);
ExeRules readExeRules(const std::filesystem::path &path);
ExeDocument inspectExe(const std::vector<uint8_t> &bytes, const std::string &sourceEncoding = "auto",
                       const ExeCancel &cancel = {});
ExeDocument inspectExeFile(const std::filesystem::path &path, const std::string &sourceEncoding = "auto",
                           const ExeCancel &cancel = {});
std::vector<uint8_t> editExe(const ExeDocument &document, const ExeChanges &changes,
                             const ExeCancel &cancel = {});
void saveEditedExe(const std::filesystem::path &source, const std::filesystem::path &output,
                   const ExeDocument &document, const ExeChanges &changes, const ExeCancel &cancel = {});
std::string describeExe(const ExeDocument &document);
} // namespace liarsoft
