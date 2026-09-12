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
    // org's highlighting is queries + the bundled capture classifiers
    // (Plugins/languages.janet) + :capture-spans -- no escape any more; what
    // stays here is what is genuinely a tree/line walk.
    RegisterModeEscape("org.indent", Indent);
    RegisterModeEscape("org.symbols", Symbols);
}

void RegisterBundledEscapes() {
    RegisterCLikeEscapes();
    RegisterMarkdownEscapes();
    RegisterOrgEscapes();
}

} // namespace ned::editor::languages
