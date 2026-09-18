#include "Corpus.h"

#include <algorithm>
#include <cctype>
#include <optional>

#include "Editor/Parse/Sexp.h"

namespace ned::editor::grammar::corpus {

namespace {

    namespace fs = std::filesystem;

    bool IsWordChar(char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    }

    struct Line {
        std::size_t      start; // byte offset of the line's first character
        std::size_t      end;   // byte offset one past the newline (or EOF)
        std::string_view text;  // without the trailing \n / \r\n
    };

    std::vector<Line> SplitLines(std::string_view content) {
        std::vector<Line> lines;
        std::size_t       pos = 0;
        while (pos < content.size()) {
            std::size_t      nl   = content.find('\n', pos);
            std::size_t      end  = nl == std::string_view::npos ? content.size() : nl + 1;
            std::string_view text = content.substr(pos, (nl == std::string_view::npos ? content.size() : nl) - pos);
            if (!text.empty() && text.back() == '\r')
                text.remove_suffix(1);
            lines.push_back({pos, end, text});
            pos = end;
        }
        return lines;
    }

    // A fence is a maximal run of at least three of `marker`; the suffix is
    // the rest of the line (which by maximality cannot begin with the
    // marker). nullopt suffix = none; the reference's suffix capture never
    // matches empty.
    struct Fence {
        std::size_t                     length;
        std::optional<std::string_view> suffix;
    };

    std::optional<Fence> ParseFence(std::string_view line, char marker) {
        std::size_t run = 0;
        while (run < line.size() && line[run] == marker)
            ++run;
        if (run < 3)
            return std::nullopt;
        if (run == line.size())
            return Fence{run, std::nullopt};
        return Fence{run, line.substr(run)};
    }

    // The reference's name/marker line rule: first char is not '=' (a
    // fence-ish line ends the run), or the line is whitespace followed by ':'.
    bool IsNameOrMarkerLine(std::string_view line) {
        if (line.empty())
            return true;
        if (line.front() != '=')
            return true;
        std::size_t i = 0;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])) != 0)
            ++i;
        return i > 0 && i < line.size() && line[i] == ':';
    }

    struct HeaderMatch {
        std::size_t              startOffset; // of the opening fence line
        std::size_t              endOffset;   // one past the closing fence's newline
        std::string              name;
        bool                     skip            = false;
        bool                     error           = false;
        bool                     platformMatches = true;
        std::vector<std::string> languages;
    };

} // namespace

std::string NormalizeExpected(std::string_view raw) {
    std::string noComments;
    noComments.reserve(raw.size());
    std::size_t pos = 0;
    while (pos <= raw.size()) {
        std::size_t      nl   = raw.find('\n', pos);
        std::string_view line = raw.substr(pos, (nl == std::string_view::npos ? raw.size() : nl) - pos);
        std::size_t      ws   = 0;
        while (ws < line.size() && std::isspace(static_cast<unsigned char>(line[ws])) != 0)
            ++ws;
        const bool comment = ws < line.size() && line[ws] == ';';
        if (!comment)
            noComments.append(line);
        if (nl == std::string_view::npos)
            break;
        noComments.push_back('\n');
        pos = nl + 1;
    }
    std::string collapsed;
    collapsed.reserve(noComments.size());
    bool inSpace = false;
    for (char c : noComments) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            inSpace = true;
            continue;
        }
        if (inSpace && !collapsed.empty() && c != ')')
            collapsed.push_back(' ');
        inSpace = false;
        collapsed.push_back(c);
    }
    return collapsed;
}

bool HasFieldSyntax(std::string_view s) {
    // the reference: regex " \w+: \("
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != ' ')
            continue;
        std::size_t j = i + 1;
        while (j < s.size() && IsWordChar(s[j]))
            ++j;
        if (j == i + 1)
            continue;
        if (j + 2 < s.size() && s[j] == ':' && s[j + 1] == ' ' && s[j + 2] == '(')
            return true;
    }
    return false;
}

std::string StripSexpFields(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        if (s[i] == ' ') {
            std::size_t j = i + 1;
            while (j < s.size() && IsWordChar(s[j]))
                ++j;
            if (j > i + 1 && j + 2 < s.size() && s[j] == ':' && s[j + 1] == ' ' && s[j + 2] == '(') {
                out += " (";
                i = j + 3;
                continue;
            }
        }
        out.push_back(s[i++]);
    }
    return out;
}

