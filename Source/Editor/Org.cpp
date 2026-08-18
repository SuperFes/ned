#include "Editor/Org.h"

#include <cctype>
#include <mutex>
#include <regex>
#include <utility>

#include "Editor/Table.h"

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
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
            ++start;
        std::size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
            --end;
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
            }
            else {
                current.push_back(c);
            }
        }
        if (!current.empty())
            tags.push_back(current);
        return tags;
    }

    std::optional<Headline> ParseHeadlineLine(std::string_view line, const std::vector<std::string>& todoKeywords) {
        std::size_t starCount = 0;
        while (starCount < line.size() && line[starCount] == '*')
            ++starCount;
        // No leading whitespace allowed before the stars (that's what makes
        // this a headline, not an indented list item), and at least one space
        // must follow the stars -- real Org's own outline-regexp shape.
        if (starCount == 0 || starCount >= line.size() || line[starCount] != ' ')
            return std::nullopt;

        std::string_view rest = line.substr(starCount + 1);

        Headline headline;
        headline.level = static_cast<int>(starCount);

        for (const std::string& keyword : todoKeywords) {
            if (!keyword.empty() && rest.size() >= keyword.size() && rest.compare(0, keyword.size(), keyword) == 0 &&
                (rest.size() == keyword.size() || rest[keyword.size()] == ' ')) {
                headline.todoKeyword = keyword;
                rest.remove_prefix(keyword.size());
                if (!rest.empty() && rest.front() == ' ')
                    rest.remove_prefix(1);
                break;
            }
        }

        if (rest.size() >= 4 && rest[0] == '[' && rest[1] == '#' && rest[3] == ']' &&
            (rest.size() == 4 || rest[4] == ' ')) {
            headline.priority = rest[2];
            rest.remove_prefix(4);
            if (!rest.empty() && rest.front() == ' ')
                rest.remove_prefix(1);
        }

        // rest is still a view into the same underlying `line` buffer
        // throughout (remove_prefix only ever moves its start pointer
        // forward) -- this is `rest`'s own offset within `line`, unaffected
        // by however much keyword/priority parsing already consumed above.
        const auto restOffsetInLine = static_cast<std::size_t>(rest.data() - line.data());

        std::string restStr(rest);
        std::smatch match;
        if (std::regex_match(restStr, match, TagBlockPattern())) {
            headline.title = TrimWhitespace(match[1].str());
            headline.tags  = SplitTagBlock(match[2].str());
            // Starts right after the (trimmed) title ends, consuming the
            // \s* separator the regex matched between title and tags --
            // SetHeadlineTags removes from here through lineEndByte
            // wholesale, so that separator (and any trailing whitespace
            // after the tags block, also covered by \s*$) never survives a
            // tags-block removal or replacement.
            headline.tagsStartByte =
                restOffsetInLine + static_cast<std::size_t>(match.position(1)) + static_cast<std::size_t>(match.length(1));
        }
        else {
            headline.title         = TrimWhitespace(restStr);
            headline.tagsStartByte = line.size(); // no tags block -- becomes lineEndByte once ParseOutline adds lineStart
        }

        return headline;
    }

    // "  - [ ] Buy milk" -> indent 2, state ' ', text "Buy milk". Requires
    // exactly one space between the bullet and '[', and between ']' and the
    // text (if any) -- matching how ParseHeadlineLine treats the space after
    // the stars as mandatory, not optional.
    std::optional<Checkbox> ParseCheckboxLine(std::string_view line) {
        std::size_t indent = 0;
        while (indent < line.size() && (line[indent] == ' ' || line[indent] == '\t'))
            ++indent;

        std::size_t pos = indent;
        if (pos >= line.size() || (line[pos] != '-' && line[pos] != '+'))
            return std::nullopt;
        ++pos;
        if (pos >= line.size() || line[pos] != ' ')
            return std::nullopt;
        ++pos;
        if (pos >= line.size() || line[pos] != '[')
            return std::nullopt;
        ++pos;
        if (pos >= line.size())
            return std::nullopt;

        const char state = line[pos];
        if (state != ' ' && state != 'X' && state != 'x' && state != '-')
            return std::nullopt;
        const std::size_t stateOffsetInLine = pos;
        ++pos;

        if (pos >= line.size() || line[pos] != ']')
            return std::nullopt;
        ++pos;

        if (pos < line.size() && line[pos] == ' ')
            ++pos;

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
        if (newText.empty() && tokenEnd < lineEnd)
            ++deleteEnd;
        if (deleteEnd > tokenStart)
            buffer.DeleteRange(tokenStart, deleteEnd - tokenStart);

        if (!newText.empty()) {
            std::string insertText(newText);
            if (tokenStart == tokenEnd)
                insertText += ' ';
            buffer.InsertAt(tokenStart, insertText);
        }
    }

    // Recursively builds the direct children of a node spanning
    // headlines[start, end) at the shallowest level found there -- headlines
    // past the first one deeper than that shallowest level belong to ITS
    // subtree, not as a sibling; only ever visited once each across the whole
    // recursion for a realistic (shallow-nesting) outline. Builds and returns
    // each vector by value (no pointers into a vector that a sibling's own
    // later push_back could reallocate out from under it) -- simplicity and
    // obvious correctness preferred over a pointer-based single pass.
    std::vector<HeadlineNode> BuildHeadlineChildren(const std::vector<Headline>& headlines, std::size_t start,
                                                    std::size_t end) {
        std::vector<HeadlineNode> nodes;
        std::size_t               i = start;
        while (i < end) {
            const int   level    = headlines[i].level;
            std::size_t childEnd = i + 1;
            while (childEnd < end && headlines[childEnd].level > level)
                ++childEnd;

            HeadlineNode node;
            node.headline = &headlines[i];
            node.children = BuildHeadlineChildren(headlines, i + 1, childEnd);
            nodes.push_back(std::move(node));
            i = childEnd;
        }
        return nodes;
    }

    // Finds the node (searching depth-first through nodes and every
    // descendant) whose own headline sits at lineStartByte -- used to locate
    // the headline HeadlineAtPoint already found (by value, not by pointer
    // into this function's own freshly-parsed headlines/tree) in the tree
    // CycleFoldAtPoint just built.
    const HeadlineNode* FindHeadlineNode(const std::vector<HeadlineNode>& nodes, std::size_t lineStartByte) {
        for (const HeadlineNode& node : nodes) {
            if (node.headline->lineStartByte == lineStartByte)
                return &node;
            if (const HeadlineNode* found = FindHeadlineNode(node.children, lineStartByte))
                return found;
        }
        return nullptr;
    }

    // ChildrenVisible -> Expanded clears every descendant's own marker (not
    // node's own -- the caller clears that directly), recursively, so a fully
    // expanded subtree has no leftover Collapsed/ChildrenVisible markers
    // anywhere underneath it.
    void ClearDescendantFoldMarkers(text::Buffer& buffer, const HeadlineNode& node) {
        for (const HeadlineNode& child : node.children) {
            buffer.SetFoldMarker(child.headline->lineStartByte, std::nullopt);
            ClearDescendantFoldMarkers(buffer, child);
        }
    }

    void CollectFoldedLineRanges(const std::vector<Headline>& headlines, const HeadlineNode& node,
                                 const text::Buffer& buffer, std::size_t totalLines,
                                 std::vector<std::pair<std::size_t, std::size_t>>& ranges) {
        const auto marker = buffer.FoldMarkerAt(node.headline->lineStartByte);

        if (marker == text::Buffer::FoldMarker::Collapsed) {
            const std::size_t index       = static_cast<std::size_t>(node.headline - headlines.data());
            const std::size_t subtreeEnd  = SubtreeEndLine(headlines, index, totalLines);
            const std::size_t hiddenStart = node.headline->lineNumber + 1;
            if (subtreeEnd > hiddenStart) {
                ranges.emplace_back(hiddenStart, subtreeEnd);
            }
            return; // don't recurse -- everything below is hidden regardless of its own marker
        }

        if (marker == text::Buffer::FoldMarker::ChildrenVisible) {
            const std::size_t index       = static_cast<std::size_t>(node.headline - headlines.data());
            const std::size_t bodyEnd     = node.children.empty() ? SubtreeEndLine(headlines, index, totalLines)
                                                                  : node.children.front().headline->lineNumber;
            const std::size_t hiddenStart = node.headline->lineNumber + 1;
            if (bodyEnd > hiddenStart) {
                ranges.emplace_back(hiddenStart, bodyEnd);
            }
            for (const HeadlineNode& child : node.children) {
                CollectFoldedLineRanges(headlines, child, buffer, totalLines, ranges);
            }
            return;
        }

        // Unmarked (Expanded): nothing of this node's own is hidden, recurse normally.
        for (const HeadlineNode& child : node.children) {
            CollectFoldedLineRanges(headlines, child, buffer, totalLines, ranges);
        }
    }

    // Org's own separator-row rule: every non-whitespace character is '-'
    // or '+', with at least one of them present (an all-whitespace line
    // never reaches here at all -- FindTableBlockLines already requires a
    // leading '|'). See OrgTable's own doc comment in Org.h for the
    // resulting, format-inherent ambiguity against a data row that happens
    // to look the same.
    bool IsOrgSeparatorRow(std::string_view line) {
        bool sawDashOrPlus = false;
        for (const char c : line) {
            if (c == '-' || c == '+') {
                sawDashOrPlus = true;
            }
            else if (c != '|' && c != ' ' && c != '\t') {
                return false;
            }
        }
        return sawDashOrPlus;
    }

} // namespace

