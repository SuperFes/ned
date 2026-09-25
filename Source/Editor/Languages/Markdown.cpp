#include "Escapes.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "Editor/Grammar/IncrementalParse.h"
#include "Editor/Grammar/Node.h"
#include "Editor/Grammar/Parser.h"
#include "Editor/Grammar/Tree.h"
#include "Editor/IndentStyle.h"
#include "Editor/Injection.h"
#include "Editor/LanguageDefinition.h"
#include "Editor/ModeInternal.h"

namespace ned::editor::languages {

namespace {

    // The visual column of `byte` on its line, tabs to CommonMark's stops of 4.
    int ColumnOf(std::string_view text, std::size_t byte) {
        const std::size_t lineStart = text.rfind('\n', byte == 0 ? 0 : byte - 1);
        int               column    = 0;
        for (std::size_t i = (lineStart == std::string_view::npos || byte == 0) ? 0 : lineStart + 1; i < byte; ++i) {
            column = text[i] == '\t' ? column + 4 - column % 4 : column + 1;
        }
        return column;
    }

    bool IsListMarker(std::string_view type) {
        return type.starts_with("list_marker_");
    }

    // How far a list item's content sits from its marker: the marker token
    // carries the spaces up to its content, and a marker ending its line has
    // content one column past it.
    int MarkerWidth(const grammar::Node& item, std::string_view text) {
        std::optional<grammar::Node> marker;
        item.ForEachChild([&](const grammar::Node& child) {
            if (!marker && IsListMarker(child.Type())) {
                marker = child;
            }
        });
        if (!marker) {
            return 2;
        }
        const std::size_t end    = marker->EndByte();
        const bool        spaced = end > marker->StartByte() && (text[end - 1] == ' ' || text[end - 1] == '\t');
        return ColumnOf(text, end) - ColumnOf(text, marker->StartByte()) + (spaced ? 0 : 1);
    }

    // The column a list item's continuation lines hang at, from where its
    // marker is planned to sit rather than where it is: one indent step past
    // the marker, held to what CommonMark still reads as the item's content --
    // at least the marker's width, and short of the four more that make code.
    int Hang(const grammar::Node& item, int markerColumn, int step, std::string_view text) {
        const int width = MarkerWidth(item, text);
        return markerColumn + std::clamp(step, width, width + 3);
    }

