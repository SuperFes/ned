#include "Editor/Org.h"

#include <cctype>
#include <mutex>
#include <regex>
#include <utility>

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

// "  - [ ] Buy milk" -> indent 2, state ' ', text "Buy milk". Requires
// exactly one space between the bullet and '[', and between ']' and the
// text (if any) -- matching how ParseHeadlineLine treats the space after
// the stars as mandatory, not optional.
std::optional<Checkbox> ParseCheckboxLine(std::string_view line) {
    std::size_t indent = 0;
    while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t')) ++indent;

    std::size_t pos = indent;
    if (pos >= line.size() || (line[pos] != '-' && line[pos] != '+')) return std::nullopt;
    ++pos;
    if (pos >= line.size() || line[pos] != ' ') return std::nullopt;
    ++pos;
    if (pos >= line.size() || line[pos] != '[') return std::nullopt;
    ++pos;
    if (pos >= line.size()) return std::nullopt;

    const char state = line[pos];
    if (state != ' ' && state != 'X' && state != 'x' && state != '-') return std::nullopt;
    const std::size_t stateOffsetInLine = pos;
    ++pos;

    if (pos >= line.size() || line[pos] != ']') return std::nullopt;
    ++pos;

    if (pos < line.size() && line[pos] == ' ') ++pos;

    Checkbox box;
    box.indent    = indent;
    box.state     = state;
    box.text      = TrimWhitespace(line.substr(pos));
    box.stateByte = stateOffsetInLine; // caller adds the line's own start offset
    return box;
}

std::mutex& TodoKeywordsMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<std::string>& TodoKeywordsStorage() {
    static std::vector<std::string> keywords = DefaultTodoKeywords();
    return keywords;
}

// Shared by SetHeadlineTodoKeyword/SetHeadlinePriority: replaces the token
// occupying [tokenStart, tokenEnd) with newText, where tokenStart == tokenEnd
// means "no token there yet" (the field was absent) and an empty newText
// means "remove it". A present token is always followed by exactly one
// separating space UNLESS it runs to the line's own end (nothing after it)
// -- ParseHeadlineLine only ever consumes a token followed by a space or
// end-of-line, so that invariant is what lets this be computed rather than
// re-scanned. Inserting a brand-new token always adds its own trailing
// space (even if the line had nothing after it); removing an existing one
// also consumes that trailing space, so title/tag text already present
// never ends up with a stray leading space either way.
void ReplaceOptionalToken(text::Buffer& buffer, std::size_t tokenStart, std::size_t tokenEnd, std::size_t lineEnd,
                           std::string_view newText) {
    std::size_t deleteEnd = tokenEnd;
    if (newText.empty() && tokenEnd < lineEnd) ++deleteEnd;
    if (deleteEnd > tokenStart) buffer.DeleteRange(tokenStart, deleteEnd - tokenStart);

    if (!newText.empty()) {
        std::string insertText(newText);
        if (tokenStart == tokenEnd) insertText += ' ';
        buffer.InsertAt(tokenStart, insertText);
    }
}

} // namespace

std::vector<std::string> DefaultTodoKeywords() { return {"TODO", "DONE"}; }

void SetTodoKeywords(std::vector<std::string> keywords) {
    const std::lock_guard<std::mutex> lock(TodoKeywordsMutex());
    TodoKeywordsStorage() = std::move(keywords);
}

const std::vector<std::string>& TodoKeywords() {
    const std::lock_guard<std::mutex> lock(TodoKeywordsMutex());
    return TodoKeywordsStorage();
}

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

std::vector<Checkbox> ParseCheckboxes(std::string_view bufferText) {
    std::vector<Checkbox> checkboxes;
    std::size_t           lineStart  = 0;
    std::size_t           lineNumber = 0;

    while (lineStart <= bufferText.size()) {
        std::size_t      newlinePos = bufferText.find('\n', lineStart);
        std::size_t      lineEnd    = (newlinePos == std::string_view::npos) ? bufferText.size() : newlinePos;
        std::string_view line       = bufferText.substr(lineStart, lineEnd - lineStart);

        if (auto checkbox = ParseCheckboxLine(line)) {
            checkbox->lineNumber = lineNumber;
            checkbox->stateByte += lineStart;
            checkboxes.push_back(std::move(*checkbox));
        }

        if (newlinePos == std::string_view::npos) break;
        lineStart = newlinePos + 1;
        ++lineNumber;
    }

    return checkboxes;
}

char ToggleCheckboxState(char current) { return (current == 'X' || current == 'x') ? ' ' : 'X'; }

