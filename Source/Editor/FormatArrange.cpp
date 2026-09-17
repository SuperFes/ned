#include "FormatArrange.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

#include "FormatRules.h"

namespace ned::editor {

namespace {

    std::string FoldCase(std::string_view text) {
        std::string folded(text);
        for (char& c : folded) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return folded;
    }

    struct LineSpan {
        std::size_t start;
        std::size_t endInclusiveOfNewline;
        bool        hasTrailingNewline;
    };

    // A capture's own node span sometimes already includes its own trailing
    // newline (cpp's preproc_include: verified live it spans through the
    // '\n' itself, unlike JavaScript's import_statement, which stops at the
    // ';') and sometimes doesn't -- stripping at most ONE trailing newline
    // before checking for an EMBEDDED one (a real multi-line span) or using
    // the text as a sort key is what makes both shapes behave identically
    // here, rather than every single-line cpp capture being misread as
    // "multi-line" and declined outright (a real bug this rollout's own
    // test suite caught live, not by inspection).
    std::string_view WithoutOneTrailingNewline(std::string_view span) {
        if (!span.empty() && span.back() == '\n') {
            span.remove_suffix(1);
        }
        return span;
    }

    LineSpan FullLineOf(std::string_view text, std::size_t at) {
        const std::size_t lineStart = LineStartOf(text, at);
        const std::size_t newline   = text.find('\n', lineStart);
        if (newline == std::string_view::npos) {
            return LineSpan{lineStart, text.size(), false};
        }
        return LineSpan{lineStart, newline + 1, true};
    }

} // namespace

std::vector<FormatTextEdit> ComputeArrangeEdits(std::string_view text, std::string_view languageKey,
                                                const std::vector<FormatCapture>& captures) {
    std::vector<FormatTextEdit> edits;

    std::unordered_map<std::string, std::vector<const FormatCapture*>> byName;
    for (const FormatCapture& capture : captures) {
        if (capture.startByte >= capture.endByte || capture.endByte > text.size()) {
            continue; // degenerate span -- never expected from a real query
        }
        const std::string_view span = text.substr(capture.startByte, capture.endByte - capture.startByte);
        if (WithoutOneTrailingNewline(span).find('\n') != std::string_view::npos) {
            continue; // a multi-line import -- decline rather than approximate, see this file's own header comment
        }
        const ArrangeRuleValue rule = ArrangeRuleFor(capture.name, languageKey);
        if (!rule.enabled || !*rule.enabled) {
            continue; // unconfigured -- no built-in default, nothing forced
        }
        byName[capture.name].push_back(&capture);
    }

    for (auto& [name, group] : byName) {
        std::sort(group.begin(), group.end(),
                  [](const FormatCapture* a, const FormatCapture* b) { return a->startByte < b->startByte; });
        const bool caseInsensitive =
            ArrangeRuleFor(name, languageKey).caseInsensitive.value_or(false);

        std::size_t i = 0;
        while (i < group.size()) {
            std::size_t prevLineStart = LineStartOf(text, group[i]->startByte);
            std::size_t j             = i + 1;
            while (j < group.size()) {
                const std::size_t thisLineEnd = text.find('\n', prevLineStart);
                if (thisLineEnd == std::string_view::npos) {
                    break;
                }
                const std::size_t nextLineStart = thisLineEnd + 1;
                if (LineStartOf(text, group[j]->startByte) != nextLineStart) {
                    break; // not on the very next line -- a blank/other line intervenes
                }
                prevLineStart = nextLineStart;
                ++j;
            }

            if (j - i >= 2) { // a run of one has nothing to reorder against
                const LineSpan lastLine = FullLineOf(text, group[j - 1]->startByte);
                if (!lastLine.hasTrailingNewline) {
                    // The run's own last member is the literal last line of
                    // the buffer with no trailing newline -- decline rather
                    // than risk losing or duplicating that missing
                    // terminator across a reorder (a narrow edge case: the
                    // Hygiene pass already ensures a final newline on any
                    // buffer this pass would otherwise touch in practice).
                    i = j;
                    continue;
                }

                struct Entry {
                    std::string      key;
                    std::string_view line;
                };
                std::vector<Entry> entries;
                entries.reserve(j - i);
                for (std::size_t k = i; k < j; ++k) {
                    const LineSpan         span     = FullLineOf(text, group[k]->startByte);
                    const std::string_view lineText = text.substr(span.start, span.endInclusiveOfNewline - span.start);
                    const std::string_view rawKey   = WithoutOneTrailingNewline(
                        text.substr(group[k]->startByte, group[k]->endByte - group[k]->startByte));
                    entries.push_back(Entry{caseInsensitive ? FoldCase(rawKey) : std::string(rawKey), lineText});
                }

                std::vector<std::size_t> order(entries.size());
                for (std::size_t k = 0; k < order.size(); ++k) {
                    order[k] = k;
                }
                std::stable_sort(order.begin(), order.end(),
                                 [&](std::size_t a, std::size_t b) { return entries[a].key < entries[b].key; });

                bool alreadySorted = true;
                for (std::size_t k = 0; k < order.size(); ++k) {
                    if (order[k] != k) {
                        alreadySorted = false;
                        break;
                    }
                }
                if (alreadySorted) {
                    i = j;
                    continue;
                }

                std::string desired;
                for (const std::size_t index : order) {
                    desired.append(entries[index].line);
                }
                const std::size_t blockStart = LineStartOf(text, group[i]->startByte);
                edits.push_back(FormatTextEdit{blockStart, lastLine.endInclusiveOfNewline, std::move(desired)});
            }

            i = j;
        }
    }

    std::sort(edits.begin(), edits.end(), [](const FormatTextEdit& a, const FormatTextEdit& b) { return a.start < b.start; });
    return edits;
}

} // namespace ned::editor
