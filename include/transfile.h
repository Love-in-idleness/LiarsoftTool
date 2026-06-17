#ifndef LIARSOFTTOOL_TRANSFILE_H
#define LIARSOFTTOOL_TRANSFILE_H

#include "gscfile.h"
#include <string>
#include <vector>

namespace liarsoft {

/**
 * Handles conversion between GSC binary files and editable TXT files.
 *
 * TXT format:
 *   Lines starting with '#' mark the original (Japanese) text.
 *   Lines starting with '>' mark the translation.
 *   If the translation line is empty (just ">"), the original text is kept.
 *
 * Internal TXT representation uses:
 *   - '^' as escape character (written as '\' in the external file)
 *   - '^n' for newlines
 *   - '^t' for full-width spaces (converted to/from leading regular spaces)
 *
 * The TXT file itself is always UTF-8.
 */
class TransFile {
public:
    /// The list of strings (in the target encoding).
    std::vector<std::string> strings;

    // ---- Factory methods ----

    /// Create a TransFile by extracting strings from a GSC file.
    static TransFile fromGsc(const GscFile& gsc);

    /// Create a TransFile by parsing a TXT file (UTF-8).
    static TransFile fromFile(const std::string& path);

    // ---- Conversions ----

    /// Populate a reference GSC with the strings from this TransFile.
    /// The reference GSC provides the command section, end sequence, etc.
    void populate(GscFile& gsc) const;

    /// Convert this TransFile to a GSC, using a reference GSC file for structure.
    /// `refGscPath` is the path to the original/reference GSC file.
    /// `encoding` is the encoding for the output GSC strings (e.g. "SHIFT_JIS", "GBK").
    GscFile toGsc(const std::string& refGscPath, const std::string& encoding) const;

    // ---- Save ----

    /// Save to a TXT file (UTF-8).
    void save(const std::string& path) const;

private:
    TransFile() = default;

    /// Convert internal representation to external (for TXT output).
    static std::string convertToExternal(const std::string& str);

    /// Convert external (TXT) representation to internal.
    static std::string convertToInternal(const std::string& str);

    /// Read a section (original or translation) from the TXT lines.
    static bool readSection(const std::vector<std::string>& lines, size_t& offset,
                            std::string& result, bool& isJapanese);
};

} // namespace liarsoft

#endif // LIARSOFTTOOL_TRANSFILE_H