std::vector<std::string> DefaultTodoKeywords() {
    return {"TODO", "DONE"};
}

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
            headline->tagsStartByte += lineStart; // was line-relative, see ParseHeadlineLine
            headlines.push_back(std::move(*headline));
        }

        if (newlinePos == std::string_view::npos)
            break;
        lineStart = newlinePos + 1;
        ++lineNumber;
    }

    return headlines;
}

std::string NextTodoKeyword(const std::string& current, const std::vector<std::string>& todoKeywords) {
    if (todoKeywords.empty())
        return "";

    for (std::size_t i = 0; i < todoKeywords.size(); ++i) {
        if (todoKeywords[i] == current)
            return (i + 1 < todoKeywords.size()) ? todoKeywords[i + 1] : "";
    }
    // Unrecognized (including "") -- start the cycle over, same "don't
    // silently no-op on a stale value" reasoning as the header documents.
    return todoKeywords.front();
}

std::optional<char> NextPriority(std::optional<char> current) {
    if (!current)
        return 'A';
    if (*current == 'A')
        return 'B';
    if (*current == 'B')
        return 'C';
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

        if (newlinePos == std::string_view::npos)
            break;
        lineStart = newlinePos + 1;
        ++lineNumber;
    }

    return checkboxes;
}

char ToggleCheckboxState(char current) {
    return (current == 'X' || current == 'x') ? ' ' : 'X';
}

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
            while (descendantEnd < items.size() && items[descendantEnd].indent > items[j].indent)
                ++descendantEnd;
            j = descendantEnd;
        }
        if (directChildStates.empty())
            continue;

        bool allChecked   = true;
        bool allUnchecked = true;
        for (char state : directChildStates) {
            if (state != 'X' && state != 'x')
                allChecked = false;
            if (state != ' ')
                allUnchecked = false;
        }
        items[i].state = allChecked ? 'X' : (allUnchecked ? ' ' : '-');
    }
    return items;
}

