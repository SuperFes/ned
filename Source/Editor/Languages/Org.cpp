#include "Escapes.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Injection.h"
#include "Editor/LanguageDefinition.h"
#include "Editor/ModeInternal.h"
#include "Editor/Org.h"
#include "Editor/TreeSitter/IncrementalParse.h"
#include "Editor/TreeSitter/Node.h"
#include "Editor/TreeSitter/Parser.h"
#include "Editor/TreeSitter/Query.h"
#include "Editor/TreeSitter/Tree.h"

namespace ned::editor::languages {

namespace {

    // Org-mode syntax-highlighting follow-up: replaces the generic closure
    // with one resolving two capture names directly -- headline level is a
    // count of `*` characters and TODO-vs-DONE compares captured text against
    // org::TodoKeywords(), a list configured from Janet at runtime; neither is
    // expressible as a query predicate -- over the parser/query/cache the
    // generic build already constructed.
    void Highlight(Mode& mode, const LanguageDefinition&, const ModeBuildContext& context) {
        mode.highlight = [parser = context.parser, query = context.highlightQuery, injectionQuery = context.injectionQuery,
                          embeddedLanguageCache = context.embeddedLanguageCache, sharedParse = context.sharedParse,
                          languageKey = context.languageKey](std::string_view bufferText, HighlightWindow window) -> std::vector<HighlightSpan> {
            const treesitter::Tree& tree = sharedParse->Update(*parser, bufferText);
            if (tree.IsNull()) {
                return {};
            }
            const treesitter::Node                      root     = tree.RootNode();
            const std::vector<treesitter::QueryCapture> captures = query->CapturesInRange(root, bufferText, window.startByte, window.endByte);

            // Four passes, concatenated in this order so a later, narrower
            // span visually wins over an earlier, broader one via
            // HighlightSpan's own documented "later wins" rule -- e.g. a tag or
            // a TODO/DONE keyword sitting inside a HeadlineLevelN span that
            // covers the whole headline line.
            std::vector<HighlightSpan> spans;

            // Pass 1: "org.headline.stars" -> cyclic heading level, covering
            // the WHOLE headline line (stars through its own end-of-line, not
            // just the stars themselves) -- a headline reads as one visual
            // unit, matching real Org's own convention.
            for (const treesitter::QueryCapture& capture : captures) {
                if (capture.name != "org.headline.stars") {
                    continue;
                }
                const std::size_t starCount  = capture.endByte - capture.startByte; // stars are literal '*' bytes, one byte each
                const std::size_t newlinePos = bufferText.find('\n', capture.startByte);
                const std::size_t lineEnd    = (newlinePos == std::string_view::npos) ? bufferText.size() : newlinePos;
                spans.push_back(HighlightSpan{
                    .startByte   = capture.startByte,
                    .endByte     = lineEnd,
                    .syntaxClass = HeadlineLevelForStarCount(starCount),
                });
            }

            // Pass 2: "org.keyword.candidate" -> TodoKeyword/DoneKeyword, but
            // only for an EXACT match against org::TodoKeywords()'s own
            // configured list -- a headline whose first word merely isn't a
            // configured keyword gets no span here at all, falling through to
            // Default, the same "exact match or nothing" rule Org.cpp's own
            // ParseHeadlineLine already applies. The LAST configured keyword is
            // treated as the "done" state, everything earlier as "still open"
            // -- the standard single-sequence Org convention, no new config
            // surface needed.
            const std::vector<std::string>& todoKeywords = org::TodoKeywords();
            for (const treesitter::QueryCapture& capture : captures) {
                if (capture.name != "org.keyword.candidate") {
                    continue;
                }
                const std::string_view candidate = bufferText.substr(capture.startByte, capture.endByte - capture.startByte);
                for (std::size_t i = 0; i < todoKeywords.size(); ++i) {
                    if (todoKeywords[i] == candidate) {
                        spans.push_back(HighlightSpan{
                            .startByte   = capture.startByte,
                            .endByte     = capture.endByte,
                            .syntaxClass = (i + 1 == todoKeywords.size()) ? SyntaxClass::DoneKeyword : SyntaxClass::TodoKeyword,
                        });
                        break;
                    }
                }
            }

            // Pass 3: everything else, through the same shared, generic
            // CaptureTable()/SyntaxClassForCapture() mapping every other
            // bundled grammar's Mode already uses.
            SpanCollector genericCollector;
            for (const treesitter::QueryCapture& capture : captures) {
                if (capture.name == "org.headline.stars" || capture.name == "org.keyword.candidate") {
                    continue;
                }
                genericCollector.Add(capture.name, capture.startByte, capture.endByte, SyntaxClassForCapture(capture.name, languageKey));
            }
            for (const HighlightSpan& span : genericCollector.Take()) {
                spans.push_back(span);
            }

            // Pass 4: real per-language highlighting inside #+BEGIN_SRC/
            // #+BEGIN_EXPORT block bodies, appended last so it wins over
            // whatever Pass 3's generic capture table resolved the block's
            // "contents" node to (typically Default -- OrgHighlights.scm has no
            // pattern for it at all).
            if (injectionQuery) {
                CollectInjectedHighlightSpans(root, bufferText, *injectionQuery, *embeddedLanguageCache, spans, window);
            }

            return spans;
        };
    }

