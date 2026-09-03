#include <catch2/catch_test_macros.hpp>

#include "Editor/Lsp/LspContent.h"

using ned::editor::lsp::CodeAction;
using ned::editor::lsp::CodeLens;
using ned::editor::lsp::CompletionItem;
using ned::editor::lsp::DefinitionLocation;
using ned::editor::lsp::DocumentChangeOp;
using ned::editor::lsp::DocumentHighlight;
using ned::editor::lsp::ExtractCodeActions;
using ned::editor::lsp::ExtractCodeLenses;
using ned::editor::lsp::ExtractCompletionItems;
using ned::editor::lsp::ExtractDefinitionLocations;
using ned::editor::lsp::ExtractDocumentHighlights;
using ned::editor::lsp::ExtractFormattingEdits;
using ned::editor::lsp::ExtractHierarchyItems;
using ned::editor::lsp::ExtractHoverText;
using ned::editor::lsp::ExtractIncomingCalls;
using ned::editor::lsp::ExtractInlayHints;
using ned::editor::lsp::ExtractOnTypeFormattingTriggers;
using ned::editor::lsp::ExtractOutgoingCalls;
using ned::editor::lsp::ExtractPullDiagnosticReport;
using ned::editor::lsp::ExtractRenameEdits;
using ned::editor::lsp::ExtractSemanticTokens;
using ned::editor::lsp::ExtractSemanticTokensLegend;
using ned::editor::lsp::ExtractSignatureHelp;
using ned::editor::lsp::ExtractSingleCodeAction;
using ned::editor::lsp::ExtractSingleCodeLens;
using ned::editor::lsp::ExtractSymbols;
using ned::editor::lsp::ExtractTextDocumentSyncKind;
using ned::editor::lsp::HierarchyCall;
using ned::editor::lsp::HierarchyItem;
using ned::editor::lsp::Json;
using ned::editor::lsp::LspPosition;
using ned::editor::lsp::OnTypeFormattingTriggers;
using ned::editor::lsp::RenameResult;
using ned::editor::lsp::SemanticTokensLegend;
using ned::editor::lsp::SymbolEntry;
using ned::editor::lsp::SymbolKindLabel;
using ned::editor::lsp::TextDocumentSyncKind;

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
    REQUIRE_FALSE(actions[0].touchesUnsupportedForm);
    REQUIRE(actions[0].edits.size() == 1);
    REQUIRE(actions[0].edits[0].uri == "file:///a.c");
    REQUIRE(actions[0].edits[0].edits.size() == 1);
    REQUIRE(actions[0].edits[0].edits[0].start == LspPosition{.line = 0, .character = 0});
    REQUIRE(actions[0].edits[0].edits[0].end == LspPosition{.line = 0, .character = 0});
    REQUIRE(actions[0].edits[0].edits[0].newText == "#include <cstdio>\n");
}

TEST_CASE("ExtractCodeActions parses a \"changes\" WorkspaceEdit touching several files, one entry per URI",
          "[Lsp]") {
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
    REQUIRE_FALSE(actions[0].touchesUnsupportedForm);
    REQUIRE(actions[0].edits.size() == 2);
}