std::vector<Case> ParseCorpusFile(std::string_view content, const std::string& fileLabel) {
    const std::vector<Line> lines = SplitLines(content);

    // Pass 1: header candidates (fence / >=1 name-marker lines / fence),
    // consumed non-overlapping, exactly as the reference's captures_iter does.
    struct Candidate {
        std::size_t                     startOffset, endOffset;
        std::optional<std::string_view> suffix1, suffix2;
        std::size_t                     nameFirst, nameLast; // line indices of the name/marker block
    };
    std::vector<Candidate> candidates;
    std::size_t            i = 0;
    while (i < lines.size()) {
        const auto open = ParseFence(lines[i].text, '=');
        if (!open) {
            ++i;
            continue;
        }
        std::size_t j          = i + 1;
        bool        blockValid = true;
        while (j < lines.size() && !ParseFence(lines[j].text, '=')) {
            if (lines[j].text.empty()) {
                // The reference's name-line rule is ([^=\r\n]|\s+:)[^\r\n]*
                // and \s matches newlines, so a blank-line run is legal
                // exactly when it is absorbed as the leading whitespace of a
                // following `:attribute` line.
                std::size_t k = j + 1;
                while (k < lines.size() && lines[k].text.empty())
                    ++k;
                std::string_view next = k < lines.size() ? lines[k].text : std::string_view{};
                std::size_t      ws   = 0;
                while (ws < next.size() && std::isspace(static_cast<unsigned char>(next[ws])) != 0)
                    ++ws;
                if (k < lines.size() && !ParseFence(next, '=') && ws < next.size() && next[ws] == ':') {
                    j = k;
                    continue;
                }
                blockValid = false;
                break;
            }
            if (!IsNameOrMarkerLine(lines[j].text)) {
                blockValid = false;
                break;
            }
            ++j;
        }
        const bool haveNames = j > i + 1;
        const auto close     = blockValid && j < lines.size() ? ParseFence(lines[j].text, '=') : std::nullopt;
        if (haveNames && close) {
            candidates.push_back({lines[i].start, lines[j].end, open->suffix, close->suffix, i + 1, j - 1});
            i = j + 1;
        }
        else {
            ++i;
        }
    }
    if (candidates.empty())
        return {};

    // The first candidate's opening suffix governs the whole file.
    const std::optional<std::string_view> fileSuffix = candidates.front().suffix1;

    std::vector<HeaderMatch> headers;
    for (const Candidate& c : candidates) {
        if (c.suffix1 != fileSuffix || c.suffix2 != fileSuffix)
            continue;
        HeaderMatch h;
        h.startOffset   = c.startOffset;
        h.endOffset     = c.endOffset;
        bool seenMarker = false;
        for (std::size_t li = c.nameFirst; li <= c.nameLast; ++li) {
            const std::string_view line    = lines[li].text;
            std::string_view       trimmed = line;
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())) != 0)
                trimmed.remove_prefix(1);
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())) != 0)
                trimmed.remove_suffix(1);
            const std::string_view beforeParen = trimmed.substr(0, trimmed.find('('));
            if (beforeParen == ":skip") {
                seenMarker = true;
                h.skip     = true;
            }
            else if (beforeParen == ":fail-fast") {
                seenMarker = true; // no meaning for a scorecard run
            }
            else if (beforeParen == ":error") {
                seenMarker = true;
                h.error    = true;
            }
            else if (beforeParen == ":platform" && trimmed.size() > 10 && trimmed.back() == ')') {
                seenMarker                   = true;
                const std::string_view value = trimmed.substr(10, trimmed.size() - 11);
                h.platformMatches            = value == "linux";
            }
            else if (beforeParen == ":language" && trimmed.size() > 10 && trimmed.back() == ')') {
                seenMarker = true;
                h.languages.emplace_back(trimmed.substr(10, trimmed.size() - 11));
            }
            else if (!seenMarker) {
                h.name.append(line);
                h.name.push_back('\n');
            }
        }
        while (!h.name.empty() && std::isspace(static_cast<unsigned char>(h.name.back())) != 0)
            h.name.pop_back();
        if (h.skip)
            h.error = false;
        if (h.languages.empty())
            h.languages.emplace_back("");
        headers.push_back(std::move(h));
    }

    // Pass 2: pair consecutive headers; the longest suffix-matching divider
    // line between them splits input from expected (last one on a tie).
    std::vector<Case> cases;
    for (std::size_t hi = 0; hi < headers.size(); ++hi) {
        const HeaderMatch& h          = headers[hi];
        const std::size_t  segmentEnd = hi + 1 < headers.size() ? headers[hi + 1].startOffset : content.size();

        std::optional<Line> divider;
        std::size_t         bestLength = 0;
        for (const Line& line : lines) {
            if (line.start < h.endOffset || line.end > segmentEnd)
                continue;
            const auto fence = ParseFence(line.text, '-');
            if (!fence || fence->suffix != fileSuffix)
                continue;
            const std::size_t matchLength = line.end - line.start;
            if (matchLength >= bestLength) {
                bestLength = matchLength;
                divider    = line;
            }
        }
        if (!divider)
            continue;

        Case item;
        item.name            = h.name;
        item.file            = fileLabel;
        item.skip            = h.skip;
        item.error           = h.error;
        item.platformMatches = h.platformMatches;
        item.languages       = h.languages;
        item.input           = std::string(content.substr(h.endOffset, divider->start - h.endOffset));
        if (!item.input.empty() && item.input.back() == '\n')
            item.input.pop_back();
        item.expectedStart = divider->end;
        item.expectedEnd   = segmentEnd;
        item.expected      = NormalizeExpected(content.substr(divider->end, segmentEnd - divider->end));
        item.hasFields     = HasFieldSyntax(item.expected);
        cases.push_back(std::move(item));
    }
    return cases;
}

