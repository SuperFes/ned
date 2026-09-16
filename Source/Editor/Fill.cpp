#include "Fill.h"

#include <algorithm>
#include <cctype>

namespace ned::editor {

namespace {

    std::size_t CodepointCount(std::string_view text) {
        std::size_t count = 0;
        for (const unsigned char byte : text) {
            if ((byte & 0xC0) != 0x80) { // not a UTF-8 continuation byte
                ++count;
            }
        }
        return count;
    }

    // Mirrors Commands.cpp's own file-local LineContentEnd exactly: the byte
    // offset just past a line's content, excluding its trailing newline (if
    // any) -- point may be anywhere on the line, not just its start.
    std::size_t LineContentEnd(const text::ITextStorage& content, std::size_t point) {
        const std::size_t line = content.ByteOffsetToLine(point);
        return (line + 1 < content.LineCount()) ? content.LineToByteOffset(line + 1) - 1 : content.ByteLength();
    }

    bool IsBlankLine(const text::ITextStorage& content, std::size_t line) {
        const std::size_t start = content.LineToByteOffset(line);
        const std::size_t end   = LineContentEnd(content, start);
        const std::string text  = content.Substring(start, end - start);
        return text.find_first_not_of(" \t") == std::string::npos;
    }

    // Splits `body` on runs of space/tab into words, appending each into
    // `words` -- shared by every line FillParagraph collects text from.
    void AppendWords(std::string_view body, std::vector<std::string>& words) {
        std::size_t i = 0;
        while (i < body.size()) {
            while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) {
                ++i;
            }
            const std::size_t wordStart = i;
            while (i < body.size() && body[i] != ' ' && body[i] != '\t') {
                ++i;
            }
            if (i > wordStart) {
                words.emplace_back(body.substr(wordStart, i - wordStart));
            }
        }
    }

    // Detects a Markdown/Org/reST-style list marker at the very start of
    // `body` -- a bullet ("-", "*", "+") or an ordinal ("1.", "12)"),
    // always followed by real whitespace, optionally followed by a GFM
    // task checkbox ("[ ]"/"[x]"/"[X]") and ITS own trailing whitespace.
    // Returns the marker's total width (through its final trailing
    // whitespace, where the real content starts), or nullopt if `body`
    // doesn't open with one. Deliberately mode-agnostic, unlike
    // Editor/Languages/Markdown.cpp's own list handling: this syntax is
    // unambiguous wherever it appears -- a real word never starts "- " or
    // "1. " -- so treating it specially is correct in a plain-text
    // paragraph, an Org list, or a Doxygen-style bulleted comment alike.
    // list-aware-fill-paragraph follow-up: without this, wrapping a list
    // item's own first line folded its marker into the ordinary word
    // stream, so every WRAPPED continuation line lost the marker's hang
    // width entirely -- confirmed live as the root cause of ROADMAP.md's
    // own ad hoc paragraphs occasionally drifting to an indentation that
    // no longer matches their enclosing list item, which is exactly the
    // "4 spaces relative to nothing" shape that becomes an indented code
    // block under CommonMark.
    std::optional<std::size_t> DetectListMarker(std::string_view body) {
        std::size_t i = 0;
        if (!body.empty() && (body[0] == '-' || body[0] == '*' || body[0] == '+')) {
            i = 1;
        }
        else {
            std::size_t digits = 0;
            while (i < body.size() && std::isdigit(static_cast<unsigned char>(body[i]))) {
                ++i;
                ++digits;
            }
            if (digits == 0 || i >= body.size() || (body[i] != '.' && body[i] != ')')) {
                return std::nullopt;
            }
            ++i; // the '.' or ')'
        }
        if (i >= body.size() || (body[i] != ' ' && body[i] != '\t')) {
            return std::nullopt; // the glyph alone, with no following space, isn't a list marker
        }
        while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) {
            ++i;
        }
        if (i + 2 < body.size() && body[i] == '[' &&
            (body[i + 1] == ' ' || body[i + 1] == 'x' || body[i + 1] == 'X') && body[i + 2] == ']') {
            std::size_t afterCheckbox = i + 3;
            if (afterCheckbox < body.size() && (body[afterCheckbox] == ' ' || body[afterCheckbox] == '\t')) {
                i = afterCheckbox;
                while (i < body.size() && (body[i] == ' ' || body[i] == '\t')) {
                    ++i;
                }
            }
        }
        return i;
    }

} // namespace

std::vector<std::string> WrapWords(const std::vector<std::string>& words, std::size_t width) {
    std::vector<std::string> lines;
    std::string              current;
    std::size_t              currentWidth = 0;

    for (const std::string& word : words) {
        const std::size_t wordWidth = CodepointCount(word);
        if (current.empty()) {
            current      = word;
            currentWidth = wordWidth;
            continue;
        }
        if (currentWidth + 1 + wordWidth <= width) {
            current += ' ';
            current += word;
            currentWidth += 1 + wordWidth;
        }
        else {
            lines.push_back(std::move(current));
            current      = word;
            currentWidth = wordWidth;
        }
    }
    if (!current.empty()) {
        lines.push_back(std::move(current));
    }
    return lines;
}