std::optional<Headline> HeadlineAtPoint(const text::Buffer& buffer, const std::vector<std::string>& todoKeywords) {
    const auto        headlines = ParseOutline(buffer.Text(), todoKeywords);
    const std::size_t point     = buffer.Point();
    for (const Headline& headline : headlines) {
        if (point >= headline.lineStartByte && point <= headline.lineEndByte)
            return headline;
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
        if (tokenStart < headline.lineEndByte)
            ++tokenStart;
    }
    const std::size_t tokenEnd = tokenStart + (headline.priority.has_value() ? 4 : 0);

    std::string newText;
    if (newPriority)
        newText = std::string("[#") + *newPriority + "]";
    ReplaceOptionalToken(buffer, tokenStart, tokenEnd, headline.lineEndByte, newText);
}

void SetHeadlineTags(text::Buffer& buffer, const Headline& headline, std::vector<std::string> newTags) {
    // Not routed through ReplaceOptionalToken: that helper's "exactly one
    // separating space, add/remove it symmetrically" rule is built for a
    // fixed-position token right after the stars -- tagsStartByte already
    // covers the tags block's own leading separator AND any trailing
    // whitespace after it (see the field's own doc comment), so a plain
    // delete-then-insert is both correct and simpler here.
    if (headline.tagsStartByte < headline.lineEndByte) {
        buffer.DeleteRange(headline.tagsStartByte, headline.lineEndByte - headline.tagsStartByte);
    }
    if (!newTags.empty()) {
        std::string newText = " :";
        for (const std::string& tag : newTags) {
            newText += tag + ":";
        }
        buffer.InsertAt(headline.tagsStartByte, newText);
    }
}