std::string ActualSexp(const parse::GreenTree& tree, bool keepFields) {
    std::string sexp = parse::SubtreeToSexp(tree.Root(), tree.Language());
    if (!keepFields)
        sexp = StripSexpFields(sexp);
    return sexp;
}

std::string PrettySexp(std::string_view sexp) {
    std::string out;
    int         depth = 0;
    std::size_t i     = 0;
    while (i < sexp.size()) {
        const char c = sexp[i];
        if (c == '(') {
            if (!out.empty()) {
                out.push_back('\n');
                out.append(static_cast<std::size_t>(depth) * 2, ' ');
            }
            out.push_back('(');
            ++depth;
            ++i;
        }
        else if (c == ')') {
            out.push_back(')');
            --depth;
            ++i;
        }
        else if (c == ' ') {
            // A field name precedes its node on the same line.
            std::size_t j = i + 1;
            while (j < sexp.size() && IsWordChar(sexp[j]))
                ++j;
            if (j > i + 1 && j + 2 < sexp.size() && sexp[j] == ':' && sexp[j + 1] == ' ' && sexp[j + 2] == '(') {
                out.push_back('\n');
                out.append(static_cast<std::size_t>(depth) * 2, ' ');
                out.append(sexp.substr(i + 1, j - i)); // "name:"
                out.push_back(' ');
                out.push_back('(');
                ++depth;
                i = j + 3;
                continue;
            }
            ++i;
        }
        else {
            out.push_back(c);
            ++i;
        }
    }
    return out;
}

std::vector<fs::path> CorpusFiles(const fs::path& directory) {
    std::vector<fs::path> files;
    std::error_code       ec;
    for (const auto& entry : fs::recursive_directory_iterator(directory, ec))
        if (entry.is_regular_file() && (entry.path().extension() == ".txt" || entry.path().extension() == ".mk"))
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    return files;
}

CaseResult RunCase(parse::Engine& engine, const Case& item) {
    const parse::GreenTree tree = engine.Parse(item.input);
    if (item.error)
        return {.passed = tree.HasError(), .actual = tree.HasError() ? std::string() : ActualSexp(tree, item.hasFields)};
    std::string actual = ActualSexp(tree, item.hasFields);
    const bool  passed = actual == item.expected;
    return {.passed = passed, .actual = std::move(actual)};
}

} // namespace ned::editor::grammar::corpus
