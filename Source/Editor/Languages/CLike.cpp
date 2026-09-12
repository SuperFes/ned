#include "Escapes.h"

#include <algorithm>
#include <array>
#include <string_view>

#include "Editor/LanguageDefinition.h"
#include "Editor/ModeInternal.h"
#include "Editor/TreeSitter/IncrementalParse.h"
#include "Editor/TreeSitter/Node.h"
#include "Editor/TreeSitter/Parser.h"
#include "Editor/TreeSitter/Tree.h"

namespace ned::editor::languages {

namespace {

    // Debugging wishlist (line-inspect follow-up), Tier 2: real, grammar-
    // verified compound-expression node types C and C++ share (confirmed
    // against both vendored grammars' own node-types.json directly, not
    // guessed) -- overrides the generic Tier-1 identifier-only default with
    // this richer predicate.
    bool IsCLikeExpressionNodeType(std::string_view type) {
        static constexpr std::array<std::string_view, 11> kTypes = {
            "identifier",
            "call_expression",
            "field_expression",
            "subscript_expression",
            "binary_expression",
            "unary_expression",
            "pointer_expression",
            "cast_expression",
            "conditional_expression",
            "assignment_expression",
            "parenthesized_expression",
        };
        return std::find(kTypes.begin(), kTypes.end(), type) != kTypes.end();
    }

    void LineInspect(Mode& mode, const LanguageDefinition&, const ModeBuildContext& context) {
        mode.lineInspect = BuildLineInspectFunction(*context.language, IsCLikeExpressionNodeType);
    }

    // tree-sitter-cpp parses an unexpanded TEST_CASE("x") { ... } as a
    // call_expression statement with the body left as a *sibling*
    // compound_statement (the macro isn't valid C++ unexpanded) -- extend
    // each discovered test over an immediately adjacent compound_statement
    // sibling so point-inside-the-body still resolves to this test for
    // run-test-at-point. Wraps the generic closure and re-reads the tree it
    // just parsed from the shared cache, so this costs no second parse.
    void TestBody(Mode& mode, const LanguageDefinition&, const ModeBuildContext& context) {
        if (!mode.testDiscovery) {
            return;
        }
        mode.testDiscovery = [inner = std::move(mode.testDiscovery), parser = context.parser,
                              sharedParse = context.sharedParse](std::string_view bufferText) -> std::vector<TestMarker> {
            std::vector<TestMarker> markers = inner(bufferText);
            const treesitter::Tree& tree    = sharedParse->Update(*parser, bufferText);
            if (tree.IsNull()) {
                return markers;
            }
            for (TestMarker& marker : markers) {
                treesitter::Node node = tree.RootNode().NamedDescendantForByteRange(
                    marker.startByte, marker.endByte > marker.startByte ? marker.endByte - 1 : marker.startByte);
                while (!node.IsNull() && node.StartByte() >= marker.startByte) {
                    const treesitter::Node sibling = node.NextNamedSibling();
                    if (!sibling.IsNull()) {
                        if (sibling.Type() == "compound_statement" && sibling.StartByte() >= marker.endByte &&
                            sibling.StartByte() <= marker.endByte + 2) {
                            marker.endByte = sibling.EndByte();
                        }
                        break;
                    }
                    node = node.Parent();
                }
            }
            return markers;
        };
    }

} // namespace

void RegisterCLikeEscapes() {
    RegisterModeEscape("c.line-inspect", LineInspect);
    RegisterModeEscape("cpp.test-body", TestBody);
}

} // namespace ned::editor::languages
