#include "Editor/Org.h"

#include <cctype>
#include <regex>

namespace ned::editor::org {

namespace {

// Matches "<title><ws><tags>" where <tags> is one or more ":tag:" runs
// closed by a final ':' -- e.g. "Buy milk  :errand:home:" -> title "Buy
// milk", tags block ":errand:home:". regex_match anchors both ends, and the
// lazy `.*?` for the title means the engine only accepts the shortest
// title for which *the remainder* is a fully-valid trailing tag block, so a
// title that merely contains a stray "foo:bar" substring earlier in the
// line never gets misread as the start of a tags block.
const std::regex& TagBlockPattern() {
    static const std::regex pattern(R"(^(.*?)\s*((?::[A-Za-z0-9_@#%]+)+:)\s*$)");
    return pattern;
}

std::string TrimWhitespace(std::string_view s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return std::string(s.substr(start, end - start));
}

// tagBlock looks like ":tag1:tag2:...:tagN:" -- split on ':', discarding
// the empty tokens the leading and trailing colons produce.
std::vector<std::string> SplitTagBlock(const std::string& tagBlock) {
    std::vector<std::string> tags;
    std::string              current;
    for (char c : tagBlock) {
        if (c == ':') {
            if (!current.empty()) {
                tags.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) tags.push_back(current);
    return tags;
}

std::optional<Headline> ParseHeadlineLine(std::string_view line, const std::vector<std::string>& todoKeywords) {
    std::size_t starCount = 0;
    while (starCount < line.size() && line[starCount] == '*') ++starCount;
    // No leading whitespace allowed before the stars (that's what makes
    // this a headline, not an indented list item), and at least one space
    // must follow the stars -- real Org's own outline-regexp shape.
    if (starCount == 0 || starCount >= line.size() || line[starCount] != ' ') return std::nullopt;

    std::string_view rest = line.substr(starCount + 1);

    Headline headline;
    headline.level = static_cast<int>(starCount);

    for (const std::string& keyword : todoKeywords) {
        if (!keyword.empty() && rest.size() >= keyword.size() && rest.compare(0, keyword.size(), keyword) == 0 &&
            (rest.size() == keyword.size() || rest[keyword.size()] == ' ')) {
            headline.todoKeyword = keyword;
            rest.remove_prefix(keyword.size());
            if (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
            break;
        }
    }

    if (rest.size() >= 4 && rest[0] == '[' && rest[1] == '#' && rest[3] == ']' &&
        (rest.size() == 4 || rest[4] == ' ')) {
        headline.priority = rest[2];
        rest.remove_prefix(4);
        if (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
    }

    std::string restStr(rest);
    std::smatch match;
    if (std::regex_match(restStr, match, TagBlockPattern())) {
        headline.title = TrimWhitespace(match[1].str());
        headline.tags  = SplitTagBlock(match[2].str());
    } else {
        headline.title = TrimWhitespace(restStr);
    }

    return headline;
}

} // namespace

std::vector<std::string> DefaultTodoKeywords() { return {"TODO", "DONE"}; }

std::vector<Headline> ParseOutline(std::string_view bufferText, const std::vector<std::string>& todoKeywords) {
    std::vector<Headline> headlines;
    std::size_t           lineStart  = 0;
    std::size_t           lineNumber = 0;

    while (lineStart <= bufferText.size()) {
        std::size_t      newlinePos = bufferText.find('\n', lineStart);
        std::size_t      lineEnd    = (newlinePos == std::string_view::npos) ? bufferText.size() : newlinePos;
        std::string_view line       = bufferText.substr(lineStart, lineEnd - lineStart);

        if (auto headline = ParseHeadlineLine(line, todoKeywords)) {
            headline->lineNumber    = lineNumber;
            headline->lineStartByte = lineStart;
            headline->lineEndByte   = lineEnd;
            headlines.push_back(std::move(*headline));
        }

        if (newlinePos == std::string_view::npos) break;
        lineStart = newlinePos + 1;
        ++lineNumber;
    }

    return headlines;
}

std::string NextTodoKeyword(const std::string& current, const std::vector<std::string>& todoKeywords) {
    if (todoKeywords.empty()) return "";

    for (std::size_t i = 0; i < todoKeywords.size(); ++i) {
        if (todoKeywords[i] == current) return (i + 1 < todoKeywords.size()) ? todoKeywords[i + 1] : "";
    }
    // Unrecognized (including "") -- start the cycle over, same "don't
    // silently no-op on a stale value" reasoning as the header documents.
    return todoKeywords.front();
}

std::optional<char> NextPriority(std::optional<char> current) {
    if (!current) return 'A';
    if (*current == 'A') return 'B';
    if (*current == 'B') return 'C';
    return std::nullopt;
}

} // namespace ned::editor::org
