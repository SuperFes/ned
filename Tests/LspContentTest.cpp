#include <catch2/catch_test_macros.hpp>

#include "Editor/Lsp/LspContent.h"

using ned::editor::lsp::CodeAction;
using ned::editor::lsp::CompletionItem;
using ned::editor::lsp::DefinitionLocation;
using ned::editor::lsp::DocumentHighlight;
using ned::editor::lsp::ExtractCodeActions;
using ned::editor::lsp::ExtractCompletionItems;
using ned::editor::lsp::ExtractDefinitionLocations;
using ned::editor::lsp::ExtractDocumentHighlights;
using ned::editor::lsp::ExtractHoverText;
using ned::editor::lsp::ExtractFormattingEdits;
using ned::editor::lsp::ExtractRenameEdits;
using ned::editor::lsp::ExtractSignatureHelp;
using ned::editor::lsp::ExtractSingleCodeAction;
using ned::editor::lsp::ExtractSymbols;
using ned::editor::lsp::Json;
using ned::editor::lsp::LspPosition;
using ned::editor::lsp::RenameResult;
using ned::editor::lsp::SymbolEntry;
using ned::editor::lsp::SymbolKindLabel;

TEST_CASE("ExtractHoverText handles a bare string contents field", "[Lsp]") {
    const Json result = {{"contents", "hello world"}};
    const auto text   = ExtractHoverText(result);
    REQUIRE(text.has_value());
    REQUIRE(*text == "hello world");
}

TEST_CASE("ExtractHoverText handles a MarkupContent object contents field", "[Lsp]") {
    const Json result = {{"contents", {{"kind", "markdown"}, {"value", "**bold**"}}}};
    const auto text   = ExtractHoverText(result);
    REQUIRE(text.has_value());
    REQUIRE(*text == "**bold**");
}

TEST_CASE("ExtractHoverText joins an array of contents entries with a blank line", "[Lsp]") {
    const Json result = {{"contents", Json::array({"first", {{"value", "second"}}})}};
    const auto text   = ExtractHoverText(result);
    REQUIRE(text.has_value());
    REQUIRE(*text == "first\n\nsecond");
}

TEST_CASE("ExtractHoverText returns nullopt for a null result", "[Lsp]") {
    REQUIRE_FALSE(ExtractHoverText(Json(nullptr)).has_value());
}

TEST_CASE("ExtractHoverText returns nullopt when contents is missing", "[Lsp]") {
    REQUIRE_FALSE(ExtractHoverText(Json::object()).has_value());
}

TEST_CASE("ExtractHoverText returns nullopt for an empty contents string", "[Lsp]") {
    const Json result = {{"contents", ""}};
    REQUIRE_FALSE(ExtractHoverText(result).has_value());
}

TEST_CASE("ExtractCompletionItems handles a bare CompletionItem array", "[Lsp]") {
    const Json                        result = Json::array({
        {{"label", "foo"}, {"insertText", "foo()"}},
        {{"label", "bar"}},
    });
    const std::vector<CompletionItem> items  = ExtractCompletionItems(result);
    REQUIRE(items.size() == 2);
    REQUIRE(items[0].label == "foo");
    REQUIRE(items[0].insertText == "foo()");
    REQUIRE(items[1].label == "bar");
    REQUIRE(items[1].insertText == "bar"); // falls back to label when insertText is absent
}

TEST_CASE("ExtractCompletionItems handles a CompletionList {isIncomplete, items} object", "[Lsp]") {
    const Json result = {
        {"isIncomplete", false},
        {"items", Json::array({{{"label", "baz"}}})},
    };
    const std::vector<CompletionItem> items = ExtractCompletionItems(result);
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].label == "baz");
}

TEST_CASE("ExtractCompletionItems skips an item with no label", "[Lsp]") {
    const Json                        result = Json::array({{{"insertText", "no label here"}}, {{"label", "has-label"}}});
    const std::vector<CompletionItem> items  = ExtractCompletionItems(result);
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].label == "has-label");
}

TEST_CASE("ExtractCompletionItems returns empty for a null result", "[Lsp]") {
    REQUIRE(ExtractCompletionItems(Json(nullptr)).empty());
}