TEST_CASE("ExtractCodeActions parses a documentChanges edit into documentChangeOps, in order", "[Lsp]") {
    const Json result = Json::array({
        {{"title", "Extract to new file"},
         {"edit",
          {{"documentChanges",
            Json::array({
                {{"kind", "create"}, {"uri", "file:///new.c"}, {"options", {{"overwrite", true}}}},
                {{"textDocument", {{"uri", "file:///new.c"}, {"version", nullptr}}},
                 {"edits", Json::array({{{"range", MakeRange(0, 0, 0, 0)}, {"newText", "int x;\n"}}})}},
                {{"kind", "rename"}, {"oldUri", "file:///old.c"}, {"newUri", "file:///renamed.c"}},
                {{"kind", "delete"}, {"uri", "file:///gone.c"}, {"options", {{"ignoreIfNotExists", true}}}},
            })}}}},
    });

    const std::vector<CodeAction> actions = ExtractCodeActions(result, "file:///a.c");
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].hasEdit);
    REQUIRE_FALSE(actions[0].touchesUnsupportedForm);
    REQUIRE(actions[0].edits.empty()); // "changes" form stays empty -- documentChangeOps carries everything

    const std::vector<DocumentChangeOp>& ops = actions[0].documentChangeOps;
    REQUIRE(ops.size() == 4);

    REQUIRE(ops[0].kind == DocumentChangeOp::Kind::CreateFile);
    REQUIRE(ops[0].uri == "file:///new.c");
    REQUIRE(ops[0].overwrite);

    REQUIRE(ops[1].kind == DocumentChangeOp::Kind::EditFile);
    REQUIRE(ops[1].uri == "file:///new.c");
    REQUIRE(ops[1].edits.size() == 1);
    REQUIRE(ops[1].edits[0].newText == "int x;\n");

    REQUIRE(ops[2].kind == DocumentChangeOp::Kind::RenameFile);
    REQUIRE(ops[2].oldUri == "file:///old.c");
    REQUIRE(ops[2].uri == "file:///renamed.c");

    REQUIRE(ops[3].kind == DocumentChangeOp::Kind::DeleteFile);
    REQUIRE(ops[3].uri == "file:///gone.c");
    REQUIRE(ops[3].ignoreIfNotExists);
}

TEST_CASE("ExtractCodeActions marks a malformed documentChanges entry as unsupported, refused wholesale", "[Lsp]") {
    const Json result = Json::array({
        {{"title", "Rename symbol"}, {"edit", {{"documentChanges", Json::array({{{"kind", "not-a-real-kind"}}})}}}},
    });

    const std::vector<CodeAction> actions = ExtractCodeActions(result, "file:///a.c");
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].hasEdit);
    REQUIRE(actions[0].touchesUnsupportedForm);
    REQUIRE(actions[0].edits.empty());
    REQUIRE(actions[0].documentChangeOps.empty());
}

TEST_CASE("ExtractCodeActions treats an empty documentChanges array as a well-formed, no-op edit", "[Lsp]") {
    const Json result = Json::array({
        {{"title", "No-op"}, {"edit", {{"documentChanges", Json::array()}}}},
    });

    const std::vector<CodeAction> actions = ExtractCodeActions(result, "file:///a.c");
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].hasEdit);
    REQUIRE_FALSE(actions[0].touchesUnsupportedForm);
    REQUIRE(actions[0].edits.empty());
    REQUIRE(actions[0].documentChangeOps.empty());
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

TEST_CASE("ExtractRenameEdits parses a documentChanges edit -- a rename that also moves the file", "[Lsp]") {
    // The real-world shape this scope cut was closed for: a rename that
    // needs the file itself renamed to match (a class whose containing file
    // convention expects the file to follow), not just its content edited.
    const Json result = {
        {"documentChanges",
         Json::array({
             {{"kind", "rename"}, {"oldUri", "file:///Old.java"}, {"newUri", "file:///Renamed.java"}},
             {{"textDocument", {{"uri", "file:///Renamed.java"}}},
              {"edits", Json::array({{{"range", MakeRange(0, 13, 0, 16)}, {"newText", "Renamed"}}})}},
         })},
    };
    const RenameResult renamed = ExtractRenameEdits(result);
    REQUIRE(renamed.hasEdit);
    REQUIRE_FALSE(renamed.touchesUnsupportedForm);
    REQUIRE(renamed.edits.empty());
    REQUIRE(renamed.documentChangeOps.size() == 2);
    REQUIRE(renamed.documentChangeOps[0].kind == DocumentChangeOp::Kind::RenameFile);
    REQUIRE(renamed.documentChangeOps[0].oldUri == "file:///Old.java");
    REQUIRE(renamed.documentChangeOps[0].uri == "file:///Renamed.java");
    REQUIRE(renamed.documentChangeOps[1].kind == DocumentChangeOp::Kind::EditFile);
    REQUIRE(renamed.documentChangeOps[1].edits.size() == 1);
}