bool CycleTodoKeywordAtPoint(text::Buffer& buffer, const std::vector<std::string>& todoKeywords) {
    const auto headline = HeadlineAtPoint(buffer, todoKeywords);
    if (!headline)
        return false;
    SetHeadlineTodoKeyword(buffer, *headline, NextTodoKeyword(headline->todoKeyword, todoKeywords));
    return true;
}

bool CyclePriorityAtPoint(text::Buffer& buffer, const std::vector<std::string>& todoKeywords) {
    const auto headline = HeadlineAtPoint(buffer, todoKeywords);
    if (!headline)
        return false;
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
    if (!targetIndex)
        return false;

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

std::vector<HeadlineNode> BuildHeadlineTree(const std::vector<Headline>& headlines) {
    return BuildHeadlineChildren(headlines, 0, headlines.size());
}

std::size_t SubtreeEndLine(const std::vector<Headline>& headlines, std::size_t index, std::size_t totalLines) {
    const int level = headlines[index].level;
    for (std::size_t j = index + 1; j < headlines.size(); ++j) {
        if (headlines[j].level <= level)
            return headlines[j].lineNumber;
    }
    return totalLines;
}

std::vector<std::pair<std::size_t, std::size_t>> FoldedLineRanges(const text::Buffer&             buffer,
                                                                  const std::vector<std::string>& todoKeywords) {
    if (buffer.FoldMarkers().empty()) {
        return {};
    }

    const std::string text       = buffer.Text();
    const auto        headlines  = ParseOutline(text, todoKeywords);
    const auto        tree       = BuildHeadlineTree(headlines);
    const std::size_t totalLines = buffer.Content().LineCount();

    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    for (const HeadlineNode& root : tree) {
        CollectFoldedLineRanges(headlines, root, buffer, totalLines, ranges);
    }
    return ranges;
}

bool CycleFoldAtPoint(text::Buffer& buffer, const std::vector<std::string>& todoKeywords) {
    const auto headlineAtPoint = HeadlineAtPoint(buffer, todoKeywords);
    if (!headlineAtPoint)
        return false;

    const std::string   text      = buffer.Text();
    const auto          headlines = ParseOutline(text, todoKeywords);
    const auto          tree      = BuildHeadlineTree(headlines);
    const HeadlineNode* node      = FindHeadlineNode(tree, headlineAtPoint->lineStartByte);
    if (!node)
        return false; // same buffer, same parse -- should always be found

    const auto current = buffer.FoldMarkerAt(node->headline->lineStartByte);
    if (!current) {
        buffer.SetFoldMarker(node->headline->lineStartByte, text::Buffer::FoldMarker::Collapsed);
    }
    else if (*current == text::Buffer::FoldMarker::Collapsed) {
        buffer.SetFoldMarker(node->headline->lineStartByte, text::Buffer::FoldMarker::ChildrenVisible);
        for (const HeadlineNode& child : node->children) {
            buffer.SetFoldMarker(child.headline->lineStartByte, text::Buffer::FoldMarker::Collapsed);
        }
    }
    else {
        buffer.SetFoldMarker(node->headline->lineStartByte, std::nullopt);
        ClearDescendantFoldMarkers(buffer, *node);
    }

    // No point-safety clamp needed here: HeadlineAtPoint already required
    // point to sit on this exact headline's own line, and every transition
    // above only ever hides lines strictly after it (hiddenStart is always
    // lineNumber + 1) -- point can never end up inside a range its own
    // headline's fold just hid.
    return true;
}

std::optional<OrgTable> FindOrgTableAtPoint(const text::Buffer& buffer) {
    const std::string bufferText = buffer.Text();
    const std::size_t pointLine  = buffer.Content().ByteOffsetToLine(buffer.Point());

    const auto block = table::FindTableBlockLines(bufferText, pointLine);
    if (!block) {
        return std::nullopt;
    }
    const auto [startLine, endLine] = *block;

    OrgTable result;
    result.startLine = startLine;
    result.endLine   = endLine;

    std::size_t lineStart  = 0;
    std::size_t lineNumber = 0;
    while (lineStart <= bufferText.size() && lineNumber < endLine) {
        const std::size_t newlinePos = bufferText.find('\n', lineStart);
        const std::size_t lineEnd    = (newlinePos == std::string::npos) ? bufferText.size() : newlinePos;
        if (lineNumber >= startLine) {
            const std::string_view line(bufferText.data() + lineStart, lineEnd - lineStart);
            const bool             isSeparator = IsOrgSeparatorRow(line);
            result.isSeparatorRow.push_back(isSeparator);
            result.rows.push_back(isSeparator ? std::vector<std::string>{} : table::SplitRow(line));
        }
        if (newlinePos == std::string::npos)
            break;
        lineStart = newlinePos + 1;
        ++lineNumber;
    }
    return result;
}

bool AlignOrgTableAtPoint(text::Buffer& buffer) {
    const auto tableInfo = FindOrgTableAtPoint(buffer);
    if (!tableInfo) {
        return false;
    }

    // Byte spans per non-separator row's own cells -- re-derived here
    // (not carried on OrgTable itself) since only this point-tracking path
    // needs them, mirroring the existing split between this file's pure
    // parsers and their *AtPoint wrappers' own extra buffer-aware work.
    const std::string                                             bufferText = buffer.Text();
    std::vector<std::vector<std::pair<std::size_t, std::size_t>>> cellSpans(tableInfo->rows.size());
    {
        std::size_t lineStart  = 0;
        std::size_t lineNumber = 0;
        std::size_t rowIndex   = 0;
        while (lineStart <= bufferText.size() && lineNumber < tableInfo->endLine) {
            const std::size_t newlinePos = bufferText.find('\n', lineStart);
            const std::size_t lineEnd    = (newlinePos == std::string::npos) ? bufferText.size() : newlinePos;
            if (lineNumber >= tableInfo->startLine) {
                if (!tableInfo->isSeparatorRow[rowIndex]) {
                    cellSpans[rowIndex] =
                        table::CellByteSpans(std::string_view(bufferText).substr(lineStart, lineEnd - lineStart), lineStart);
                }
                ++rowIndex;
            }
            if (newlinePos == std::string::npos)
                break;
            lineStart = newlinePos + 1;
            ++lineNumber;
        }
    }

    // Which cell point currently sits in -- falls back to the table's
    // first real cell if point isn't cleanly inside any span (e.g. sitting
    // on a separator row's own line); AlignOrgTableAtPoint's own
    // precondition is already satisfied (FindOrgTableAtPoint succeeded),
    // so there's always a sensible cell to land on/advance from.
    const std::size_t point      = buffer.Point();
    std::size_t       currentRow = 0, currentCol = 0;
    bool              found = false;
    for (std::size_t row = 0; row < tableInfo->rows.size() && !found; ++row) {
        if (tableInfo->isSeparatorRow[row])
            continue;
        for (std::size_t col = 0; col < cellSpans[row].size(); ++col) {
            if (point >= cellSpans[row][col].first && point <= cellSpans[row][col].second) {
                currentRow = row;
                currentCol = col;
                found      = true;
                break;
            }
        }
    }
    if (!found) {
        for (std::size_t row = 0; row < tableInfo->rows.size(); ++row) {
            if (!tableInfo->isSeparatorRow[row]) {
                currentRow = row;
                currentCol = 0;
                break;
            }
        }
    }

    std::vector<std::vector<std::string>> dataRows;
    for (std::size_t row = 0; row < tableInfo->rows.size(); ++row) {
        if (!tableInfo->isSeparatorRow[row])
            dataRows.push_back(tableInfo->rows[row]);
    }
    const std::vector<std::size_t> widths = table::ComputeColumnWidths(dataRows);

    // Rebuild the whole block, tracking each rendered cell's own start
    // offset within the new text as it's built -- the target cell's new
    // position is then known exactly, no separate re-parse needed.
    std::string                           newText;
    std::vector<std::vector<std::size_t>> newCellOffsets(tableInfo->rows.size());
    for (std::size_t row = 0; row < tableInfo->rows.size(); ++row) {
        if (tableInfo->isSeparatorRow[row]) {
            // Leading/trailing '|', '+' only at internal column
            // intersections -- matches real Org's own hline convention
            // ("|-------+-----|", not "+-------+-----+"). Load-bearing,
            // not just cosmetic: a leading '+' would make this line fail
            // table::FindTableBlockLines' own "starts with '|'" detection
            // rule the next time this table is touched.
            newText += '|';
            for (std::size_t col = 0; col < widths.size(); ++col) {
                newText += std::string(widths[col] + 2, '-');
                newText += (col + 1 < widths.size()) ? '+' : '|';
            }
        }
        else {
            newText += '|';
            for (std::size_t col = 0; col < widths.size(); ++col) {
                newText += ' ';
                newCellOffsets[row].push_back(newText.size());
                const std::string cellText = col < tableInfo->rows[row].size() ? tableInfo->rows[row][col] : std::string();
                newText += table::PadCell(cellText, widths[col], table::Alignment::Left);
                newText += " |";
            }
        }
        newText += '\n';
    }
    // The block's own last line may not have had a trailing newline (it's
    // the buffer's real last line and the buffer itself doesn't end in
    // '\n') -- drop the one just added above to match, rather than
    // unconditionally introducing one that wasn't there before.
    const bool blockHadTrailingNewline = tableInfo->endLine < buffer.Content().LineCount();
    if (!blockHadTrailingNewline && !newText.empty()) {
        newText.pop_back();
    }

    const std::size_t blockStartByte = buffer.Content().LineToByteOffset(tableInfo->startLine);
    const std::size_t blockEndByte   = (tableInfo->endLine < buffer.Content().LineCount())
                                           ? buffer.Content().LineToByteOffset(tableInfo->endLine)
                                           : buffer.Content().ByteLength();

    // Advance to the next cell in row-major order, skipping separator
    // rows, wrapping from the table's last real cell back to its first.
    std::vector<std::pair<std::size_t, std::size_t>> realCells;
    for (std::size_t row = 0; row < tableInfo->rows.size(); ++row) {
        if (tableInfo->isSeparatorRow[row])
            continue;
        for (std::size_t col = 0; col < widths.size(); ++col)
            realCells.emplace_back(row, col);
    }
    std::size_t currentIndex = 0;
    for (std::size_t i = 0; i < realCells.size(); ++i) {
        if (realCells[i].first == currentRow && realCells[i].second == currentCol) {
            currentIndex = i;
            break;
        }
    }
    const auto [targetRow, targetCol]       = realCells[(currentIndex + 1) % realCells.size()];
    const std::size_t targetOffsetInNewText = newCellOffsets[targetRow][targetCol];

    buffer.DeleteRange(blockStartByte, blockEndByte - blockStartByte);
    buffer.InsertAt(blockStartByte, newText);
    buffer.SetPoint(blockStartByte + targetOffsetInNewText);
    return true;
}

namespace {

    // "[[target]]" or "[[target][description]]" -- group 1 is the target
    // (anything but ']'), group 2 (optional) is the description (anything
    // but ']', may be empty for "[[target][]]").
    const std::regex& LinkPattern() {
        static const std::regex pattern(R"(\[\[([^\]]+)\](?:\[([^\]]*)\])?\])");
        return pattern;
    }

} // namespace

std::vector<Link> ParseLinks(std::string_view bufferText) {
    std::vector<Link> links;
    std::size_t       lineStart = 0;

    while (lineStart <= bufferText.size()) {
        const std::size_t      newlinePos = bufferText.find('\n', lineStart);
        const std::size_t      lineEnd    = (newlinePos == std::string_view::npos) ? bufferText.size() : newlinePos;
        const std::string_view line       = bufferText.substr(lineStart, lineEnd - lineStart);

        const std::cregex_iterator end;
        for (std::cregex_iterator it(line.data(), line.data() + line.size(), LinkPattern()); it != end; ++it) {
            const auto& match = *it;
            links.push_back(Link{
                .target      = match[1].str(),
                .description = match[2].matched ? match[2].str() : std::string(),
                .startByte   = lineStart + static_cast<std::size_t>(match.position()),
                .endByte     = lineStart + static_cast<std::size_t>(match.position() + match.length()),
            });
        }

        if (newlinePos == std::string_view::npos)
            break;
        lineStart = newlinePos + 1;
    }

    return links;
}

std::optional<Link> LinkAtPoint(const text::Buffer& buffer) {
    const std::size_t point = buffer.Point();
    for (const Link& link : ParseLinks(buffer.Text())) {
        if (point >= link.startByte && point < link.endByte) {
            return link;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> FindHeadlineByTitle(std::string_view bufferText, std::string_view title) {
    const std::string trimmedTitle = TrimWhitespace(title);
    for (const Headline& headline : ParseOutline(bufferText)) {
        if (headline.title == trimmedTitle) {
            return headline.lineStartByte;
        }
    }
    return std::nullopt;
}

} // namespace ned::editor::org
