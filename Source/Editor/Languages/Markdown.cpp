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
} // namespace

void RegisterMarkdownEscapes() {
    // markdown's highlighting and section breadcrumbs are plain query
    // patterns now (Source/Languages/markdown/{highlights,tags}.janet --
    // the level IS which marker child is present, and the grammar's own
    // "section" nodes nest by level); the hanging list indent is the one
    // fact that is genuinely a tree walk.
    RegisterModeEscape("markdown.indent", Indent);
}

} // namespace ned::editor::languages