TEST_CASE("ExtractRenameEdits marks a malformed documentChanges entry as unsupported, refused wholesale", "[Lsp]") {
    const Json         result  = {{"documentChanges", Json::array({Json::object()})}}; // neither a TextDocumentEdit nor a ResourceOperation
    const RenameResult renamed = ExtractRenameEdits(result);
    REQUIRE(renamed.touchesUnsupportedForm);
    REQUIRE_FALSE(renamed.hasEdit);
    REQUIRE(renamed.edits.empty());
    REQUIRE(renamed.documentChangeOps.empty());
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

TEST_CASE("ExtractCompletionItems reads kind and detail when present", "[Lsp]") {
    // completion-popup follow-up.
    const auto                        result = ned::editor::lsp::Json::array({
        {{"label", "foo"}, {"insertText", "foo"}, {"kind", 3}, {"detail", "() -> int"}},
        {{"label", "bar"}, {"insertText", "bar"}},
    });
    const std::vector<CompletionItem> items  = ExtractCompletionItems(result);
    REQUIRE(items.size() == 2);
    REQUIRE(items[0].kind == 3);
    REQUIRE(items[0].detail == "() -> int");
    REQUIRE(items[1].kind == 0); // absent -> unset
    REQUIRE(items[1].detail.empty());
}

TEST_CASE("ExtractCompletionItems reads documentation, both string and MarkupContent form", "[Lsp]") {
    // completion-popup-preview follow-up.
    const auto                        result = ned::editor::lsp::Json::array({
        {{"label", "foo"}, {"insertText", "foo"}, {"documentation", "plain string doc"}},
        {{"label", "bar"}, {"insertText", "bar"}, {"documentation", {{"kind", "markdown"}, {"value", "**bold** doc"}}}},
        {{"label", "baz"}, {"insertText", "baz"}},
    });
    const std::vector<CompletionItem> items  = ExtractCompletionItems(result);
    REQUIRE(items.size() == 3);
    REQUIRE(items[0].documentation == "plain string doc");
    REQUIRE(items[1].documentation == "**bold** doc"); // MarkupContent -- value extracted, kind ignored
    REQUIRE(items[2].documentation.empty());           // absent -> unset
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

TEST_CASE("ExtractSignatureHelp resolves a half-open start/end offset-pair parameter label", "[Lsp]") {
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

TEST_CASE("ExtractSemanticTokensLegend parses tokenTypes and tokenModifiers from a full initialize response", "[Lsp]") {
    const Json result = {
        {"capabilities",
         {{"semanticTokensProvider",
           {{"legend", {{"tokenTypes", Json::array({"class", "function", "variable"})}, {"tokenModifiers", Json::array({"declaration", "readonly"})}}},
            {"full", true}}}}},
    };
    const auto legend = ExtractSemanticTokensLegend(result);
    REQUIRE(legend.has_value());
    REQUIRE(legend->tokenTypes == std::vector<std::string>{"class", "function", "variable"});
    REQUIRE(legend->tokenModifiers == std::vector<std::string>{"declaration", "readonly"});
}

TEST_CASE("ExtractSemanticTokensLegend defaults tokenModifiers to empty when the server omits it", "[Lsp]") {
    const Json result = {
        {"capabilities", {{"semanticTokensProvider", {{"legend", {{"tokenTypes", Json::array({"comment"})}}}}}}},
    };
    const auto legend = ExtractSemanticTokensLegend(result);
    REQUIRE(legend.has_value());
    REQUIRE(legend->tokenTypes == std::vector<std::string>{"comment"});
    REQUIRE(legend->tokenModifiers.empty());
}

TEST_CASE("ExtractSemanticTokensLegend returns nullopt when the provider is absent, malformed, or the result isn't an object", "[Lsp]") {
    REQUIRE_FALSE(ExtractSemanticTokensLegend(Json{{"capabilities", Json::object()}}).has_value());
    REQUIRE_FALSE(ExtractSemanticTokensLegend(Json{{"capabilities", {{"semanticTokensProvider", true}}}}).has_value());
    REQUIRE_FALSE(ExtractSemanticTokensLegend(Json{{"capabilities", {{"semanticTokensProvider", Json::object()}}}}).has_value());
    REQUIRE_FALSE(ExtractSemanticTokensLegend(Json(nullptr)).has_value());
}

TEST_CASE("ExtractOnTypeFormattingTriggers parses firstTriggerCharacter and moreTriggerCharacter", "[Lsp]") {
    const Json result = {
        {"capabilities",
         {{"documentOnTypeFormattingProvider", {{"firstTriggerCharacter", "}"}, {"moreTriggerCharacter", Json::array({";", "\n"})}}}}},
    };
    const auto triggers = ExtractOnTypeFormattingTriggers(result);
    REQUIRE(triggers.has_value());
    REQUIRE(triggers->first == "}");
    REQUIRE(triggers->more == std::vector<std::string>{";", "\n"});
}

TEST_CASE("ExtractOnTypeFormattingTriggers defaults moreTriggerCharacter to empty when omitted", "[Lsp]") {
    const Json result = {
        {"capabilities", {{"documentOnTypeFormattingProvider", {{"firstTriggerCharacter", ";"}}}}},
    };
    const auto triggers = ExtractOnTypeFormattingTriggers(result);
    REQUIRE(triggers.has_value());
    REQUIRE(triggers->first == ";");
    REQUIRE(triggers->more.empty());
}

TEST_CASE("ExtractOnTypeFormattingTriggers returns nullopt when the provider is absent or missing firstTriggerCharacter", "[Lsp]") {
    REQUIRE_FALSE(ExtractOnTypeFormattingTriggers(Json{{"capabilities", Json::object()}}).has_value());
    REQUIRE_FALSE(
        ExtractOnTypeFormattingTriggers(Json{{"capabilities", {{"documentOnTypeFormattingProvider", Json::object()}}}}).has_value());
    REQUIRE_FALSE(ExtractOnTypeFormattingTriggers(Json(nullptr)).has_value());
}

TEST_CASE("ExtractTextDocumentSyncKind parses the legacy bare-integer form", "[Lsp]") {
    const auto kind = ExtractTextDocumentSyncKind(Json{{"capabilities", {{"textDocumentSync", 2}}}});
    REQUIRE(kind.has_value());
    REQUIRE(*kind == TextDocumentSyncKind::Incremental);
}

TEST_CASE("ExtractTextDocumentSyncKind parses the object form's change field", "[Lsp]") {
    const Json result = {
        {"capabilities", {{"textDocumentSync", {{"change", 1}, {"openClose", true}}}}},
    };
    const auto kind = ExtractTextDocumentSyncKind(result);
    REQUIRE(kind.has_value());
    REQUIRE(*kind == TextDocumentSyncKind::Full);
}

TEST_CASE("ExtractTextDocumentSyncKind returns None for change: 0", "[Lsp]") {
    const auto kind = ExtractTextDocumentSyncKind(Json{{"capabilities", {{"textDocumentSync", 0}}}});
    REQUIRE(kind.has_value());
    REQUIRE(*kind == TextDocumentSyncKind::None);
}

TEST_CASE("ExtractTextDocumentSyncKind returns nullopt when textDocumentSync is absent", "[Lsp]") {
    REQUIRE_FALSE(ExtractTextDocumentSyncKind(Json{{"capabilities", Json::object()}}).has_value());
}

TEST_CASE("ExtractTextDocumentSyncKind returns nullopt for an out-of-range change value", "[Lsp]") {
    REQUIRE_FALSE(ExtractTextDocumentSyncKind(Json{{"capabilities", {{"textDocumentSync", 7}}}}).has_value());
    REQUIRE_FALSE(
        ExtractTextDocumentSyncKind(Json{{"capabilities", {{"textDocumentSync", {{"change", -1}}}}}}).has_value());
}

TEST_CASE("ExtractTextDocumentSyncKind returns nullopt for a non-object, non-integer textDocumentSync", "[Lsp]") {
    REQUIRE_FALSE(ExtractTextDocumentSyncKind(Json{{"capabilities", {{"textDocumentSync", "full"}}}}).has_value());
    REQUIRE_FALSE(ExtractTextDocumentSyncKind(Json{{"capabilities", {{"textDocumentSync", Json::array({1, 2})}}}}).has_value());
}

TEST_CASE("ExtractTextDocumentSyncKind returns nullopt for a null initialize result", "[Lsp]") {
    REQUIRE_FALSE(ExtractTextDocumentSyncKind(Json(nullptr)).has_value());
}

TEST_CASE("ExtractPullDiagnosticReport parses a full report's items", "[Lsp]") {
    const Json result = {
        {"kind", "full"},
        {"items", Json::array({{{"range", MakeRange(0, 0, 0, 3)}, {"severity", 1}, {"message", "syntax error"}}})},
    };
    const auto items = ExtractPullDiagnosticReport(result);
    REQUIRE(items.has_value());
    REQUIRE(items->size() == 1);
    REQUIRE((*items)[0].severity == 1);
    REQUIRE((*items)[0].message == "syntax error");
    REQUIRE((*items)[0].end.character == 3);
}

TEST_CASE("ExtractPullDiagnosticReport treats a missing \"kind\" the same as \"full\"", "[Lsp]") {
    const Json result = {{"items", Json::array({{{"range", MakeRange(0, 0, 0, 1)}}})}};
    const auto items  = ExtractPullDiagnosticReport(result);
    REQUIRE(items.has_value());
    REQUIRE(items->size() == 1);
}

TEST_CASE("ExtractPullDiagnosticReport returns nullopt for an \"unchanged\" report", "[Lsp]") {
    const Json result = {{"kind", "unchanged"}, {"resultId", "abc123"}};
    REQUIRE_FALSE(ExtractPullDiagnosticReport(result).has_value());
}

TEST_CASE("ExtractPullDiagnosticReport skips an entry missing \"range\" and returns nullopt for a non-object result",
          "[Lsp]") {
    const Json result = {{"kind", "full"}, {"items", Json::array({{{"message", "no range"}}, {{"range", MakeRange(0, 0, 0, 1)}}})}};
    const auto items  = ExtractPullDiagnosticReport(result);
    REQUIRE(items.has_value());
    REQUIRE(items->size() == 1);

    REQUIRE_FALSE(ExtractPullDiagnosticReport(Json(nullptr)).has_value());
    REQUIRE_FALSE(ExtractPullDiagnosticReport(Json{{"kind", "something-unrecognized"}}).has_value());
}

TEST_CASE("ExtractSemanticTokens decodes deltaLine/deltaStartChar per the spec's relative encoding", "[Lsp]") {
    // Three tokens: (line 0, char 0, len 3, type 5, mods 0) -- "int"
    //               (line 0, char 4, len 1, type 8, mods 1) -- "x", same line as the first (deltaStartChar relative)
    //               (line 1, char 2, len 6, type 5, mods 0) -- "return" on the next line (deltaStartChar absolute)
    const Json result = {{"data", Json::array({0, 0, 3, 5, 0, 0, 4, 1, 8, 1, 1, 2, 6, 5, 0})}};
    const auto tokens  = ExtractSemanticTokens(result);
    REQUIRE(tokens.size() == 3);
    REQUIRE(tokens[0].start.line == 0);
    REQUIRE(tokens[0].start.character == 0);
    REQUIRE(tokens[0].length == 3);
    REQUIRE(tokens[0].tokenTypeIndex == 5);
    REQUIRE(tokens[0].tokenModifiers == 0);

    REQUIRE(tokens[1].start.line == 0);
    REQUIRE(tokens[1].start.character == 4); // 0 + 4, same line as token 0
    REQUIRE(tokens[1].tokenModifiers == 1);

    REQUIRE(tokens[2].start.line == 1);
    REQUIRE(tokens[2].start.character == 2); // absolute, not 4 + 2 -- deltaLine != 0
    REQUIRE(tokens[2].tokenTypeIndex == 5);
}

TEST_CASE("ExtractSemanticTokens returns empty for a missing/non-array/malformed-length \"data\"", "[Lsp]") {
    REQUIRE(ExtractSemanticTokens(Json::object()).empty());
    REQUIRE(ExtractSemanticTokens(Json{{"data", "not an array"}}).empty());
    REQUIRE(ExtractSemanticTokens(Json{{"data", Json::array({0, 0, 3, 5})}}).empty()); // 4 entries, not a multiple of 5
    REQUIRE(ExtractSemanticTokens(Json(nullptr)).empty());
}

TEST_CASE("ExtractInlayHints parses a bare-string label", "[Lsp]") {
    const Json result = Json::array({{{"position", {{"line", 2}, {"character", 5}}}, {"label", ": int"}}});
    const auto hints   = ExtractInlayHints(result);
    REQUIRE(hints.size() == 1);
    REQUIRE(hints[0].position.line == 2);
    REQUIRE(hints[0].position.character == 5);
    REQUIRE(hints[0].label == ": int");
}

TEST_CASE("ExtractInlayHints concatenates an InlayHintLabelPart[] label's own \"value\" fields", "[Lsp]") {
    const Json result = Json::array(
        {{{"position", {{"line", 0}, {"character", 0}}}, {"label", Json::array({{{"value", "x"}}, {{"value", ": "}}, {{"value", "int"}}})}}});
    const auto hints = ExtractInlayHints(result);
    REQUIRE(hints.size() == 1);
    REQUIRE(hints[0].label == "x: int");
}

TEST_CASE("ExtractInlayHints skips an entry missing position/label, an empty-string label, and returns empty for a "
          "non-array result",
          "[Lsp]") {
    const Json result = Json::array({
        {{"label", "no position"}},
        {{"position", {{"line", 0}, {"character", 0}}}}, // no label
        {{"position", {{"line", 0}, {"character", 0}}}, {"label", ""}},
        {{"position", {{"line", 1}, {"character", 1}}}, {"label", "kept"}},
    });
    const auto hints = ExtractInlayHints(result);
    REQUIRE(hints.size() == 1);
    REQUIRE(hints[0].label == "kept");

    REQUIRE(ExtractInlayHints(Json(nullptr)).empty());
    REQUIRE(ExtractInlayHints(Json::object()).empty());
}

TEST_CASE("ExtractSingleCodeLens parses range and a resolved command", "[Lsp]") {
    const Json item = {
        {"range", MakeRange(2, 0, 2, 10)},
        {"command", {{"title", "3 references"}, {"command", "editor.action.showReferences"}, {"arguments", Json::array({1, 2})}}},
    };
    const CodeLens lens = ExtractSingleCodeLens(item);
    REQUIRE(lens.start.line == 2);
    REQUIRE(lens.end.character == 10);
    REQUIRE(lens.title == "3 references");
    REQUIRE(lens.commandName == "editor.action.showReferences");
    REQUIRE(lens.hasCommand);
    REQUIRE(lens.commandArguments == Json::array({1, 2}));
}

TEST_CASE("ExtractSingleCodeLens leaves hasCommand false for an unresolved lens (no \"command\" at all)", "[Lsp]") {
    const Json item = {{"range", MakeRange(0, 0, 0, 5)}};
    const CodeLens lens = ExtractSingleCodeLens(item);
    REQUIRE_FALSE(lens.hasCommand);
    REQUIRE(lens.title.empty());
    REQUIRE(lens.raw == item); // round-trips verbatim for a later resolve
}

TEST_CASE("ExtractCodeLenses skips an entry missing \"range\" and returns empty for a non-array result", "[Lsp]") {
    const Json result = Json::array({
        {{"command", {{"title", "no range"}, {"command", "x"}}}},
        {{"range", MakeRange(0, 0, 0, 1)}, {"command", {{"title", "kept"}, {"command", "x"}}}},
    });
    const auto lenses = ExtractCodeLenses(result);
    REQUIRE(lenses.size() == 1);
    REQUIRE(lenses[0].title == "kept");

    REQUIRE(ExtractCodeLenses(Json(nullptr)).empty());
    REQUIRE(ExtractCodeLenses(Json::object()).empty());
}

namespace {

Json MakeHierarchyItem(const std::string& name, const std::string& uri, std::optional<Json> data = std::nullopt) {
    Json item = {
        {"name", name},
        {"kind", 12},
        {"detail", "int(int)"},
        {"uri", uri},
        {"range", MakeRange(4, 0, 8, 1)},
        {"selectionRange", MakeRange(4, 4, 4, 10)},
    };
    if (data) {
        item["data"] = *data;
    }
    return item;
}

} // namespace

TEST_CASE("ExtractHierarchyItems parses name/detail/kind/uri/position and round-trips the whole item verbatim as "
          "\"raw\"",
          "[Lsp]") {
    const Json itemJson = MakeHierarchyItem("caller", "file:///a.cpp", Json{{"token", 7}});
    const Json result   = Json::array({itemJson});
    const auto items    = ExtractHierarchyItems(result);
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].name == "caller");
    REQUIRE(items[0].detail == "int(int)");
    REQUIRE(items[0].kind == 12);
    REQUIRE(items[0].uri == "file:///a.cpp");
    REQUIRE(items[0].position.line == 4);
    REQUIRE(items[0].position.character == 4);
    REQUIRE(items[0].raw == itemJson); // round-trips verbatim -- including "data" -- for the next request's "item"
}

TEST_CASE("ExtractHierarchyItems' \"raw\" has no \"data\" key when the server omitted one", "[Lsp]") {
    const Json result = Json::array({MakeHierarchyItem("caller", "file:///a.cpp")});
    const auto items  = ExtractHierarchyItems(result);
    REQUIRE(items.size() == 1);
    REQUIRE_FALSE(items[0].raw.contains("data"));
}

TEST_CASE("ExtractHierarchyItems skips an entry missing name/uri/selectionRange and returns empty for a non-array "
          "result",
          "[Lsp]") {
    const Json result = Json::array({
        Json{{"uri", "file:///a.cpp"}, {"selectionRange", MakeRange(0, 0, 0, 1)}},        // missing name
        Json{{"name", "x"}, {"selectionRange", MakeRange(0, 0, 0, 1)}},                    // missing uri
        Json{{"name", "x"}, {"uri", "file:///a.cpp"}},                                     // missing selectionRange
        MakeHierarchyItem("kept", "file:///b.cpp"),
    });
    const auto items = ExtractHierarchyItems(result);
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].name == "kept");

    REQUIRE(ExtractHierarchyItems(Json(nullptr)).empty());
    REQUIRE(ExtractHierarchyItems(Json::object()).empty());
}