TEST_CASE("ExtractCompletionItems returns empty when a CompletionList has no items field", "[Lsp]") {
    const Json result = {{"isIncomplete", true}};
    REQUIRE(ExtractCompletionItems(result).empty());
}

TEST_CASE("ExtractCompletionItems preserves server order, no re-sorting", "[Lsp]") {
    const Json                        result = Json::array({{{"label", "zzz"}}, {{"label", "aaa"}}, {{"label", "mmm"}}});
    const std::vector<CompletionItem> items  = ExtractCompletionItems(result);
    REQUIRE(items.size() == 3);
    REQUIRE(items[0].label == "zzz");
    REQUIRE(items[1].label == "aaa");
    REQUIRE(items[2].label == "mmm");
}

namespace {

Json MakeRange(std::size_t startLine, std::size_t startChar, std::size_t endLine, std::size_t endChar) {
    return Json{
        {"start", {{"line", startLine}, {"character", startChar}}},
        {"end", {{"line", endLine}, {"character", endChar}}},
    };
}

} // namespace

TEST_CASE("ExtractCodeActions parses a CodeAction with a \"changes\" WorkspaceEdit touching only ownUri", "[Lsp]") {
    const Json result = Json::array({
        {{"title", "Add missing include <cstdio>"},
         {"edit",
          {{"changes",
            {{"file:///a.c", Json::array({{{"range", MakeRange(0, 0, 0, 0)}, {"newText", "#include <cstdio>\n"}}})}}}}}},
    });

    const std::vector<CodeAction> actions = ExtractCodeActions(result, "file:///a.c");
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].title == "Add missing include <cstdio>");
    REQUIRE(actions[0].hasEdit);
    REQUIRE_FALSE(actions[0].touchesOtherFiles);
    REQUIRE(actions[0].edits.size() == 1);
    REQUIRE(actions[0].edits[0].start == LspPosition{.line = 0, .character = 0});
    REQUIRE(actions[0].edits[0].end == LspPosition{.line = 0, .character = 0});
    REQUIRE(actions[0].edits[0].newText == "#include <cstdio>\n");
}

TEST_CASE("ExtractCodeActions refuses wholesale when \"changes\" also touches another URI", "[Lsp]") {
    const Json result = Json::array({
        {{"title", "Rename across files"},
         {"edit",
          {{"changes",
            {
                {"file:///a.c", Json::array({{{"range", MakeRange(0, 0, 0, 1)}, {"newText", "x"}}})},
                {"file:///b.c", Json::array({{{"range", MakeRange(0, 0, 0, 1)}, {"newText", "y"}}})},
            }}}}},
    });

    const std::vector<CodeAction> actions = ExtractCodeActions(result, "file:///a.c");
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].hasEdit);
    REQUIRE(actions[0].touchesOtherFiles);
    REQUIRE(actions[0].edits.empty()); // refused wholesale, not partially applied
}

TEST_CASE("ExtractCodeActions marks a documentChanges-only edit as touching other files, with no edits parsed", "[Lsp]") {
    const Json result = Json::array({
        {{"title", "Rename symbol"},
         {"edit", {{"documentChanges", Json::array()}}}},
    });

    const std::vector<CodeAction> actions = ExtractCodeActions(result, "file:///a.c");
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].hasEdit);
    REQUIRE(actions[0].touchesOtherFiles);
    REQUIRE(actions[0].edits.empty());
}

TEST_CASE("ExtractCodeActions marks a bare Command item (no \"edit\") as hasEdit=false", "[Lsp]") {
    const Json result = Json::array({
        {{"title", "Run a server-side fixit"}, {"command", "myserver.fixit"}, {"arguments", Json::array()}},
    });

    const std::vector<CodeAction> actions = ExtractCodeActions(result, "file:///a.c");
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].title == "Run a server-side fixit");
    REQUIRE_FALSE(actions[0].hasEdit);
    REQUIRE(actions[0].edits.empty());
}

TEST_CASE("ExtractCodeActions preserves server order across multiple actions", "[Lsp]") {
    const Json result = Json::array({
        {{"title", "First fix"}},
        {{"title", "Second fix"}},
        {{"title", "Third fix"}},
    });

    const std::vector<CodeAction> actions = ExtractCodeActions(result, "file:///a.c");
    REQUIRE(actions.size() == 3);
    REQUIRE(actions[0].title == "First fix");
    REQUIRE(actions[1].title == "Second fix");
    REQUIRE(actions[2].title == "Third fix");
}