std::vector<Checkbox> ReflectParentCheckboxStates(std::vector<Checkbox> items) {
    // Reverse order so that by the time a parent's own direct children are
    // examined, every one of THEIR own descendants (processed earlier in
    // this same pass, since they sit at later indices) already holds its
    // final, recomputed state.
    for (std::size_t i = items.size(); i-- > 0;) {
        std::vector<char> directChildStates;
        std::size_t       j = i + 1;
        while (j < items.size() && items[j].indent > items[i].indent) {
            directChildStates.push_back(items[j].state);
            // Skip past this child's own descendants -- they aren't DIRECT
            // children of item i, and their influence is already folded
            // into items[j].state from an earlier iteration of this loop.
            std::size_t descendantEnd = j + 1;
            while (descendantEnd < items.size() && items[descendantEnd].indent > items[j].indent) ++descendantEnd;
            j = descendantEnd;
        }
        if (directChildStates.empty()) continue;

        bool allChecked   = true;
        bool allUnchecked = true;
        for (char state : directChildStates) {
            if (state != 'X' && state != 'x') allChecked = false;
            if (state != ' ') allUnchecked = false;
        }
        items[i].state = allChecked ? 'X' : (allUnchecked ? ' ' : '-');
    }
    return items;
}

std::optional<Headline> HeadlineAtPoint(const text::Buffer& buffer, const std::vector<std::string>& todoKeywords) {
    const auto        headlines = ParseOutline(buffer.Text(), todoKeywords);
    const std::size_t point     = buffer.Point();
    for (const Headline& headline : headlines) {
        if (point >= headline.lineStartByte && point <= headline.lineEndByte) return headline;
    }
    return std::nullopt;
}

void SetHeadlineTodoKeyword(text::Buffer& buffer, const Headline& headline, const std::string& newKeyword) {
    const std::size_t tokenStart = headline.lineStartByte + static_cast<std::size_t>(headline.level) + 1;
    const std::size_t tokenEnd   = tokenStart + headline.todoKeyword.size();
    ReplaceOptionalToken(buffer, tokenStart, tokenEnd, headline.lineEndByte, newKeyword);
}

void SetHeadlinePriority(text::Buffer& buffer, const Headline& headline, std::optional<char> newPriority) {
    std::size_t tokenStart = headline.lineStartByte + static_cast<std::size_t>(headline.level) + 1;
    if (!headline.todoKeyword.empty()) {
        tokenStart += headline.todoKeyword.size();
        // Only step past a separating space if one is actually there --
        // "* TODO" alone (keyword runs to the line's own end) has none.
        if (tokenStart < headline.lineEndByte) ++tokenStart;
    }
    const std::size_t tokenEnd = tokenStart + (headline.priority.has_value() ? 4 : 0);

    std::string newText;
    if (newPriority) newText = std::string("[#") + *newPriority + "]";
    ReplaceOptionalToken(buffer, tokenStart, tokenEnd, headline.lineEndByte, newText);
}

bool CycleTodoKeywordAtPoint(text::Buffer& buffer, const std::vector<std::string>& todoKeywords) {
    const auto headline = HeadlineAtPoint(buffer, todoKeywords);
    if (!headline) return false;
    SetHeadlineTodoKeyword(buffer, *headline, NextTodoKeyword(headline->todoKeyword, todoKeywords));
    return true;
}

bool CyclePriorityAtPoint(text::Buffer& buffer, const std::vector<std::string>& todoKeywords) {
    const auto headline = HeadlineAtPoint(buffer, todoKeywords);
    if (!headline) return false;
    SetHeadlinePriority(buffer, *headline, NextPriority(headline->priority));
    return true;
}

bool ToggleCheckboxAtPoint(text::Buffer& buffer) {
    std::vector<Checkbox> checkboxes = ParseCheckboxes(buffer.Text());
    const std::size_t     pointLine  = buffer.Content().ByteOffsetToLine(buffer.Point());

    std::optional<std::size_t> targetIndex;
    for (std::size_t i = 0; i < checkboxes.size(); ++i) {
        if (checkboxes[i].lineNumber == pointLine) {
            targetIndex = i;
            break;
        }
    }
    if (!targetIndex) return false;

    const std::vector<Checkbox> before = checkboxes;
    checkboxes[*targetIndex].state     = ToggleCheckboxState(checkboxes[*targetIndex].state);
    checkboxes                         = ReflectParentCheckboxStates(std::move(checkboxes));

    // Every edit here is a single-byte replace at a fixed, already-known
    // offset -- net buffer length never changes, so no other checkbox's own
    // stateByte can ever drift mid-loop, regardless of edit order.
    for (std::size_t i = 0; i < checkboxes.size(); ++i) {
        if (checkboxes[i].state != before[i].state) {
            buffer.DeleteRange(checkboxes[i].stateByte, 1);
            buffer.InsertAt(checkboxes[i].stateByte, std::string(1, checkboxes[i].state));
        }
    }
    return true;
}

} // namespace ned::editor::org