std::optional<std::pair<std::size_t, std::size_t>> FindParagraphRange(const text::ITextStorage& content,
                                                                      std::size_t               point) {
    const std::size_t lineCount = content.LineCount();
    if (lineCount == 0) {
        return std::nullopt;
    }

    std::size_t line = content.ByteOffsetToLine(std::min(point, content.ByteLength()));
    while (line < lineCount && IsBlankLine(content, line)) {
        ++line;
    }
    if (line >= lineCount) {
        return std::nullopt;
    }

    std::size_t first = line;
    while (first > 0 && !IsBlankLine(content, first - 1)) {
        --first;
    }
    std::size_t last = line;
    while (last + 1 < lineCount && !IsBlankLine(content, last + 1)) {
        ++last;
    }

    const std::size_t start = content.LineToByteOffset(first);
    const std::size_t end   = LineContentEnd(content, content.LineToByteOffset(last));
    return std::make_pair(start, end);
}

void FillParagraph(text::Buffer& buffer, std::size_t fillColumn, std::string_view commentPrefix) {
    const text::ITextStorage& content = buffer.Content();
    const auto                range   = FindParagraphRange(content, buffer.Point());
    if (!range) {
        return;
    }
    const auto [start, end] = *range;

    const std::size_t firstLine = content.ByteOffsetToLine(start);
    const std::size_t lastLine  = content.ByteOffsetToLine(end - 1); // end > start: a non-blank line has >=1 byte

    // Pass 1 (read-only): collect each line's raw text plus where its body
    // (past leading whitespace) starts, and whether *every* line carries
    // commentPrefix there -- a uniform check done before any stripping, so
    // a mixed paragraph never loses an earlier line's comment marker to a
    // later line's mismatch (the same two-pass shape toggle-line-comment's
    // own commented-vs-uncommented check uses).
    std::vector<std::string> lineTexts;
    std::vector<std::size_t> bodyStarts;
    lineTexts.reserve(lastLine - firstLine + 1);
    bodyStarts.reserve(lastLine - firstLine + 1);

    bool        commentMode = !commentPrefix.empty();
    std::string indent;

    for (std::size_t line = firstLine; line <= lastLine; ++line) {
        const std::size_t lineStart = content.LineToByteOffset(line);
        const std::size_t lineEnd   = LineContentEnd(content, lineStart);
        std::string       text      = content.Substring(lineStart, lineEnd - lineStart);
        const std::size_t wsEnd     = text.find_first_not_of(" \t");
        const std::size_t bodyStart = (wsEnd == std::string::npos) ? text.size() : wsEnd;

        if (line == firstLine) {
            indent = text.substr(0, bodyStart);
        }
        if (commentMode && !std::string_view(text).substr(bodyStart).starts_with(commentPrefix)) {
            commentMode = false;
        }

        bodyStarts.push_back(bodyStart);
        lineTexts.push_back(std::move(text));
    }

    // Pass 2: extract words, stripping the comment prefix per line only if
    // every line actually carried one. The FIRST line's own body (after any
    // comment-prefix strip) is also checked for a list marker -- see
    // DetectListMarker's own doc comment -- so it's never folded into the
    // ordinary word stream, and its width can become every OTHER line's
    // hang indent below.
    std::vector<std::string> words;
    std::string              listMarkerText; // verbatim, e.g. "- [ ] " -- empty when none detected
    for (std::size_t i = 0; i < lineTexts.size(); ++i) {
        std::string_view body = std::string_view(lineTexts[i]).substr(bodyStarts[i]);
        if (commentMode) {
            body.remove_prefix(commentPrefix.size());
            if (!body.empty() && body.front() == ' ') {
                body.remove_prefix(1);
            }
        }
        if (i == 0) {
            if (const std::optional<std::size_t> markerWidth = DetectListMarker(body)) {
                listMarkerText = std::string(body.substr(0, *markerWidth));
                body.remove_prefix(*markerWidth);
            }
        }
        AppendWords(body, words);
    }

    const std::string commentLeader = commentMode ? std::string(commentPrefix) + " " : std::string();
    // Both prefixes are the same codepoint WIDTH by construction -- the
    // continuation one just spells the marker's own width as plain spaces
    // instead of repeating it, the same "align under, don't repeat" rule a
    // real Markdown/Org formatter (or Emacs' own adaptive-fill-mode) uses.
    const std::string firstLinePrefix    = indent + commentLeader + listMarkerText;
    const std::string continuationPrefix = indent + commentLeader + std::string(listMarkerText.size(), ' ');
    const std::size_t prefixWidth        = CodepointCount(continuationPrefix);
    const std::size_t wrapWidth          = (fillColumn > prefixWidth) ? fillColumn - prefixWidth : 1;

    const std::vector<std::string> wrapped = WrapWords(words, wrapWidth);

    std::string replacement;
    if (wrapped.empty()) {
        // A comment-leader/list-marker-only paragraph (e.g. a lone "//" or
        // "-") has no words to wrap -- keep it alone rather than emitting
        // nothing.
        replacement = firstLinePrefix;
    }
    else {
        for (std::size_t i = 0; i < wrapped.size(); ++i) {
            if (i > 0) {
                replacement += '\n';
            }
            replacement += (i == 0) ? firstLinePrefix : continuationPrefix;
            replacement += wrapped[i];
        }
    }

    buffer.BeginUndoGroup();
    buffer.DeleteRange(start, end - start);
    buffer.InsertAt(start, replacement);
    buffer.SetPoint(start + replacement.size());
    buffer.EndUndoGroup();
}

} // namespace ned::editor
