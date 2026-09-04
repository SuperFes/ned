#include "MassifOutputParser.h"

#include <charconv>
#include <cctype>
#include <optional>

namespace ned::editor {

namespace {

    std::string_view Trim(std::string_view s) {
        while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) != 0)) {
            s.remove_prefix(1);
        }
        while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) != 0)) {
            s.remove_suffix(1);
        }
        return s;
    }

    std::vector<std::string_view> SplitLines(std::string_view text) {
        std::vector<std::string_view> lines;
        std::size_t                   start = 0;
        while (start <= text.size()) {
            const std::size_t newline = text.find('\n', start);
            if (newline == std::string_view::npos) {
                lines.push_back(text.substr(start));
                break;
            }
            lines.push_back(text.substr(start, newline - start));
            start = newline + 1;
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

    std::optional<int> ParseInt(std::string_view s) {
        int        value      = 0;
        const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
        if (ec != std::errc() || ptr != s.data() + s.size()) {
            return std::nullopt;
        }
        return value;
    }

    // True (and sets value) iff `line` is `key=...` for the given key --
    // massif.out's own snapshot `key=value` fields carry no surrounding
    // whitespace around `=` in practice, but this trims defensively either
    // side anyway.
    bool MatchField(std::string_view line, std::string_view key, std::string_view& value) {
        if (line.size() <= key.size() || !line.starts_with(key) || line[key.size()] != '=') {
            return false;
        }
        value = Trim(line.substr(key.size() + 1));
        return true;
    }

    // massif.out's three header lines (desc/cmd/time_unit) use a distinct
    // `key: value` shape -- a colon, not `=` -- from every snapshot field.
    bool MatchHeaderField(std::string_view line, std::string_view key, std::string_view& value) {
        if (line.size() <= key.size() || !line.starts_with(key) || line[key.size()] != ':') {
            return false;
        }
        value = Trim(line.substr(key.size() + 1));
        return true;
    }

} // namespace

MassifProfile ParseMassifOutput(std::string_view output) {
    MassifProfile profile;

    MassifSnapshot current;
    bool           hasPending = false;

    auto commitPending = [&]() {
        if (hasPending) {
            profile.snapshots.push_back(current);
            hasPending = false;
        }
    };

    for (const std::string_view rawLine : SplitLines(output)) {
        const std::string_view line = Trim(rawLine);
        if (line.empty() || line.front() == '#') {
            continue;
        }

        std::string_view value;
        if (MatchHeaderField(line, "desc", value)) {
            profile.desc = std::string(value);
        }
        else if (MatchHeaderField(line, "cmd", value)) {
            profile.cmd = std::string(value);
        }
        else if (MatchHeaderField(line, "time_unit", value)) {
            profile.timeUnit = std::string(value);
        }
        else if (MatchField(line, "snapshot", value)) {
            commitPending(); // malformed input missing heap_tree= for the prior snapshot -- keep it anyway
            current    = MassifSnapshot{};
            current.index = ParseInt(value).value_or(-1);
            hasPending = true;
        }
        else if (hasPending && MatchField(line, "time", value)) {
            current.time = ParseSize(value).value_or(0);
        }
        else if (hasPending && MatchField(line, "mem_heap_B", value)) {
            current.heapBytes = ParseSize(value).value_or(0);
        }
        else if (hasPending && MatchField(line, "mem_heap_extra_B", value)) {
            current.heapExtraBytes = ParseSize(value).value_or(0);
        }
        else if (hasPending && MatchField(line, "mem_stacks_B", value)) {
            current.stacksBytes = ParseSize(value).value_or(0);
        }
        else if (hasPending && MatchField(line, "heap_tree", value)) {
            current.isPeak     = (value == "peak");
            current.isDetailed = current.isPeak || (value == "detailed");
            commitPending(); // heap_tree is always the last scalar field in a snapshot's preamble
        }
        // Anything else -- an allocation-tree `nN: ...` line following a
        // non-empty heap_tree -- is deliberately not parsed; falls through.
    }
    commitPending(); // EOF reached with a snapshot preamble but no heap_tree= line

    return profile;
}

} // namespace ned::editor