TEST_CASE("ExtractCodeActions skips an item with no title", "[Lsp]") {
    const Json                    result  = Json::array({{{"command", "no.title"}}, {{"title", "Has a title"}}});
    const std::vector<CodeAction> actions = ExtractCodeActions(result, "file:///a.c");
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].title == "Has a title");
}

TEST_CASE("ExtractCodeActions returns empty for a non-array result", "[Lsp]") {
    REQUIRE(ExtractCodeActions(Json(nullptr), "file:///a.c").empty());
    REQUIRE(ExtractCodeActions(Json::object(), "file:///a.c").empty());
}

TEST_CASE("ExtractSingleCodeAction marks a real CodeAction missing \"edit\" as resolvable", "[Lsp]") {
    const Json       item   = {{"title", "Remove #include directive"}, {"kind", "quickfix"}, {"data", {{"opaque", 42}}}};
    const CodeAction action = ExtractSingleCodeAction(item, "file:///a.c");

    REQUIRE(action.title == "Remove #include directive");
    REQUIRE_FALSE(action.hasEdit);
    REQUIRE(action.resolvable);  // has "kind" -- a real CodeAction, not a bare Command
    REQUIRE(action.raw == item); // preserved verbatim for codeAction/resolve to send back
}

TEST_CASE("ExtractSingleCodeAction parses kind and isPreferred (quick-fix follow-up)", "[Lsp]") {
    const Json       item   = {{"title", "The fix"}, {"kind", "quickfix"}, {"isPreferred", true}};
    const CodeAction action = ExtractSingleCodeAction(item, "file:///a.c");
    REQUIRE(action.kind == "quickfix");
    REQUIRE(action.isPreferred);

    // Both default off for a bare Command, which carries neither field.
    const CodeAction command = ExtractSingleCodeAction(Json{{"title", "Run"}, {"command", "x"}}, "file:///a.c");
    REQUIRE(command.kind.empty());
    REQUIRE_FALSE(command.isPreferred);
}

TEST_CASE("ExtractSingleCodeAction does NOT mark a bare Command (no \"kind\") as resolvable", "[Lsp]") {
    const Json       item   = {{"title", "Run a server-side fixit"}, {"command", "myserver.fixit"}};
    const CodeAction action = ExtractSingleCodeAction(item, "file:///a.c");

    REQUIRE_FALSE(action.hasEdit);
    REQUIRE_FALSE(action.resolvable);
}

TEST_CASE("ExtractSingleCodeAction is not resolvable once it already has an edit", "[Lsp]") {
    const Json textEdit = {{"range", MakeRange(0, 0, 0, 1)}, {"newText", "x"}};
    const Json changes  = {{"file:///a.c", Json::array({textEdit})}};
    const Json edit     = {{"changes", changes}};
    const Json item     = {{"title", "Fix"}, {"kind", "quickfix"}, {"edit", edit}};

    const CodeAction action = ExtractSingleCodeAction(item, "file:///a.c");

    REQUIRE(action.hasEdit);
    REQUIRE_FALSE(action.resolvable); // already has its edit -- nothing to resolve
}

TEST_CASE("ExtractSingleCodeAction parses a bare Command item's command/arguments", "[Lsp]") {
    const Json       item   = {{"title", "Add to dictionary"}, {"command", "HarperAddToUserDict"}, {"arguments", Json::array({"teh"})}};
    const CodeAction action = ExtractSingleCodeAction(item, "file:///a.c");

    REQUIRE(action.command.has_value());
    REQUIRE(action.command->name == "HarperAddToUserDict");
    REQUIRE(action.command->arguments == Json::array({"teh"}));
}

TEST_CASE("ExtractSingleCodeAction parses a real CodeAction's nested command object", "[Lsp]") {
    const Json item = {
        {"title", "Ignore this lint"},
        {"kind", "quickfix"},
        {"command", {{"title", "Ignore this lint"}, {"command", "HarperIgnoreLint"}, {"arguments", Json::array({1, 2})}}},
    };
    const CodeAction action = ExtractSingleCodeAction(item, "file:///a.c");

    REQUIRE(action.command.has_value());
    REQUIRE(action.command->name == "HarperIgnoreLint");
    REQUIRE(action.command->arguments == Json::array({1, 2}));
}