    // smart-indentation follow-up: hand-rolled, mirroring Languages/Markdown.cpp's
    // own bespoke closure -- real Org list continuation needs a hanging
    // indent to the bullet's own content COLUMN (checked against a real
    // parse dump: "listitem"'s children are [bullet, ...body], with the
    // body's own start byte -- NOT bullet's own end byte, there's a
    // separating space in between not covered by either -- giving the real
    // hang width), so this doesn't fit Editor/Indent.h's generic
    // @indent/@dedent engine any more than Markdown's own list handling
    // does. Headline body text is deliberately NOT indented under its own
    // stars here -- real Org's own long-standing convention keeps body text
    // flush regardless of heading level, unlike list continuation. Shares
    // parser/sharedParse with highlight above.
    void Indent(Mode& mode, const LanguageDefinition&, const ModeBuildContext& context) {
        mode.indentColumn = [parser = context.parser, sharedParse = context.sharedParse](
                                std::string_view bufferText, std::size_t lineStart, std::size_t lineEnd) -> std::optional<int> {
            const treesitter::Tree& tree = sharedParse->Update(*parser, bufferText);
            if (tree.IsNull()) {
                return std::nullopt;
            }

            std::size_t contentStart = lineEnd; // default: the whole line is blank
            for (std::size_t i = lineStart; i < lineEnd; ++i) {
                if (bufferText[i] != ' ' && bufferText[i] != '\t') {
                    contentStart = i;
                    break;
                }
            }

            const treesitter::Node node = tree.RootNode().NamedDescendantForByteRange(contentStart, contentStart);
            if (node.IsNull()) {
                return 0;
            }

            // Sum each enclosing listitem's own hang width (its second child's
            // byte offset from its own start -- bullet plus whatever separates
            // it from the body, e.g. "- " = 2, "1. " = 3) -- nested lists stack
            // additively. A listitem is excluded from its OWN bullet line
            // (StartByte() == position), the same self-exclusion Editor/
            // Indent.h's generic engine needs for a container's own opening
            // line. A lambda, not an inline loop -- smart-blank-line-on-newline
            // follow-up: needs calling twice, see the rescue immediately below.
            const auto sumHangColumn = [](const treesitter::Node& startNode, std::size_t position) {
                int result = 0;
                for (treesitter::Node ancestor = startNode; !ancestor.IsNull(); ancestor = ancestor.Parent()) {
                    if (ancestor.Type() != "listitem") {
                        continue;
                    }
                    if (ancestor.StartByte() == position || ancestor.ChildCount() < 2) {
                        continue;
                    }
                    const treesitter::Node body = ancestor.Child(1);
                    result += static_cast<int>(body.StartByte() - ancestor.StartByte());
                }
                return result;
            };

            int column = sumHangColumn(node, contentStart);

            // smart-blank-line-on-newline follow-up: mirrors Languages/Markdown.cpp's
            // own rescue -- a freshly inserted, not-yet-typed blank line at the
            // document's own tail can fall entirely outside every listitem's
            // own byte range. Re-sum from the last real, non-whitespace byte
            // instead when that happens; see Languages/Markdown.cpp's own comment for
            // the full reasoning.
            if (lineStart == lineEnd && column == 0 && contentStart == bufferText.size() && contentStart > 0) {
                const std::size_t rescuePos = bufferText.find_last_not_of(" \t\n\r", contentStart - 1);
                if (rescuePos != std::string_view::npos) {
                    const treesitter::Node rescueNode = tree.RootNode().NamedDescendantForByteRange(rescuePos, rescuePos);
                    if (!rescueNode.IsNull()) {
                        column = sumHangColumn(rescueNode, rescuePos);
                    }
                }
            }

            // smart-blank-line-on-newline follow-up: mirrors Languages/Markdown.cpp's
            // own addition just above -- a second consecutive Enter on an
            // empty list-continuation line breaks out of the list rather than
            // hang-indenting to the same column again. See that comment for
            // the full reasoning; only duplicated here for the same reason
            // this closure's own contentStart computation already is.
            if (lineStart == lineEnd && column > 0 && lineStart > 0) {
                const std::size_t searchFrom    = (lineStart <= 1) ? 0 : lineStart - 2;
                std::size_t       prevLineStart = bufferText.rfind('\n', searchFrom);
                prevLineStart                   = (prevLineStart == std::string_view::npos) ? 0 : prevLineStart + 1;
                bool prevLineBlank              = true;
                for (std::size_t i = prevLineStart; i < lineStart - 1; ++i) {
                    if (bufferText[i] != ' ' && bufferText[i] != '\t') {
                        prevLineBlank = false;
                        break;
                    }
                }
                if (prevLineBlank) {
                    return 0;
                }
            }
            return column;
        };
    }