TEST_CASE("ExtractIncomingCalls parses \"from\"/\"fromRanges\" and keeps every call site", "[Lsp]") {
    const Json result = Json::array({Json{
        {"from", MakeHierarchyItem("caller", "file:///a.cpp")},
        {"fromRanges", Json::array({MakeRange(5, 2, 5, 8), MakeRange(9, 2, 9, 8)})},
    }});
    const auto calls = ExtractIncomingCalls(result);
    REQUIRE(calls.size() == 1);
    REQUIRE(calls[0].item.name == "caller");
    REQUIRE(calls[0].callSites.size() == 2);
    REQUIRE(calls[0].callSites[0].line == 5);
    REQUIRE(calls[0].callSites[1].line == 9);
}

TEST_CASE("ExtractOutgoingCalls parses \"to\" instead of \"from\", and defaults callSites to empty when "
          "\"fromRanges\" is absent",
          "[Lsp]") {
    const Json result = Json::array({Json{{"to", MakeHierarchyItem("callee", "file:///a.cpp")}}});
    const auto calls  = ExtractOutgoingCalls(result);
    REQUIRE(calls.size() == 1);
    REQUIRE(calls[0].item.name == "callee");
    REQUIRE(calls[0].callSites.empty());
}

TEST_CASE("ExtractIncomingCalls/ExtractOutgoingCalls skip an entry with a malformed item and return empty for a "
          "non-array result",
          "[Lsp]") {
    const Json badItem = Json::array({Json{{"from", Json{{"uri", "file:///a.cpp"}}}}}); // "from" missing "name"
    REQUIRE(ExtractIncomingCalls(badItem).empty());
    REQUIRE(ExtractIncomingCalls(Json(nullptr)).empty());
    REQUIRE(ExtractOutgoingCalls(Json(nullptr)).empty());
}