TEST_CASE("ExtractSingleCodeAction defaults a command's missing arguments to an empty array", "[Lsp]") {
    const Json item = {{"title", "Run"}, {"command", "myserver.fixit"}};
    REQUIRE(ExtractSingleCodeAction(item, "file:///a.c").command->arguments == Json::array());
}

TEST_CASE("ExtractSingleCodeAction parses an item carrying both an edit and a command", "[Lsp]") {
    // The roadmap's own "Replace with X" case: a real CodeAction that
    // applies an edit AND separately asks the client to execute a command.
    const Json textEdit = {{"range", MakeRange(0, 0, 0, 3)}, {"newText", "the"}};
    const Json item      = {
        {"title", "Replace with \"the\""},
        {"kind", "quickfix"},
        {"edit", {{"changes", {{"file:///a.c", Json::array({textEdit})}}}}},
        {"command", {{"title", "Record fix"}, {"command", "HarperRecordLint"}}},
    };

    const CodeAction action = ExtractSingleCodeAction(item, "file:///a.c");
    REQUIRE(action.hasEdit);
    REQUIRE(action.edits.size() == 1);
    REQUIRE(action.command.has_value());
    REQUIRE(action.command->name == "HarperRecordLint");
}

TEST_CASE("ExtractSingleCodeAction leaves command unset when the item has none", "[Lsp]") {
    const Json item = {{"title", "No command here"}, {"kind", "quickfix"}};
    REQUIRE_FALSE(ExtractSingleCodeAction(item, "file:///a.c").command.has_value());
}

TEST_CASE("ExtractDefinitionLocations parses a bare Location object", "[Lsp]") {
    const Json                            result    = {{"uri", "file:///a.c"}, {"range", MakeRange(4, 2, 4, 10)}};
    const std::vector<DefinitionLocation> locations = ExtractDefinitionLocations(result);
    REQUIRE(locations.size() == 1);
    REQUIRE(locations[0].uri == "file:///a.c");
    REQUIRE(locations[0].position == LspPosition{.line = 4, .character = 2});
}

TEST_CASE("ExtractDefinitionLocations parses a Location-array response", "[Lsp]") {
    const Json                            result    = Json::array({
        {{"uri", "file:///a.c"}, {"range", MakeRange(1, 0, 1, 1)}},
        {{"uri", "file:///b.c"}, {"range", MakeRange(2, 0, 2, 1)}},
    });
    const std::vector<DefinitionLocation> locations = ExtractDefinitionLocations(result);
    REQUIRE(locations.size() == 2);
    REQUIRE(locations[0].uri == "file:///a.c");
    REQUIRE(locations[1].uri == "file:///b.c");
    REQUIRE(locations[1].position == LspPosition{.line = 2, .character = 0});
}

TEST_CASE("ExtractDefinitionLocations parses a LocationLink-array response via targetUri/targetSelectionRange", "[Lsp]") {
    const Json                            result    = Json::array({
        {{"targetUri", "file:///impl.c"},
         {"targetRange", MakeRange(0, 0, 10, 0)},
         {"targetSelectionRange", MakeRange(3, 5, 3, 12)}},
    });
    const std::vector<DefinitionLocation> locations = ExtractDefinitionLocations(result);
    REQUIRE(locations.size() == 1);
    REQUIRE(locations[0].uri == "file:///impl.c");
    // targetSelectionRange, not the wider targetRange -- the precise identifier location.
    REQUIRE(locations[0].position == LspPosition{.line = 3, .character = 5});
}

TEST_CASE("ExtractDefinitionLocations returns empty for a null result", "[Lsp]") {
    REQUIRE(ExtractDefinitionLocations(Json(nullptr)).empty());
}

TEST_CASE("ExtractDefinitionLocations skips a malformed entry (no uri/targetUri)", "[Lsp]") {
    const Json                            result    = Json::array({{{"range", MakeRange(0, 0, 0, 1)}}, {{"uri", "file:///a.c"}, {"range", MakeRange(0, 0, 0, 1)}}});
    const std::vector<DefinitionLocation> locations = ExtractDefinitionLocations(result);
    REQUIRE(locations.size() == 1);
    REQUIRE(locations[0].uri == "file:///a.c");
}