    // Leading whitespace is the only thing a reindent rewrites, and in
    // Markdown most of it is meaning: four columns make code, a `>` line's
    // indent belongs to what encloses the quote, a list item owns exactly the
    // lines at its content column. So a line gets the column its containers
    // fix and nothing else:
    //   - code (fenced, indented) and HTML blocks, fences included: no
    //     opinion -- a reindent leaves them, Enter copies the line above;
    //   - a line opening with `>`: the content column of the innermost list
    //     item enclosing its outermost quote, else 0 (what follows the `>` is
    //     not leading whitespace);
    //   - a lazy continuation inside a quote: no opinion;
    //   - otherwise the hang of the innermost list item the line continues
    //     -- an item's own marker line takes its parent's -- else 0. A hang
    //     is one indent step past the marker (Hang), so nesting reads as
    //     depth, the way the rest of ned indents.
    void Indent(Mode& mode, const LanguageDefinition&, const ModeBuildContext& context) {
        mode.indentColumn = [blockParser = context.parser, sharedParse = context.sharedParse](
                                std::string_view text, std::size_t lineStart, std::size_t lineEnd,
                                const IndentOverride& bufferIndent) -> std::optional<int> {
            const grammar::Tree& tree = sharedParse->Update(*blockParser, text);
            if (tree.IsNull()) {
                return std::nullopt;
            }
            std::size_t contentStart = lineStart;
            while (contentStart < lineEnd && (text[contentStart] == ' ' || text[contentStart] == '\t')) {
                ++contentStart;
            }
            const bool blank = contentStart >= lineEnd;

            // Enter twice ends a list: a blank line after a blank line.
            if (blank && lineStart > 0) {
                const std::size_t previousEnd   = lineStart - 1;
                const std::size_t previousStart = previousEnd == 0 ? 0 : text.rfind('\n', previousEnd - 1) + 1;
                if (text.substr(previousStart, previousEnd - previousStart).find_first_not_of(" \t") == std::string_view::npos) {
                    return 0;
                }
            }

            // A blank line belongs to no block: read its containers off the
            // content just before it.
            std::size_t position = contentStart;
            if (blank) {
                const std::size_t before = lineStart == 0 ? std::string_view::npos : text.find_last_not_of(" \t\n\r", lineStart - 1);
                if (before == std::string_view::npos) {
                    return 0;
                }
                position = before;
            }

            grammar::Node node = tree.RootNode().NamedDescendantForByteRange(position, position);
            if (node.IsNull()) {
                return 0;
            }
            std::vector<grammar::Node> chain;
            node.AncestorChain(chain);
            chain.insert(chain.begin(), node);

            const auto verbatim = std::find_if(chain.begin(), chain.end(), [](const grammar::Node& ancestor) {
                const std::string_view type = ancestor.Type();
                return type == "fenced_code_block" || type == "indented_code_block" || type == "html_block";
            });
            if (verbatim != chain.end()) {
                return std::nullopt;
            }

            // Innermost first; the outermost quote bounds which list items count.
            const auto outermostQuote = std::find_if(chain.rbegin(), chain.rend(), [](const grammar::Node& ancestor) {
                return ancestor.Type() == "block_quote";
            });
            auto listSearchFrom = chain.begin();
            if (outermostQuote != chain.rend()) {
                if (!blank && text[contentStart] != '>') {
                    return std::nullopt; // lazy continuation
                }
                listSearchFrom = outermostQuote.base(); // just outside the quote
            }
            // The list items that decide this line, outermost first. Only
            // items outside every quote: past a `>` nothing is leading
            // whitespace, so the outermost item stands at column 0.
            std::vector<grammar::Node> items;
            for (auto it = listSearchFrom; it != chain.end(); ++it) {
                if (it->Type() == "list_item") {
                    items.insert(items.begin(), *it);
                }
            }
            // An item's own marker line sits at its parent's hang.
            if (!blank && !items.empty() && items.back().StartByte() == contentStart && outermostQuote == chain.rend()) {
                items.pop_back();
            }
            const int step   = bufferIndent.AppliedTo(EffectiveIndentStyle("markdown-mode")).width;
            int       column = 0;
            for (const grammar::Node& item : items) {
                column = Hang(item, column, step, text);
            }
            return column;
        };
    }

