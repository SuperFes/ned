#include "SanitizerOutputParser.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>

namespace ned::editor {

namespace {

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

    std::string_view Trim(std::string_view s) {
        while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) != 0)) {
            s.remove_prefix(1);
        }
        while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) != 0)) {
            s.remove_suffix(1);
        }
        return s;
    }

    bool AllDigits(std::string_view s) {
        return !s.empty() && std::ranges::all_of(s, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
    }

    std::optional<std::size_t> ParseSize(std::string_view s) {
        std::size_t value    = 0;
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
        if (ec != std::errc() || ptr != s.data() + s.size()) {
            return std::nullopt;
        }
        return value;
    }

    struct Location {
        std::string file;
        std::size_t line   = 0;
        std::size_t column = 0;
    };

    // token carries no embedded whitespace -- "path:line" or "path:line:col",
    // both numeric groups fully digits. Tries the three-part shape first,
    // falling back to two-part.
    std::optional<Location> ParseLocationToken(std::string_view token) {
        const std::size_t colon2 = token.rfind(':');
        if (colon2 == std::string_view::npos || colon2 == 0) {
            return std::nullopt;
        }
        const std::string_view afterColon2 = token.substr(colon2 + 1);
        if (!AllDigits(afterColon2)) {
            return std::nullopt;
        }

        const std::size_t colon1 = token.rfind(':', colon2 - 1);
        if (colon1 != std::string_view::npos) {
            const std::string_view mid = token.substr(colon1 + 1, colon2 - colon1 - 1);
            if (AllDigits(mid)) {
                const std::optional<std::size_t> lineVal = ParseSize(mid);
                const std::optional<std::size_t> colVal  = ParseSize(afterColon2);
                if (lineVal && *lineVal > 0) {
                    return Location{.file = std::string(token.substr(0, colon1)), .line = *lineVal, .column = colVal.value_or(0)};
                }
            }
        }

        const std::optional<std::size_t> lineVal = ParseSize(afterColon2);
        if (lineVal && *lineVal > 0) {
            return Location{.file = std::string(token.substr(0, colon2)), .line = *lineVal, .column = 0};
        }
        return std::nullopt;
    }

    // "<file>:<line>[:<col>]: runtime error: <message>" -- UBSan's own
    // per-check diagnostic line, printed immediately above its SUMMARY line.
    bool TryParseRuntimeErrorLine(std::string_view line, Location& outLocation, std::string_view& outMessage) {
        static constexpr std::string_view kMarker = ": runtime error: ";
        const std::size_t                 pos     = line.find(kMarker);
        if (pos == std::string_view::npos) {
            return false;
        }
        const std::optional<Location> loc = ParseLocationToken(line.substr(0, pos));
        if (!loc) {
            return false;
        }
        outLocation = *loc;
        outMessage  = line.substr(pos + kMarker.size());
        return true;
    }

    struct SummaryLine {
        std::string tool;
        std::string message;
        std::string symbol;
        Location    location;
        bool        hasLocation = false;
    };

    // "SUMMARY: <Tool>: <kind> [<file>:<line>[:<col>] in <symbol>]" -- every
    // sanitizer runtime's own shared report-terminator line.
    bool TryParseSummaryLine(std::string_view line, SummaryLine& out) {
        static constexpr std::string_view kPrefix = "SUMMARY: ";
        if (!line.starts_with(kPrefix)) {
            return false;
        }
        const std::string_view rest      = line.substr(kPrefix.size());
        const std::size_t      toolColon = rest.find(": ");
        if (toolColon == std::string_view::npos) {
            return false;
        }
        out.tool                  = std::string(rest.substr(0, toolColon));
        const std::string_view rest2 = rest.substr(toolColon + 2);

        const std::size_t inPos = rest2.rfind(" in ");
        if (inPos != std::string_view::npos) {
            const std::string_view beforeIn = rest2.substr(0, inPos);
            const std::size_t      lastSpace = beforeIn.find_last_of(" \t");
            const std::string_view lastToken = lastSpace == std::string_view::npos ? beforeIn : beforeIn.substr(lastSpace + 1);
            if (const std::optional<Location> loc = ParseLocationToken(lastToken)) {
                out.location    = *loc;
                out.hasLocation = true;
                out.symbol      = std::string(Trim(rest2.substr(inPos + 4)));
                out.message     = std::string(lastSpace == std::string_view::npos ? std::string_view{} : Trim(beforeIn.substr(0, lastSpace)));
                return true;
            }
        }

        out.message = std::string(Trim(rest2));
        return true;
    }

} // namespace

std::vector<SanitizerFinding> ParseSanitizerOutput(std::string_view output) {
    std::vector<SanitizerFinding>       findings;
    const std::vector<std::string_view> lines = SplitLines(output);

    for (std::size_t i = 0; i < lines.size(); ++i) {
        SummaryLine summary;
        if (!TryParseSummaryLine(lines[i], summary)) {
            continue;
        }

        // The nearest non-blank preceding line, if it's UBSan's own richer
        // diagnostic for this exact location -- a better message than the
        // SUMMARY line's own terse kind text.
        if (summary.hasLocation) {
            for (std::size_t offset = 1; offset <= 3 && offset <= i; ++offset) {
                const std::string_view candidate = Trim(lines[i - offset]);
                if (candidate.empty()) {
                    continue;
                }
                Location         candidateLoc;
                std::string_view candidateMessage;
                if (TryParseRuntimeErrorLine(candidate, candidateLoc, candidateMessage) && candidateLoc.file == summary.location.file &&
                    candidateLoc.line == summary.location.line) {
                    summary.message = std::string(candidateMessage);
                }
                break;
            }
        }

        SanitizerFinding finding;
        finding.tool    = std::move(summary.tool);
        finding.message = std::move(summary.message);
        finding.symbol  = std::move(summary.symbol);
        if (summary.hasLocation) {
            finding.file   = std::move(summary.location.file);
            finding.line   = summary.location.line;
            finding.column = summary.location.column;
        }
        findings.push_back(std::move(finding));
    }

    return findings;
}

} // namespace ned::editor