TEST_CASE("ExtractRenameEdits parses a \"changes\" WorkspaceEdit spanning multiple files", "[Lsp]") {
    const Json result = {
        {"changes",
         {
             {"file:///a.c", Json::array({{{"range", MakeRange(0, 0, 0, 3)}, {"newText", "newName"}}})},
             {"file:///b.c", Json::array({{{"range", MakeRange(2, 4, 2, 7)}, {"newText", "newName"}}})},
         }},
    };
    const RenameResult renamed = ExtractRenameEdits(result);
    REQUIRE(renamed.hasEdit);
    REQUIRE_FALSE(renamed.touchesUnsupportedForm);
    REQUIRE(renamed.edits.size() == 2); // both URIs kept -- unlike code actions, rename isn't scoped to one URI
}

TEST_CASE("ExtractRenameEdits marks a documentChanges-only edit as unsupported, with no edits parsed", "[Lsp]") {
    const Json         result  = {{"documentChanges", Json::array()}};
    const RenameResult renamed = ExtractRenameEdits(result);
    REQUIRE(renamed.touchesUnsupportedForm);
    REQUIRE_FALSE(renamed.hasEdit);
    REQUIRE(renamed.edits.empty());
}

TEST_CASE("ExtractRenameEdits returns no edits for a result with no \"changes\" map", "[Lsp]") {
    const RenameResult renamed = ExtractRenameEdits(Json::object());
    REQUIRE_FALSE(renamed.hasEdit);
    REQUIRE_FALSE(renamed.touchesUnsupportedForm);
    REQUIRE(renamed.edits.empty());
}

TEST_CASE("ExtractRenameEdits returns no edits for a non-object result", "[Lsp]") {
    REQUIRE_FALSE(ExtractRenameEdits(Json(nullptr)).hasEdit);
}

TEST_CASE("ExtractDocumentHighlights parses a well-formed array with an explicit kind", "[Lsp]") {
    const Json result = Json::array({
        {{"range", MakeRange(0, 0, 0, 3)}, {"kind", 2}},
        {{"range", MakeRange(1, 4, 1, 7)}, {"kind", 3}},
    });
    const std::vector<DocumentHighlight> highlights = ExtractDocumentHighlights(result);
    REQUIRE(highlights.size() == 2);
    REQUIRE(highlights[0].start.line == 0);
    REQUIRE(highlights[0].end.character == 3);
    REQUIRE(highlights[0].kind == 2);
    REQUIRE(highlights[1].kind == 3);
}

TEST_CASE("ExtractDocumentHighlights defaults a missing kind to 1 (Text)", "[Lsp]") {
    const Json result = Json::array({{{"range", MakeRange(0, 0, 0, 3)}}});
    const std::vector<DocumentHighlight> highlights = ExtractDocumentHighlights(result);
    REQUIRE(highlights.size() == 1);
    REQUIRE(highlights[0].kind == 1);
}

TEST_CASE("ExtractDocumentHighlights skips an entry missing \"range\"", "[Lsp]") {
    const Json result = Json::array({{{"kind", 1}}, {{"range", MakeRange(0, 0, 0, 1)}}});
    REQUIRE(ExtractDocumentHighlights(result).size() == 1);
}

TEST_CASE("ExtractDocumentHighlights returns empty for a null result", "[Lsp]") {
    REQUIRE(ExtractDocumentHighlights(Json(nullptr)).empty());
}

TEST_CASE("ExtractDocumentHighlights returns empty for a non-array result", "[Lsp]") {
    REQUIRE(ExtractDocumentHighlights(Json::object()).empty());
}

TEST_CASE("ExtractFormattingEdits parses a bare TextEdit[] response", "[Lsp]") {
    const Json result = Json::array({
        {{"range", MakeRange(0, 0, 0, 3)}, {"newText", "int"}},
        {{"range", MakeRange(1, 0, 1, 4)}, {"newText", "    "}},
    });
    const std::vector<ned::editor::lsp::WorkspaceTextEdit> edits = ExtractFormattingEdits(result);
    REQUIRE(edits.size() == 2);
    REQUIRE(edits[0].newText == "int");
    REQUIRE(edits[1].start.line == 1);
    REQUIRE(edits[1].newText == "    ");
}