    // main-editor-sticky-scroll-markdown follow-up: unlike Languages/Markdown.cpp's
    // real tree-sitter "section" nesting, Org headlines have no equivalent
    // in Ned's own tree-sitter-ned-org grammar to lean on -- so this
    // synthesizes the same "runs until the next equal-or-shallower
    // headline" extent org::SubtreeEndLine already defines (in line terms,
    // for fold visibility), directly in byte terms from ParseOutline's
    // flat, level-tagged list. No tree-sitter parse at all: ParseOutline is
    // already a pure, tree-free line scan, so this needs neither parser nor
    // sharedParse. todoKeywords left at its default (DefaultTodoKeywords())
    // deliberately, not the live org::TodoKeywords() -- a headline's own
    // level/lineStartByte (all this reads) never depend on which words are
    // configured as TODO keywords, only title/todoKeyword do, so there's no
    // reason to take a dependency on runtime-mutable global state here.
    // Reuses SymbolKind::Namespace, Languages/Markdown.cpp's own choice for the same
    // reason: heading level already shows up in the sticky row's own
    // reduced-signature text (the stars themselves), no separate kind
    // needed to carry it.
    void Symbols(Mode& mode, const LanguageDefinition&, const ModeBuildContext&) {
        mode.symbolKind = [](std::string_view bufferText) -> std::vector<SymbolMarker> {
            const std::vector<org::Headline> headlines = org::ParseOutline(bufferText);
            std::vector<SymbolMarker>        markers;
            markers.reserve(headlines.size());
            for (std::size_t i = 0; i < headlines.size(); ++i) {
                std::size_t endByte = bufferText.size();
                for (std::size_t j = i + 1; j < headlines.size(); ++j) {
                    if (headlines[j].level <= headlines[i].level) {
                        endByte = headlines[j].lineStartByte;
                        break;
                    }
                }
                markers.push_back(SymbolMarker{.startByte = headlines[i].lineStartByte,
                                               .endByte   = endByte,
                                               .kind      = SymbolKind::Namespace,
                                               .name      = std::string()});
            }
            return markers;
        };
    }

} // namespace

void RegisterOrgEscapes() {
    RegisterModeEscape("org.highlight", Highlight);
    RegisterModeEscape("org.indent", Indent);
    RegisterModeEscape("org.symbols", Symbols);
}

void RegisterBundledEscapes() {
    RegisterCLikeEscapes();
    RegisterMarkdownEscapes();
    RegisterOrgEscapes();
}

} // namespace ned::editor::languages