    // Trailing whitespace that is Markdown: two or more spaces ending any
    // paragraph line but its last are a hard line break, and a code block's
    // lines are its own text. Everything else trims freely.
    void KeptTrailingWhitespace(Mode& mode, const LanguageDefinition&, const ModeBuildContext& context) {
        mode.keptTrailingWhitespace = [parser = context.parser, sharedParse = context.sharedParse](std::string_view text) {
            std::vector<std::size_t> kept;
            const grammar::Tree&     tree = sharedParse->Update(*parser, text);
            if (tree.IsNull()) {
                return kept;
            }
            std::vector<std::size_t> lineStarts{0};
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '\n') {
                    lineStarts.push_back(i + 1);
                }
            }
            const auto lineOf = [&](std::size_t byte) {
                return static_cast<std::size_t>(std::upper_bound(lineStarts.begin(), lineStarts.end(), byte) - lineStarts.begin()) - 1;
            };
            const auto lineEnd = [&](std::size_t line) {
                return line + 1 < lineStarts.size() ? lineStarts[line + 1] - 1 : text.size();
            };
            // The last line holding any of [start, end)'s content.
            const auto lastContentLine = [&](std::size_t start, std::size_t end) {
                std::size_t last = end;
                while (last > start && (text[last - 1] == '\n' || text[last - 1] == ' ' || text[last - 1] == '\t')) {
                    --last;
                }
                return lineOf(last > start ? last - 1 : start);
            };
            tree.RootNode().WalkSubtree(
                [&](const grammar::Node& node, std::size_t) {
                    const std::string_view type = node.Type();
                    if ((type == "code_fence_content" || type == "indented_code_block") && node.EndByte() > node.StartByte()) {
                        // A fence's whitespace-only line is code too; an
                        // indented block ends at its last non-blank line.
                        const std::size_t last = type == "code_fence_content" ? lineOf(node.EndByte() - 1)
                                                                              : lastContentLine(node.StartByte(), node.EndByte());
                        for (std::size_t line = lineOf(node.StartByte()); line <= last; ++line) {
                            kept.push_back(line);
                        }
                    }
                    else if (type == "paragraph") {
                        const std::size_t last = lastContentLine(node.StartByte(), node.EndByte());
                        for (std::size_t line = lineOf(node.StartByte()); line < last; ++line) {
                            const std::size_t end = lineEnd(line);
                            if (end >= 2 && text[end - 1] == ' ' && text[end - 2] == ' ') {
                                kept.push_back(line);
                            }
                        }
                    }
                },
                [](const grammar::Node&, std::size_t) {});
            std::sort(kept.begin(), kept.end());
            kept.erase(std::unique(kept.begin(), kept.end()), kept.end());
            return kept;
        };
    }

    std::size_t Codepoints(std::string_view text) {
        return static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
    }

    // A word that would open a new block at the start of a line -- a bullet,
    // an ordinal, a heading or quote marker, a fence, an underline or break,
    // an HTML tag -- and so must not be wrapped onto one.
    bool OpensBlock(std::string_view word) {
        if (word == "-" || word == "+" || word == "*" || word.starts_with('>') || word.starts_with("```") ||
            word.starts_with("~~~")) {
            return true;
        }
        if (!word.empty() && word.size() <= 6 && word.find_first_not_of('#') == std::string_view::npos) {
            return true;
        }
        if (!word.empty() && word.find_first_not_of("-=*_") == std::string_view::npos) {
            return true;
        }
        if (word.size() >= 2 && word.size() <= 10 && (word.back() == '.' || word.back() == ')') &&
            word.substr(0, word.size() - 1).find_first_not_of("0123456789") == std::string_view::npos) {
            return true;
        }
        return word.size() >= 2 && word[0] == '<' && (std::isalpha(static_cast<unsigned char>(word[1])) || word[1] == '/' || word[1] == '!');
    }

    // Reflows the paragraph at `point` and nothing else: a heading, list item
    // marker, table row, fence or quote marker is structure, not words. The
    // first line keeps its own prefix (`> - [ ] `); later lines take the
    // paragraph's second line's, when it has the same quote depth, else the
    // first one's with every list marker turned to spaces. A hard line break
    // ends its line where it stands.
    std::optional<FillEdit> FillParagraphAt(const grammar::Tree& tree, std::string_view text, std::size_t point, std::size_t fillColumn) {
        std::size_t position = std::min(point, text.size());
        // From a blank line, the paragraph ahead (Emacs).
        const std::size_t lineStart = position == 0 ? 0 : text.rfind('\n', position - 1) + 1;
        const std::size_t lineEnd   = std::min(text.find('\n', lineStart), text.size());
        if (text.substr(lineStart, lineEnd - lineStart).find_first_not_of(" \t") == std::string_view::npos) {
            position = text.find_first_not_of(" \t\n", lineStart);
            if (position == std::string_view::npos) {
                return std::nullopt;
            }
        }
        else if (position == lineEnd && position > lineStart) {
            --position; // end of line: the text just before point
        }

        std::vector<grammar::Node> chain;
        const auto                 isParagraph = [](const grammar::Node& n) { return n.Type() == "paragraph"; };
        // On a list or quote marker, the paragraph that starts later on its line.
        const std::size_t positionLineEnd = std::min(text.find('\n', position), text.size());
        for (std::size_t at = position; at < positionLineEnd || at == position; ++at) {
            const grammar::Node node = tree.RootNode().NamedDescendantForByteRange(at, at);
            chain.clear();
            node.AncestorChain(chain);
            chain.insert(chain.begin(), node);
            if (std::any_of(chain.begin(), chain.end(), isParagraph)) {
                break;
            }
        }
        const auto paragraph = std::find_if(chain.begin(), chain.end(), isParagraph);
        if (paragraph == chain.end() || (paragraph + 1 != chain.end() && (paragraph + 1)->Type() == "setext_heading")) {
            return std::nullopt;
        }
        const int quoteDepth = static_cast<int>(std::count_if(paragraph, chain.end(), [](const grammar::Node& n) { return n.Type() == "block_quote"; }));

        // The paragraph's lines, each split into prefix and body. Its text
        // ends with its inline child: the node itself reaches over a quote's
        // following `>` blank line.
        std::size_t contentEnd = paragraph->EndByte();
        paragraph->ForEachChild([&](const grammar::Node& child) {
            if (child.Type() == "inline") {
                contentEnd = child.EndByte();
            }
        });
        while (contentEnd > paragraph->StartByte() && (text[contentEnd - 1] == '\n' || text[contentEnd - 1] == ' ' || text[contentEnd - 1] == '\t')) {
            --contentEnd;
        }
        const std::size_t firstLineStart = text.rfind('\n', paragraph->StartByte() == 0 ? 0 : paragraph->StartByte() - 1);
        const std::size_t start          = (firstLineStart == std::string_view::npos || paragraph->StartByte() == 0) ? 0 : firstLineStart + 1;
        const std::size_t end            = std::min(text.find('\n', contentEnd), text.size());
        struct Line {
            std::string_view prefix;
            std::string_view body;
            std::string_view hardBreak; // the trailing `  ` or `\` that ends a line, kept as written
        };
        std::vector<Line> lines;
        for (std::size_t at = start; at <= end;) {
            const std::size_t stop = std::min(text.find('\n', at), end);
            std::string_view  line = text.substr(at, stop - at);
            std::size_t       body = 0;
            if (at == start) {
                body = paragraph->StartByte() - start;
            }
            else {
                body = line.find_first_not_of(" \t");
                for (int depth = 0; depth < quoteDepth && body < line.size() && line[body] == '>'; ++depth) {
                    body = line.find_first_not_of(" \t", body + 1);
                    body = body == std::string_view::npos ? line.size() : body;
                }
                body = body == std::string_view::npos ? line.size() : body;
            }
            Line entry{line.substr(0, body), line.substr(body), {}};
            if (stop < end) {
                const std::size_t trimmed = entry.body.find_last_not_of(" \t");
                if (trimmed != std::string_view::npos && entry.body.size() - trimmed - 1 >= 2) {
                    entry.hardBreak = entry.body.substr(trimmed + 1);
                    entry.body      = entry.body.substr(0, trimmed + 1);
                }
                else if (!entry.body.empty() && entry.body.back() == '\\') {
                    entry.hardBreak = "\\";
                    entry.body.remove_suffix(1);
                }
            }
            lines.push_back(entry);
            at = stop + 1;
        }

        const std::string firstPrefix(lines.front().prefix);
        std::string       restPrefix;
        if (lines.size() > 1 && std::count(lines[1].prefix.begin(), lines[1].prefix.end(), '>') == quoteDepth) {
            restPrefix = std::string(lines[1].prefix);
        }
        else {
            // Every list marker (and task box) to spaces; quotes stay.
            restPrefix = firstPrefix;
            for (char& c : restPrefix) {
                if (c != '>' && c != '\t') {
                    c = ' ';
                }
            }
        }

        std::string out;
        bool        firstOut = true;
        std::string current;
        std::size_t currentWidth = 0;
        const auto  available    = [&] {
            return fillColumn > Codepoints(firstOut ? firstPrefix : restPrefix) ? fillColumn - Codepoints(firstOut ? firstPrefix : restPrefix) : 1;
        };
        const auto flush = [&](std::string_view hardBreak) {
            out += (firstOut ? firstPrefix : restPrefix) + current + std::string(hardBreak);
            firstOut = false;
            current.clear();
            currentWidth = 0;
        };
        for (std::size_t i = 0; i < lines.size(); ++i) {
            std::size_t at = 0;
            const std::string_view body = lines[i].body;
            while (at < body.size()) {
                at = body.find_first_not_of(" \t", at);
                if (at == std::string_view::npos) {
                    break;
                }
                const std::size_t stop = std::min(body.find_first_of(" \t", at), body.size());
                const std::string_view word = body.substr(at, stop - at);
                at = stop;
                const std::size_t width = Codepoints(word);
                if (!current.empty() && currentWidth + 1 + width > available() && !OpensBlock(word)) {
                    flush({});
                    out += '\n';
                }
                if (!current.empty()) {
                    current += ' ';
                    ++currentWidth;
                }
                current += word;
                currentWidth += width;
            }
            if (!lines[i].hardBreak.empty() || i + 1 == lines.size()) {
                flush(lines[i].hardBreak);
                if (i + 1 < lines.size()) {
                    out += '\n';
                }
            }
        }
        return FillEdit{start, end, std::move(out)};
    }

    void Fill(Mode& mode, const LanguageDefinition&, const ModeBuildContext& context) {
        mode.fillParagraph = [parser = context.parser, sharedParse = context.sharedParse](std::string_view text, std::size_t point,
                                                                                           std::size_t fillColumn) -> std::optional<FillEdit> {
            const grammar::Tree& tree = sharedParse->Update(*parser, text);
            return tree.IsNull() ? std::nullopt : FillParagraphAt(tree, text, point, fillColumn);
        };
    }

    // An ordered item's number: the value its marker spells and where the
    // digits sit. nullopt for a bullet item.
    struct ItemNumber {
        std::size_t   start = 0;
        std::size_t   end   = 0;
        unsigned long value = 0;
    };

    std::optional<ItemNumber> NumberOf(const grammar::Node& item, std::string_view text) {
        std::optional<ItemNumber> number;
        item.ForEachChild([&](const grammar::Node& child) {
            if (number || (child.Type() != "list_marker_dot" && child.Type() != "list_marker_parenthesis")) {
                return;
            }
            std::size_t start = child.StartByte();
            while (start < child.EndByte() && !std::isdigit(static_cast<unsigned char>(text[start]))) {
                ++start;
            }
            std::size_t end = start;
            while (end < child.EndByte() && std::isdigit(static_cast<unsigned char>(text[end]))) {
                ++end;
            }
            if (end > start) {
                number = ItemNumber{start, end, std::stoul(std::string(text.substr(start, end - start)))};
            }
        });
        return number;
    }

    // The innermost list item or list holding `position`.
    grammar::Node Enclosing(const grammar::Tree& tree, std::size_t position, std::string_view type) {
        grammar::Node node = tree.RootNode().NamedDescendantForByteRange(position, position);
        while (!node.IsNull() && node.Type() != type) {
            node = node.Parent();
        }
        return node;
    }

    // Every item of an ordered list numbered the same (`1.` throughout, so
    // reordering never renumbers) is a style of its own, which Enter keeps.
    bool NumbersEveryItemTheSame(const grammar::Node& list, std::string_view text) {
        std::optional<unsigned long> shared;
        int                          items = 0;
        bool                         same  = true;
        list.ForEachChild([&](const grammar::Node& item) {
            if (item.Type() != "list_item") {
                return;
            }
            const std::optional<ItemNumber> number = NumberOf(item, text);
            ++items;
            if (!number || (shared && *shared != number->value)) {
                same = false;
            }
            shared = number ? std::optional(number->value) : shared;
        });
        return same && items > 1;
    }

    std::vector<FillEdit> RenumberList(const grammar::Tree& tree, std::string_view text, std::size_t point) {
        const grammar::Node list = Enclosing(tree, point, "list");
        if (list.IsNull() || NumbersEveryItemTheSame(list, text)) {
            return {};
        }
        std::vector<ItemNumber> numbers;
        bool                    ordered = true;
        list.ForEachChild([&](const grammar::Node& item) {
            if (item.Type() != "list_item") {
                return;
            }
            const std::optional<ItemNumber> number = NumberOf(item, text);
            ordered                                = ordered && number;
            if (number) {
                numbers.push_back(*number);
            }
        });
        std::vector<FillEdit> edits;
        if (!ordered || numbers.empty()) {
            return edits;
        }
        for (std::size_t i = 1; i < numbers.size(); ++i) {
            const unsigned long want = numbers.front().value + i;
            if (numbers[i].value != want) {
                edits.push_back(FillEdit{numbers[i].start, numbers[i].end, std::to_string(want)});
            }
        }
        return edits;
    }

    // Enter on a list item's marker line or a quote line carries the
    // structure onto the new line: the same indent and `>` markers, the next
    // marker (the next number for an ordered item, an unchecked box for a
    // task) with the same spacing. On an item or quote line with nothing in
    // it, Enter ends it instead: the marker (or one `>`) comes off and no
    // line is added. Read off the line's text, and only where the parse
    // agrees it is that structure -- a `- ` in code or prose is just text.
    std::optional<LineContinuation> ContinueLine(const grammar::Tree& tree, std::string_view text, std::size_t point) {
        const std::size_t lineStart = point == 0 ? 0 : text.rfind('\n', point - 1) + 1;
        const std::size_t lineEnd   = std::min(text.find('\n', point), text.size());
        const std::string_view line = text.substr(lineStart, lineEnd - lineStart);

        std::size_t at = line.find_first_not_of(" \t");
        if (at == std::string_view::npos) {
            return std::nullopt;
        }
        std::size_t lastQuote = std::string_view::npos;
        while (at < line.size() && line[at] == '>') {
            lastQuote = at;
            at        = std::min(line.find_first_not_of(" \t", at + 1), line.size());
        }
        // Past the last `>` and the one space that belongs to it.
        const std::size_t quotesEnd = lastQuote == std::string_view::npos ? 0
                                    : lastQuote + 1 < line.size() && line[lastQuote + 1] == ' ' ? lastQuote + 2
                                                                                                 : lastQuote + 1;
        const std::size_t markerStart = at;

        // A bullet, or up to nine digits and `.`/`)`, then a space or the end.
        std::size_t markerEnd = markerStart;
        if (markerEnd < line.size() && (line[markerEnd] == '-' || line[markerEnd] == '*' || line[markerEnd] == '+')) {
            ++markerEnd;
        }
        else {
            while (markerEnd < line.size() && markerEnd - markerStart < 9 && std::isdigit(static_cast<unsigned char>(line[markerEnd]))) {
                ++markerEnd;
            }
            if (markerEnd == markerStart || markerEnd >= line.size() || (line[markerEnd] != '.' && line[markerEnd] != ')')) {
                markerEnd = markerStart;
            }
            else {
                ++markerEnd;
            }
        }
        bool isItem = markerEnd > markerStart && (markerEnd == line.size() || line[markerEnd] == ' ' || line[markerEnd] == '\t');
        if (isItem) {
            const grammar::Node marker = tree.RootNode().NamedDescendantForByteRange(lineStart + markerStart, lineStart + markerStart);
            isItem                     = !marker.IsNull() && IsListMarker(marker.Type());
        }
        // A later line of an item's paragraph starts the item's next item,
        // the same as its marker line would -- inside a quote too.
        const grammar::Node item = Enclosing(tree, lineStart + at, "list_item");
        if (!isItem && !item.IsNull() && item.StartByte() < lineStart) {
            const std::size_t contentAt = lineStart + at;
            if (point <= contentAt) {
                return std::nullopt;
            }
            const grammar::Node paragraph   = tree.RootNode().NamedDescendantForByteRange(contentAt, contentAt);
            bool                inParagraph = false;
            for (grammar::Node node = paragraph; !node.IsNull() && node.Type() != "list_item"; node = node.Parent()) {
                inParagraph = inParagraph || node.Type() == "paragraph";
            }
            const std::size_t markerLineEnd = std::min(text.find('\n', item.StartByte()), text.size());
            if (!inParagraph) {
                return std::nullopt;
            }
            const std::optional<LineContinuation> next = ContinueLine(tree, text, markerLineEnd);
            return next && !next->currentLine ? next : std::nullopt;
        }
        if (!isItem && lastQuote == std::string_view::npos) {
            return std::nullopt;
        }
        if (!isItem) {
            const grammar::Node quote = tree.RootNode().NamedDescendantForByteRange(lineStart + lastQuote, lineStart + lastQuote);
            if (quote.IsNull() || (quote.Type() != "block_quote_marker" && quote.Type() != "block_continuation" && quote.Type() != "block_quote")) {
                return std::nullopt;
            }
        }

        // The spaces after the marker, then a task box and its space.
        std::size_t contentStart = isItem ? markerEnd : quotesEnd;
        std::string checkbox;
        if (isItem) {
            contentStart = std::min(line.find_first_not_of(" \t", markerEnd), line.size());
            if (contentStart + 2 < line.size() && line[contentStart] == '[' && std::string_view(" xX").find(line[contentStart + 1]) != std::string_view::npos &&
                line[contentStart + 2] == ']' && (contentStart + 3 == line.size() || line[contentStart + 3] == ' ')) {
                const std::size_t afterBox = std::min(line.find_first_not_of(' ', contentStart + 3), line.size());
                checkbox                   = "[ ]" + std::string(line.substr(contentStart + 3, afterBox - contentStart - 3));
                contentStart               = afterBox;
            }
        }
        if (point - lineStart < contentStart) {
            return std::nullopt; // Enter before the content: an ordinary newline above it
        }

        const auto trimmed = [](std::string_view s) {
            return std::string(s.substr(0, s.find_last_not_of(" \t") == std::string_view::npos ? 0 : s.find_last_not_of(" \t") + 1));
        };
        if (line.find_first_not_of(" \t", contentStart) == std::string_view::npos) {
            return LineContinuation{{}, isItem ? trimmed(line.substr(0, markerStart)) : trimmed(line.substr(0, lastQuote))};
        }

        std::string prefix(line.substr(0, isItem ? markerStart : quotesEnd));
        if (isItem) {
            const std::string_view marker = line.substr(markerStart, markerEnd - markerStart);
            if (std::isdigit(static_cast<unsigned char>(marker.front()))) {
                const unsigned long number = std::stoul(std::string(marker.substr(0, marker.size() - 1)));
                const grammar::Node list   = Enclosing(tree, lineStart + markerStart, "list");
                const bool          same   = !list.IsNull() && NumbersEveryItemTheSame(list, text);
                prefix += std::to_string(same ? number : number + 1) + marker.back();
            }
            else {
                prefix += marker;
            }
            const std::size_t spacesEnd = std::min(line.find_first_not_of(" \t", markerEnd), line.size());
            prefix += std::string(line.substr(markerEnd, spacesEnd - markerEnd));
            if (prefix.back() != ' ' && prefix.back() != '\t') {
                prefix += ' ';
            }
            prefix += checkbox;
        }
        return LineContinuation{std::move(prefix), std::nullopt};
    }

    void Continue(Mode& mode, const LanguageDefinition&, const ModeBuildContext& context) {
        mode.continueLine = [parser = context.parser, sharedParse = context.sharedParse](std::string_view text,
                                                                                         std::size_t point) -> std::optional<LineContinuation> {
            const grammar::Tree& tree = sharedParse->Update(*parser, text);
            return tree.IsNull() ? std::nullopt : ContinueLine(tree, text, std::min(point, text.size()));
        };
        mode.renumberList = [parser = context.parser, sharedParse = context.sharedParse](std::string_view text,
                                                                                         std::size_t      point) -> std::vector<FillEdit> {
            const grammar::Tree& tree = sharedParse->Update(*parser, text);
            return tree.IsNull() ? std::vector<FillEdit>{} : RenumberList(tree, text, std::min(point, text.size()));
        };
    }

    // A heading's section runs to the next heading of the same or a shallower
    // level. The grammar can only open a section at an ATX heading -- a
    // setext heading is one only once its underline is read -- so it leaves
    // setext headings loose and lets an ATX section run past a shallower
    // setext one; the extents are computed here from the headings instead.
    // Only headings outside list items and block quotes head sections.
    struct Section {
        std::size_t start;
        std::size_t end;
    };

    int HeadingLevel(const grammar::Node& heading) {
        int level = 0;
        heading.ForEachChild([&](const grammar::Node& child) {
            const std::string_view type = child.Type();
            if (type == "setext_h1_underline") {
                level = 1;
            }
            else if (type == "setext_h2_underline") {
                level = 2;
            }
            else if (type.starts_with("atx_h") && type.ends_with("_marker") && type.size() == 13) {
                level = type[5] - '0';
            }
        });
        return level;
    }

    void CollectHeadings(const grammar::Node& node, std::vector<std::pair<grammar::Node, int>>& headings) {
        node.ForEachChild([&](const grammar::Node& child) {
            const std::string_view type = child.Type();
            if (type == "atx_heading" || type == "setext_heading") {
                if (const int level = HeadingLevel(child); level > 0) {
                    headings.emplace_back(child, level);
                }
            }
            else if (type == "section") {
                CollectHeadings(child, headings);
            }
        });
    }

    std::vector<Section> SectionsOf(const grammar::Tree& tree, std::string_view text) {
        std::vector<std::pair<grammar::Node, int>> headings;
        CollectHeadings(tree.RootNode(), headings);
        std::vector<Section> sections;
        for (std::size_t i = 0; i < headings.size(); ++i) {
            std::size_t end = text.size();
            for (std::size_t j = i + 1; j < headings.size(); ++j) {
                if (headings[j].second <= headings[i].second) {
                    end = headings[j].first.StartByte();
                    break;
                }
            }
            sections.push_back(Section{.start = headings[i].first.StartByte(), .end = end});
        }
        return sections;
    }

    void Sections(Mode& mode, const LanguageDefinition&, const ModeBuildContext& context) {
        const auto parser      = context.parser;
        const auto sharedParse = context.sharedParse;
        if (mode.symbolKind) {
            // The tags query names each heading; its range becomes the section's.
            mode.symbolKind = [headings = mode.symbolKind, parser, sharedParse](std::string_view text) {
                std::vector<SymbolMarker> markers = headings(text);
                const grammar::Tree&      tree    = sharedParse->Update(*parser, text);
                if (tree.IsNull()) {
                    return markers;
                }
                const std::vector<Section> sections = SectionsOf(tree, text);
                for (SymbolMarker& marker : markers) {
                    const auto section = std::ranges::find(sections, marker.startByte, &Section::start);
                    if (section != sections.end()) {
                        marker.endByte = section->end;
                    }
                }
                return markers;
            };
            // A section can enclose the window from far above it, so the
            // windowed form filters the whole-document answer.
            mode.symbolKindInWindow = [whole = mode.symbolKind](std::string_view text, HighlightWindow window) {
                std::vector<SymbolMarker> markers = whole(text);
                std::erase_if(markers, [&](const SymbolMarker& marker) {
                    return marker.endByte <= window.startByte || marker.startByte >= window.endByte;
                });
                return markers;
            };
        }
        mode.fold = [blocks = mode.fold, parser, sharedParse](std::string_view text) {
            std::vector<std::pair<std::size_t, std::size_t>> ranges = blocks ? blocks(text) : std::vector<std::pair<std::size_t, std::size_t>>{};
            const grammar::Tree&                             tree   = sharedParse->Update(*parser, text);
            if (!tree.IsNull()) {
                for (const Section& section : SectionsOf(tree, text)) {
                    // Like every fold: it ends at its last content, and one
                    // that fits on its heading's line is none.
                    std::size_t end = section.end;
                    while (end > section.start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
                        --end;
                    }
                    if (text.substr(section.start, end - section.start).find('\n') != std::string_view::npos) {
                        ranges.emplace_back(section.start, end);
                    }
                }
            }
            std::sort(ranges.begin(), ranges.end());
            return ranges;
        };
    }

    // main-editor-sticky-scroll-markdown follow-up: shares blockParser/
    // sharedParse with .highlight/.indentColumn above -- no extra reparse on
    // a Paint() cycle that also needs one of those. See
    // CollectMarkdownSectionMarkers' own doc comment for why a section's
    // real tree range is already the synthesized "runs until the next
    // equal-or-shallower heading" extent sticky scroll needs, with no
    // per-level bookkeeping here. Reuses SymbolKind::Namespace (the "§"
    // glyph already reads naturally as "section") rather than a new kind --
    // heading level itself doesn't need representing separately, since the
    // sticky row's own reduced-signature text (the heading's real source
    // line, "#"/"##"/... markers included) already shows it.
} // namespace

void RegisterMarkdownEscapes() {
    // markdown's highlighting and section breadcrumbs are plain query
    // patterns now (Source/Languages/markdown/{highlights,tags}.janet --
    // the level IS which marker child is present, and the grammar's own
    // "section" nodes nest by level); the hanging list indent is the one
    // fact that is genuinely a tree walk.
    RegisterModeEscape("markdown.indent", Indent);
    RegisterModeEscape("markdown.trailing-whitespace", KeptTrailingWhitespace);
    RegisterModeEscape("markdown.fill", Fill);
    RegisterModeEscape("markdown.continue", Continue);
    RegisterModeEscape("markdown.sections", Sections);
}

} // namespace ned::editor::languages