TEST_CASE("ExtractFormattingEdits skips an entry missing \"range\"", "[Lsp]") {
    const Json result = Json::array({{{"newText", "x"}}, {{"range", MakeRange(0, 0, 0, 1)}, {"newText", "y"}}});
    REQUIRE(ExtractFormattingEdits(result).size() == 1);
}

TEST_CASE("ExtractFormattingEdits returns empty for a null result", "[Lsp]") {
    REQUIRE(ExtractFormattingEdits(Json(nullptr)).empty());
}

TEST_CASE("ExtractFormattingEdits returns empty for a non-array result", "[Lsp]") {
    REQUIRE(ExtractFormattingEdits(Json::object()).empty());
}

TEST_CASE("ExtractCompletionItems reads insertTextFormat's snippet flag", "[Lsp]") {
    const auto                        result = ned::editor::lsp::Json::array({
        {{"label", "plain"}, {"insertText", "plain"}, {"insertTextFormat", 1}},
        {{"label", "snip"}, {"insertText", "snip(${1:x})"}, {"insertTextFormat", 2}},
        {{"label", "unspecified"}, {"insertText", "unspecified"}},
    });
    const std::vector<CompletionItem> items  = ExtractCompletionItems(result);
    REQUIRE(items.size() == 3);
    REQUIRE_FALSE(items[0].isSnippet);
    REQUIRE(items[1].isSnippet);
    REQUIRE(items[1].insertText == "snip(${1:x})");
    REQUIRE_FALSE(items[2].isSnippet); // absent defaults to PlainText per the spec
}

TEST_CASE("ExtractSignatureHelp wraps the active parameter's string label in guillemets", "[Lsp]") {
    const Json result = {
        {"signatures", Json::array({{{"label", "foo(a: int, b: string)"},
                                     {"parameters", Json::array({{{"label", "a: int"}}, {{"label", "b: string"}}})}}})},
        {"activeParameter", 1},
    };
    const auto text = ExtractSignatureHelp(result);
    REQUIRE(text.has_value());
    REQUIRE(*text == "foo(a: int, **b: string**)");
}

TEST_CASE("ExtractSignatureHelp resolves a [start, end) offset-pair parameter label", "[Lsp]") {
    const Json result = {
        {"signatures", Json::array({{{"label", "foo(a, b)"}, {"parameters", Json::array({{{"label", Json::array({4, 5})}}, {{"label", Json::array({7, 8})}}})}}})},
        {"activeParameter", 1},
    };
    const auto text = ExtractSignatureHelp(result);
    REQUIRE(text.has_value());
    REQUIRE(*text == "foo(a, **b**)");
}

TEST_CASE("ExtractSignatureHelp falls back to the signature's own activeParameter over the top-level one", "[Lsp]") {
    const Json result = {
        {"signatures", Json::array({{{"label", "foo(a, b)"},
                                     {"activeParameter", 0},
                                     {"parameters", Json::array({{{"label", "a"}}, {{"label", "b"}}})}}})},
        {"activeParameter", 1}, // should be shadowed by the signature's own value above
    };
    const auto text = ExtractSignatureHelp(result);
    REQUIRE(text.has_value());
    REQUIRE(*text == "foo(**a**, b)");
}

TEST_CASE("ExtractSignatureHelp respects activeSignature when there is more than one overload", "[Lsp]") {
    const Json result = {
        {"signatures", Json::array({{{"label", "foo()"}}, {{"label", "foo(a)"}, {"parameters", Json::array({{{"label", "a"}}})}}})},
        {"activeSignature", 1},
        {"activeParameter", 0},
    };
    const auto text = ExtractSignatureHelp(result);
    REQUIRE(text.has_value());
    REQUIRE(*text == "foo(**a**)");
}

TEST_CASE("ExtractSignatureHelp returns the plain label when no active parameter resolves", "[Lsp]") {
    const Json result = {{"signatures", Json::array({{{"label", "foo(a, b)"}}})}};
    const auto text   = ExtractSignatureHelp(result);
    REQUIRE(text.has_value());
    REQUIRE(*text == "foo(a, b)");
}

TEST_CASE("ExtractSignatureHelp returns nullopt for an empty signatures array", "[Lsp]") {
    REQUIRE_FALSE(ExtractSignatureHelp(Json{{"signatures", Json::array()}}).has_value());
}

