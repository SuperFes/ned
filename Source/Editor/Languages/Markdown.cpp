#include "Escapes.h"

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "Editor/Injection.h"
#include "Editor/LanguageDefinition.h"
#include "Editor/ModeInternal.h"
#include "Editor/TreeSitter/IncrementalParse.h"
#include "Editor/TreeSitter/Node.h"
#include "Editor/TreeSitter/Parser.h"
#include "Editor/TreeSitter/Query.h"
#include "Editor/TreeSitter/Tree.h"

namespace ned::editor::languages {

namespace {

    // Markdown-highlighting follow-up. Which atx_h<N>_marker child type
    // (N = 1..6) an atx_heading node has -- a plain query capture can only
    // say "this is A heading", not which level, since every level shares
    // the same "(atx_heading (inline) @text.title)" pattern; the level only
    // shows up as which specific marker child is present.
    std::optional<int> AtxHeadingMarkerLevel(std::string_view childType) {
        static constexpr std::pair<std::string_view, int> kMarkers[] = {
            {"atx_h1_marker", 1},
            {"atx_h2_marker", 2},
            {"atx_h3_marker", 3},
            {"atx_h4_marker", 4},
            {"atx_h5_marker", 5},
            {"atx_h6_marker", 6},
        };
        for (const auto& [name, level] : kMarkers) {
            if (childType == name) {
                return level;
            }
        }
        return std::nullopt;
    }

    // Markdown-highlighting follow-up. Walks the real parsed tree (not just
    // query captures -- see AtxHeadingMarkerLevel's own doc comment) for the
    // handful of constructs a plain query can't express: ATX/setext heading
    // levels (whole-node span, matching Org's own whole-headline-line
    // convention) and GFM task-list checkboxes (reusing Org's own Checkbox
    // class -- same visual concept). Recurses over every child regardless
    // of match, matching-node checks first then recursing unconditionally
    // -- headings/checkboxes never nest, so this never double-counts.
    void CollectMarkdownStructuralSpans(const treesitter::Node& node, std::vector<HighlightSpan>& spans) {
        const std::string_view type = node.Type();
        if (type == "atx_heading") {
            for (std::size_t i = 0; i < node.ChildCount(); ++i) {
                if (const std::optional<int> level = AtxHeadingMarkerLevel(node.Child(i).Type())) {
                    spans.push_back(HighlightSpan{.startByte   = node.StartByte(),
                                                  .endByte     = node.EndByte(),
                                                  .syntaxClass = HeadlineLevelForStarCount(static_cast<std::size_t>(*level))});
                    break;
                }
            }
        }
        else if (type == "setext_heading") {
            for (std::size_t i = 0; i < node.ChildCount(); ++i) {
                const std::string_view childType = node.Child(i).Type();
                if (childType == "setext_h1_underline" || childType == "setext_h2_underline") {
                    const std::size_t level = (childType == "setext_h1_underline") ? 1 : 2;
                    spans.push_back(HighlightSpan{
                        .startByte = node.StartByte(), .endByte = node.EndByte(), .syntaxClass = HeadlineLevelForStarCount(level)});
                    break;
                }
            }
        }
        else if (type == "task_list_marker_checked" || type == "task_list_marker_unchecked") {
            spans.push_back(HighlightSpan{.startByte = node.StartByte(), .endByte = node.EndByte(), .syntaxClass = SyntaxClass::Checkbox});
        }

        for (std::size_t i = 0; i < node.ChildCount(); ++i) {
            CollectMarkdownStructuralSpans(node.Child(i), spans);
        }
    }

