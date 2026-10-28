#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace liarsoft::runtime {
enum class CheckSeverity { Info, Pending, Error };
struct CheckIssue { CheckSeverity severity; std::string source, message; };
struct CheckOptions {
    std::filesystem::path game, root, launcher, host, kinsoku;
    std::vector<std::filesystem::path> plugins;
};
struct CheckResult {
    std::vector<CheckIssue> issues;
    size_t scripts = 0, checked = 0, failed = 0;
    size_t reserved = 0, historical = 0, required = 0, remaining = 0;
    bool complete = true, cancelled = false, capacityKnown = false;
    bool ok() const;
    std::string text() const;
};
// Returning false cancels without publishing a successful or complete result.
using CheckProgress = std::function<bool(size_t, size_t, const std::string&)>;
CheckResult checkCompatibility(const CheckOptions& options, bool full,
                               const CheckProgress& progress = {});
}
