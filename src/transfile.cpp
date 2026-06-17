#include "transfile.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <iostream>

namespace liarsoft {

// ---- Factory methods ----

TransFile TransFile::fromGsc(const GscFile& gsc) {
    TransFile trans;
    trans.strings = gsc.strings; // already UTF-8
    return trans;
}

TransFile TransFile::fromFile(const std::string& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("Cannot open file: " + path);
    }

    TransFile trans;

    // Read all lines
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(stream, line)) {
        // Remove \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }

    // Parse sections
    std::string jpText;
    size_t offset = 0;
    std::string text;
    bool isJapanese = false;

    while (readSection(lines, offset, text, isJapanese)) {
        if (isJapanese) {
            jpText = text;
        } else {
            // English/translation text
            // Trim to check if empty
            std::string trimmed = text;
            // Remove all whitespace
            trimmed.erase(std::remove_if(trimmed.begin(), trimmed.end(),
                          [](unsigned char c) { return std::isspace(c); }),
                          trimmed.end());

            if (trimmed.empty() && !jpText.empty()) {
                // Keep original Japanese
                trans.strings.push_back(jpText);
            } else {
                trans.strings.push_back(text);
            }
        }
    }

    return trans;
}

// ---- Conversions ----

void TransFile::populate(GscFile& gsc) const {
    size_t count = std::min(strings.size(), gsc.strings.size());
    for (size_t i = 0; i < count; ++i) {
        gsc.strings[i] = strings[i];
    }
}

GscFile TransFile::toGsc(const std::string& refGscPath,
                         const std::string& encoding) const {
    GscFile gsc = GscFile::fromFile(refGscPath, encoding);
    populate(gsc);
    return gsc;
}

// ---- Save ----

void TransFile::save(const std::string& path) const {
    std::ofstream stream(path);
    if (!stream) {
        throw std::runtime_error("Cannot write file: " + path);
    }

    for (const auto& str : strings) {
        stream << "#" << convertToExternal(str) << "\n";
        stream << ">\n"; // blank translation line
    }
}

// ---- Private helpers ----

std::string TransFile::convertToExternal(const std::string& str) {
    std::string result = str;

    // Full-width space → ^t
    // (multi-byte in UTF-8: \xE3\x80\x80)
    size_t pos = 0;
    while ((pos = result.find("\xE3\x80\x80", pos)) != std::string::npos) {
        result.replace(pos, 3, "^t");
        pos += 2;
    }

    // ^n → newline (external format: actual \n in file)
    // In internal format ^n represents newline; in external it's real newline
    // But we write each section as one line with # prefix, so ^n means nothing here
    // Actually in the original code:
    //   ConvertToExternal: "　" → "^t", "^n" → "\n", "^" → "\\"
    //   Since we write one #-line per string, ^n inside it becomes literal \n in the file
    //   The parser reads multi-line sections until the next # or >
    // For simplicity, we keep ^n as-is and handle it in the parser

    // ^ → \ (escape character)
    pos = 0;
    while ((pos = result.find('^', pos)) != std::string::npos) {
        result.replace(pos, 1, "\\");
        pos += 1;
    }

    // Now the ^t that we replaced above needs to be converted:
    // Actually, we already replaced 　→ ^t. Now ^ → \, so ^t → \t.
    // But wait - we need to be careful with order.
    // Original code does: 　→^t, then ^n→\n, then ^→\\
    // So: 　→^t, ^n stays literal \n (which appears as newline in output).
    // Then ^→\\ but this also affects the ^ in ^t since we use it as escape.
    //
    // Actually reading the original C# more carefully:
    //   str.Replace("　", "^t").Replace("^n", "\n").Replace("^", "\\")
    // The order matters! First 　→^t, then ^n→\n, then remaining ^→\
    // So ^t (from 　) becomes \t in the output.
    // And ^n becomes \n in the output (literal newline).
    //
    // In my implementation above, I did 　→^t first, then ^→\ which would turn ^t→\t.
    // Wait, I converted ^ to \ AFTER replacing 　→^t. So ^t → \t. That's correct.

    // Handle ^n → literal newline
    pos = 0;
    while ((pos = result.find("\\n", pos)) != std::string::npos) {
        result.replace(pos, 2, "\n");
        pos += 1;
    }

    return result;
}

std::string TransFile::convertToInternal(const std::string& str) {
    std::string result = str;

    // Strip \r
    result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());

    // Replace leading space with \t (which will become ^t → 　)
    // In the original code: Regex.Replace(str, @"^ ", "\\t", RegexOptions.Multiline)
    // This replaces a space at the beginning of each line with \t
    // We'll handle this line by line
    std::istringstream iss(result);
    std::string line;
    std::vector<std::string> processedLines;
    while (std::getline(iss, line)) {
        if (!line.empty() && line[0] == ' ') {
            line = "\\t" + line.substr(1);
        }
        processedLines.push_back(line);
    }

    // Re-join with \n
    result.clear();
    for (size_t i = 0; i < processedLines.size(); ++i) {
        if (i > 0) result += '\n';
        result += processedLines[i];
    }

    // \ → ^
    size_t pos = 0;
    while ((pos = result.find('\\', pos)) != std::string::npos) {
        result.replace(pos, 1, "^");
        pos += 1;
    }

    // ^t → 　 (full-width space)
    pos = 0;
    while ((pos = result.find("^t", pos)) != std::string::npos) {
        result.replace(pos, 2, "\xE3\x80\x80"); // UTF-8 full-width space
        pos += 3;
    }

    return result;
}

bool TransFile::readSection(const std::vector<std::string>& lines, size_t& offset,
                            std::string& result, bool& isJapanese) {
    bool reading = false;
    result.clear();
    isJapanese = false;
    std::vector<std::string> output;

    for (; offset < lines.size(); ++offset) {
        const std::string& line = lines[offset];

        if (line.empty()) {
            if (reading) {
                output.push_back("");
            }
            continue;
        }

        bool ja = (line[0] == '#');
        bool en = (line[0] == '>');
        bool isSectionStart = ja || en;

        if (isSectionStart) {
            if (reading) {
                break;
            } else {
                isJapanese = ja;
                reading = true;
                // Include the rest of the line (after the marker)
                std::string rest = line.substr(1);
                output.push_back(convertToInternal(rest));
                continue;
            }
        }

        if (reading) {
            output.push_back(convertToInternal(line));
        }
    }

    // Join output with ^n
    for (size_t i = 0; i < output.size(); ++i) {
        if (i > 0) result += "^n";
        result += output[i];
    }

    // Return true if we actually read a section (fixes last-section loss bug in original code)
    return reading;
}

} // namespace liarsoft