    // main-editor-sticky-scroll-markdown follow-up: unlike a tags.scm-driven
    // SymbolKindFunction (which reads its containment range straight off one
    // real AST node), a heading's "belongs under" relationship might look
    // like it needs synthesizing by hand -- ATX/setext headings themselves
    // are flat block-level nodes, no different level from each other in the
    // tree. But tree-sitter-markdown's own grammar (see grammar.js's
    // _section1.._section6 rules) already wraps each heading and everything
    // through the next equal-or-shallower heading in a real "section" node
    // that nests by level (a section's `repeat` only ever admits STRICTLY
    // DEEPER sub-sections as children, never an equal-or-shallower one) --
    // confirmed against the vendored grammar source, not assumed. So a
    // section's own [StartByte, EndByte) is already exactly the synthesized
    // range this would otherwise have to compute by hand from heading
    // levels, and its first token is always its own heading -- no separate
    // level bookkeeping needed at all. Pre-order (parent before children,
    // siblings left-to-right) walk order is what keeps the result already
    // sorted by startByte, Mode::symbolKind's own contract.
    void CollectMarkdownSectionMarkers(const treesitter::Node& node, std::vector<SymbolMarker>& markers) {
        if (node.Type() == "section") {
            markers.push_back(SymbolMarker{
                .startByte = node.StartByte(), .endByte = node.EndByte(), .kind = SymbolKind::Namespace, .name = std::string()});
        }
        for (std::size_t i = 0; i < node.ChildCount(); ++i) {
            CollectMarkdownSectionMarkers(node.Child(i), markers);
        }
    }
    // Markdown-highlighting follow-up: replaces the generic highlight closure
    // with the same passes plus one the query cannot express -- heading
    // levels and GFM checkboxes, from walking the real tree -- over the
    // parser/query/cache the generic build already constructed.
    void Highlight(Mode& mode, const LanguageDefinition&, const ModeBuildContext& context) {
        mode.highlight = [parser = context.parser, blockQuery = context.highlightQuery, injectionQuery = context.injectionQuery,
                          sharedParse = context.sharedParse, embeddedLanguageCache = context.embeddedLanguageCache,
                          languageKey = context.languageKey](std::string_view bufferText,
                                                             HighlightWindow  window) -> std::vector<HighlightSpan> {
            const treesitter::Tree& tree = sharedParse->Update(*parser, bufferText);
            if (tree.IsNull()) {
                return {};
            }
            const treesitter::Node root = tree.RootNode();

            // Concatenated in this order so a later, narrower span visually
            // wins over an earlier, broader one via HighlightSpan's own
            // documented "later wins" overlap rule -- e.g. bold text inside a
            // heading, or a checkbox inside a list item.
            std::vector<HighlightSpan> spans;

            // Pass 1: block-level query captures through the shared CaptureTable
            // -- "punctuation.special" (list markers/thematic break/heading
            // markers/blockquote marker) resolves through the definition's own
            // MarkupMarker default instead of CaptureTable's shared Punctuation
            // one, without a special case here.
            SpanCollector blockCollector;
            for (const treesitter::QueryCapture& capture : blockQuery->CapturesInRange(root, bufferText, window.startByte, window.endByte)) {
                if (!IsHighlightableCapture(capture.name)) {
                    continue;
                }
                blockCollector.Add(capture.name, capture.startByte, capture.endByte, SyntaxClassForCapture(capture.name, languageKey));
            }
            spans = blockCollector.Take();

            // Pass 2: heading levels + task-list checkboxes, from walking the
            // real tree rather than query captures.
            CollectMarkdownStructuralSpans(root, spans);

            // Pass 3: everything the real injections.scm expresses -- inline
            // formatting (bold/italic/strikethrough/code-span/links, injected
            // into "markdown-inline"), fenced code blocks (into whatever
            // language their info string names), html_block, and frontmatter.
            // Appended last so it wins over everything above, including a
            // heading's own whole-line span and Pass 1's whole-fenced-block
            // "text.literal" (String) span.
            if (injectionQuery) {
                CollectInjectedHighlightSpans(root, bufferText, *injectionQuery, *embeddedLanguageCache, spans, window);
            }

            return spans;
        };
    }

