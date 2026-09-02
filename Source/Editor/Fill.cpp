#include "Fill.h"

#include <algorithm>

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
                                                                        std::size_t                point) {
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
        std::string        text     = content.Substring(lineStart, lineEnd - lineStart);
        const std::size_t  wsEnd    = text.find_first_not_of(" \t");
        const std::size_t  bodyStart = (wsEnd == std::string::npos) ? text.size() : wsEnd;

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
    // every line actually carried one.
    std::vector<std::string> words;
    for (std::size_t i = 0; i < lineTexts.size(); ++i) {
        std::string_view body = std::string_view(lineTexts[i]).substr(bodyStarts[i]);
        if (commentMode) {
            body.remove_prefix(commentPrefix.size());
            if (!body.empty() && body.front() == ' ') {
                body.remove_prefix(1);
            }
        }
        AppendWords(body, words);
    }

    const std::string linePrefix  = commentMode ? indent + std::string(commentPrefix) + " " : indent;
    const std::size_t prefixWidth = CodepointCount(linePrefix);
    const std::size_t wrapWidth   = (fillColumn > prefixWidth) ? fillColumn - prefixWidth : 1;

    const std::vector<std::string> wrapped = WrapWords(words, wrapWidth);

    std::string replacement;
    if (wrapped.empty()) {
        // A comment-leader-only paragraph (e.g. a lone "//") has no words
        // to wrap -- keep the leader alone rather than emitting nothing.
        replacement = linePrefix;
    }
    else {
        for (std::size_t i = 0; i < wrapped.size(); ++i) {
            if (i > 0) {
                replacement += '\n';
            }
            replacement += linePrefix;
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
