#include "OutputParser.h"

#include <charconv>
#include <map>
#include <optional>

namespace ned::editor::coverage {

namespace {

    // SanitizerOutputParser.cpp's own SplitLines, duplicated rather than
    // shared (small-helper duplication across translation units is this
    // codebase's own stated precedent -- see ValgrindOutputParser.cpp's own
    // comment on Trim/SplitLines).
    std::vector<std::string_view> SplitLines(std::string_view text) {
        std::vector<std::string_view> lines;
        std::size_t                   start = 0;
        while (start <= text.size()) {
            const std::size_t end  = text.find('\n', start);
            std::string_view  line = end == std::string_view::npos ? text.substr(start) : text.substr(start, end - start);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            lines.push_back(line);
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
        return lines;
    }

    std::optional<std::size_t> ParseSize(std::string_view s) {
        std::size_t value    = 0;
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
        if (ec != std::errc() || ptr != s.data() + s.size()) {
            return std::nullopt;
        }
        return value;
    }

    std::vector<std::string_view> SplitCsv(std::string_view s) {
        std::vector<std::string_view> parts;
        std::size_t                   start = 0;
        for (std::size_t i = 0; i <= s.size(); ++i) {
            if (i == s.size() || s[i] == ',') {
                parts.push_back(s.substr(start, i - start));
                start = i + 1;
            }
        }
        return parts;
    }

    struct PendingBranch {
        std::size_t total = 0;
        std::size_t taken = 0;
    };

    // Flushes the in-progress SF: record (if any) into `report`, merging
    // into an already-present FileCoverage entry for the same path rather
    // than pushing a duplicate -- see this file's own doc comment on why.
    void FinishRecord(std::optional<std::string>& currentPath, std::map<std::size_t, std::size_t>& hits,
                      std::map<std::size_t, PendingBranch>& branches, Report& report) {
        if (!currentPath || hits.empty()) {
            currentPath.reset();
            hits.clear();
            branches.clear();
            return;
        }

        FileCoverage* target = nullptr;
        for (FileCoverage& file : report) {
            if (file.path == *currentPath) {
                target = &file;
                break;
            }
        }
        if (target == nullptr) {
            report.push_back(FileCoverage{.path = *currentPath, .lines = {}});
            target = &report.back();
        }

        std::map<std::size_t, LineCoverage> merged;
        for (const LineCoverage& existing : target->lines) {
            merged[existing.line] = existing;
        }
        for (const auto& [line, count] : hits) {
            merged[line].line = line;
            merged[line].hitCount += count;
        }
        for (const auto& [line, branch] : branches) {
            merged[line].line = line;
            merged[line].branchesTotal += branch.total;
            merged[line].branchesTaken += branch.taken;
        }

        target->lines.clear();
        target->lines.reserve(merged.size());
        for (auto& [line, cov] : merged) {
            target->lines.push_back(cov);
        }

        currentPath.reset();
        hits.clear();
        branches.clear();
    }

} // namespace

Report ParseLcovInfo(std::string_view output) {
    Report report;

    std::optional<std::string>           currentPath;
    std::map<std::size_t, std::size_t>   hits;
    std::map<std::size_t, PendingBranch> branches;

    for (const std::string_view line : SplitLines(output)) {
        if (line.starts_with("SF:")) {
            FinishRecord(currentPath, hits, branches, report); // tolerate a missing end_of_record before a new SF:
            currentPath = std::string(line.substr(3));
        }
        else if (line.starts_with("DA:")) {
            const std::vector<std::string_view> parts = SplitCsv(line.substr(3));
            if (parts.size() >= 2) {
                const std::optional<std::size_t> lineNo = ParseSize(parts[0]);
                const std::optional<std::size_t> count  = ParseSize(parts[1]);
                if (lineNo && count && *lineNo > 0) {
                    hits[*lineNo - 1] += *count; // DA: is 1-based; LineCoverage::line is 0-based
                }
            }
        }
        else if (line.starts_with("BRDA:")) {
            const std::vector<std::string_view> parts = SplitCsv(line.substr(5));
            if (parts.size() >= 4) {
                const std::optional<std::size_t> lineNo = ParseSize(parts[0]);
                if (lineNo && *lineNo > 0) {
                    PendingBranch& branch = branches[*lineNo - 1];
                    branch.total += 1;
                    // "-" means the block containing this branch was never
                    // reached at all -- not taken, same as a numeric 0.
                    if (parts[3] != "-") {
                        if (const std::optional<std::size_t> taken = ParseSize(parts[3]); taken && *taken > 0) {
                            branch.taken += 1;
                        }
                    }
                }
            }
        }
        else if (line == "end_of_record") {
            FinishRecord(currentPath, hits, branches, report);
        }
    }
    FinishRecord(currentPath, hits, branches, report); // tolerate a missing trailing end_of_record

    return report;
}

} // namespace ned::editor::coverage