TEST_CASE("ExtractSignatureHelp returns nullopt for a null result", "[Lsp]") {
    REQUIRE_FALSE(ExtractSignatureHelp(Json(nullptr)).has_value());
}

TEST_CASE("SymbolKindLabel maps every defined LSP SymbolKind and falls back to \"symbol\"", "[Lsp]") {
    REQUIRE(SymbolKindLabel(5) == "class");
    REQUIRE(SymbolKindLabel(6) == "method");
    REQUIRE(SymbolKindLabel(12) == "function");
    REQUIRE(SymbolKindLabel(13) == "variable");
    REQUIRE(SymbolKindLabel(23) == "struct");
    REQUIRE(SymbolKindLabel(0) == "symbol");
    REQUIRE(SymbolKindLabel(999) == "symbol");
}

TEST_CASE("ExtractSymbols flattens a hierarchical DocumentSymbol[] response with the parent name as containerName",
          "[Lsp]") {
    const Json result = Json::array({
        {{"name", "Widget"},
         {"kind", 5},
         {"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 10}, {"character", 1}}}}},
         {"selectionRange", {{"start", {{"line", 0}, {"character", 6}}}, {"end", {{"line", 0}, {"character", 12}}}}},
         {"children", Json::array({{{"name", "Render"},
                                    {"kind", 6},
                                    {"range", {{"start", {{"line", 2}, {"character", 4}}}, {"end", {{"line", 4}, {"character", 5}}}}},
                                    {"selectionRange",
                                     {{"start", {{"line", 2}, {"character", 9}}}, {"end", {{"line", 2}, {"character", 15}}}}}}})}},
    });

    const std::vector<SymbolEntry> entries = ExtractSymbols(result, "file:///widget.cpp");
    REQUIRE(entries.size() == 2);
    REQUIRE(entries[0].name == "Widget");
    REQUIRE(entries[0].containerName.empty());
    REQUIRE(entries[0].kind == 5);
    REQUIRE(entries[0].uri == "file:///widget.cpp");
    REQUIRE(entries[0].position.line == 0);
    REQUIRE(entries[0].position.character == 6); // selectionRange.start, not range.start

    REQUIRE(entries[1].name == "Render");
    REQUIRE(entries[1].containerName == "Widget");
    REQUIRE(entries[1].kind == 6);
    REQUIRE(entries[1].uri == "file:///widget.cpp"); // ownUri applied uniformly -- a DocumentSymbol carries no uri of its own
    REQUIRE(entries[1].position.character == 9);
}

TEST_CASE("ExtractSymbols parses a flat SymbolInformation[] response using each entry's own location.uri", "[Lsp]") {
    const Json result = Json::array({
        {{"name", "globalCount"},
         {"kind", 13},
         {"containerName", "ns"},
         {"location",
          {{"uri", "file:///a.cpp"}, {"range", {{"start", {{"line", 3}, {"character", 4}}}, {"end", {{"line", 3}, {"character", 15}}}}}}}},
    });

    const std::vector<SymbolEntry> entries = ExtractSymbols(result, "file:///should-be-ignored.cpp");
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].name == "globalCount");
    REQUIRE(entries[0].containerName == "ns");
    REQUIRE(entries[0].uri == "file:///a.cpp"); // ownUri is not used -- SymbolInformation carries its own
    REQUIRE(entries[0].position.line == 3);
}

TEST_CASE("ExtractSymbols treats a WorkspaceSymbol with a range-less location as position {0, 0}", "[Lsp]") {
    const Json result = Json::array({
        {{"name", "unresolvedSymbol"}, {"kind", 12}, {"location", {{"uri", "file:///b.cpp"}}}},
    });

    const std::vector<SymbolEntry> entries = ExtractSymbols(result);
    REQUIRE(entries.size() == 1);
    REQUIRE(entries[0].uri == "file:///b.cpp");
    REQUIRE(entries[0].position.line == 0);
    REQUIRE(entries[0].position.character == 0);
}

TEST_CASE("ExtractSymbols skips a malformed entry and returns empty for a non-array result", "[Lsp]") {
    const Json result = Json::array({{{"kind", 5}}}); // missing "name"
    REQUIRE(ExtractSymbols(result).empty());
    REQUIRE(ExtractSymbols(Json(nullptr)).empty());
}