    // smart-indentation follow-up: hand-rolled, mirroring .highlight's own
    // bypass of the generic query-driven path above -- real Markdown list
    // continuation needs a hanging indent to the bullet's own content
    // COLUMN (e.g. "1. " = 3, "- " = 2), not level * a fixed width, so this
    // doesn't fit Editor/Indent.h's generic @indent/@dedent engine at all
    // (see that file's own header comment). Shares blockParser/sharedParse
    // with .highlight above -- one more closure reusing the same cached
    // parse, not a second reparse on the same Paint()/keystroke cycle.
    void Indent(Mode& mode, const LanguageDefinition&, const ModeBuildContext& context) {
        mode.indentColumn = [blockParser = context.parser, sharedParse = context.sharedParse](
                                std::string_view bufferText, std::size_t lineStart, std::size_t lineEnd) -> std::optional<int> {
            const treesitter::Tree& tree = sharedParse->Update(*blockParser, bufferText);
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

            treesitter::Node node = tree.RootNode().NamedDescendantForByteRange(contentStart, contentStart);
            if (node.IsNull()) {
                return 0;
            }

            // Fenced-code passthrough: content inside a ```-fenced block is
            // opaque non-Markdown text, not reflowed/recomputed from structure
            // at all -- copy whatever the previous line's own leading
            // whitespace already is, byte-for-byte (a deliberate, explicit
            // exception to "never naive-copy-the-line-above," justified because
            // fence content genuinely isn't Markdown structure to walk). Codepoint
            // count, not display column (Fill.h's own documented v1 scope cut,
            // same reasoning -- a leading run of plain spaces/tabs essentially
            // never needs real tab-expansion math to reproduce verbatim).
            for (treesitter::Node ancestor = node; !ancestor.IsNull(); ancestor = ancestor.Parent()) {
                if (ancestor.Type() != "code_fence_content") {
                    continue;
                }
                if (lineStart == 0) {
                    return 0;
                }
                // lineStart - 1 is the '\n' terminating the PREVIOUS line itself
                // (lineStart is always right after a real newline here) -- the
                // search for that line's own START has to look one byte further
                // back than that, for the newline terminating the line before
                // IT, or this finds lineStart right back again instead of the
                // previous line's start.
                const std::size_t searchFrom    = (lineStart <= 1) ? 0 : lineStart - 2;
                std::size_t       prevLineStart = bufferText.rfind('\n', searchFrom);
                prevLineStart                   = (prevLineStart == std::string_view::npos) ? 0 : prevLineStart + 1;
                int column                      = 0;
                for (std::size_t i = prevLineStart; i < bufferText.size() && (bufferText[i] == ' ' || bufferText[i] == '\t');
                     ++i) {
                    ++column;
                }
                return column;
            }

            // Otherwise: sum each enclosing list_item's own marker width (its
            // first child's byte length, e.g. "- " = 2, "10. " = 4 -- nested
            // lists stack additively) plus 2 per enclosing block_quote ("> ").
            // A list_item/block_quote is excluded from its OWN opening/marker
            // line (StartByte() == position) -- the same self-exclusion
            // Editor/Indent.h's generic engine needs for a bracket-language
            // container's own opening line, confirmed by the same kind of real
            // parse-tree check that caught that engine's own bugs. A lambda,
            // not an inline loop -- smart-blank-line-on-newline follow-up:
            // needs calling twice, see the rescue immediately below.
            const auto sumHangColumn = [](const treesitter::Node& startNode, std::size_t position) {
                int result = 0;
                for (treesitter::Node ancestor = startNode; !ancestor.IsNull(); ancestor = ancestor.Parent()) {
                    if (ancestor.Type() == "list_item") {
                        if (ancestor.StartByte() != position && ancestor.ChildCount() > 0) {
                            const treesitter::Node marker = ancestor.Child(0);
                            result += static_cast<int>(marker.EndByte() - marker.StartByte());
                        }
                    }
                    else if (ancestor.Type() == "block_quote") {
                        if (ancestor.StartByte() != position) {
                            result += 2;
                        }
                    }
                }
                return result;
            };

            int column = sumHangColumn(node, contentStart);

            // smart-blank-line-on-newline follow-up: a freshly inserted,
            // not-yet-typed blank line (lineStart == lineEnd) at the document's
            // own tail can fall entirely outside every list_item/block_quote's
            // own byte range, the same boundary gap Editor/Indent.h's generic
            // engine hit (confirmed via a real failing test, not assumed) --
            // Markdown's own list_item has no closing delimiter either, so
            // there's nothing here to accidentally rescue onto the WRONG side
            // of (unlike that engine's own dedent-range exclusion). Re-sum from
            // the last real, non-whitespace byte instead.
            if (lineStart == lineEnd && column == 0 && contentStart == bufferText.size() && contentStart > 0) {
                const std::size_t rescuePos = bufferText.find_last_not_of(" \t\n\r", contentStart - 1);
                if (rescuePos != std::string_view::npos) {
                    const treesitter::Node rescueNode = tree.RootNode().NamedDescendantForByteRange(rescuePos, rescuePos);
                    if (!rescueNode.IsNull()) {
                        column = sumHangColumn(rescueNode, rescuePos);
                    }
                }
            }

            // smart-blank-line-on-newline follow-up: a SECOND consecutive
            // Enter on an empty list-continuation line breaks out of the list
            // (real Markdown/Org editors' own convention) instead of hang-
            // indenting to the same column again forever. Checked only when
            // the ordinary computation above actually found a real hang column
            // (column > 0, i.e. we're genuinely inside a list/blockquote) --
            // never touches the fenced-code passthrough above (which already
            // returned early) or an ordinary column-0 result. A plain textual
            // check of the immediately preceding line, not tree-based --
            // deterministic, no parse-dump verification needed the way a
            // tree-sitter-driven rule would be.
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
    void Symbols(Mode& mode, const LanguageDefinition&, const ModeBuildContext& context) {
        mode.symbolKind = [blockParser = context.parser, sharedParse = context.sharedParse](std::string_view bufferText) -> std::vector<SymbolMarker> {
            const treesitter::Tree& tree = sharedParse->Update(*blockParser, bufferText);
            if (tree.IsNull()) {
                return {};
            }
            std::vector<SymbolMarker> markers;
            CollectMarkdownSectionMarkers(tree.RootNode(), markers);
            return markers;
        };
    }

} // namespace

void RegisterMarkdownEscapes() {
    RegisterModeEscape("markdown.highlight", Highlight);
    RegisterModeEscape("markdown.indent", Indent);
    RegisterModeEscape("markdown.symbols", Symbols);
}

} // namespace ned::editor::languages
