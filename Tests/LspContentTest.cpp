#include <catch2/catch_test_macros.hpp>

#include "Editor/Lsp/LspContent.h"

using ned::editor::lsp::CodeAction;
using ned::editor::lsp::CompletionItem;
using ned::editor::lsp::DefinitionLocation;
using ned::editor::lsp::ExtractCodeActions;
using ned::editor::lsp::ExtractCompletionItems;
using ned::editor::lsp::ExtractDefinitionLocations;
using ned::editor::lsp::ExtractHoverText;
using ned::editor::lsp::ExtractRenameEdits;
using ned::editor::lsp::ExtractSingleCodeAction;
using ned::editor::lsp::Json;
using ned::editor::lsp::LspPosition;
using ned::editor::lsp::RenameResult;

TEST_CASE("ExtractHoverText handles a bare string contents field", "[Lsp]") {
    const Json result = {{"contents", "hello world"}};
    const auto text    = ExtractHoverText(result);
    REQUIRE(text.has_value());
    REQUIRE(*text == "hello world");
}

TEST_CASE("ExtractHoverText handles a MarkupContent object contents field", "[Lsp]") {
    const Json result = {{"contents", {{"kind", "markdown"}, {"value", "**bold**"}}}};
    const auto text    = ExtractHoverText(result);
    REQUIRE(text.has_value());
    REQUIRE(*text == "**bold**");
}

TEST_CASE("ExtractHoverText joins an array of contents entries with a blank line", "[Lsp]") {
    const Json result = {{"contents", Json::array({"first", {{"value", "second"}}})}};
    const auto text    = ExtractHoverText(result);
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
    const Json result = Json::array({
        {{"label", "foo"}, {"insertText", "foo()"}},
        {{"label", "bar"}},
    });
    const std::vector<CompletionItem> items = ExtractCompletionItems(result);
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
    const Json result = Json::array({{{"insertText", "no label here"}}, {{"label", "has-label"}}});
    const std::vector<CompletionItem> items = ExtractCompletionItems(result);
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
    const Json result = Json::array({{{"label", "zzz"}}, {{"label", "aaa"}}, {{"label", "mmm"}}});
    const std::vector<CompletionItem> items = ExtractCompletionItems(result);
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
    const Json result = Json::array({{{"command", "no.title"}}, {{"title", "Has a title"}}});
    const std::vector<CodeAction> actions = ExtractCodeActions(result, "file:///a.c");
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].title == "Has a title");
}

TEST_CASE("ExtractCodeActions returns empty for a non-array result", "[Lsp]") {
    REQUIRE(ExtractCodeActions(Json(nullptr), "file:///a.c").empty());
    REQUIRE(ExtractCodeActions(Json::object(), "file:///a.c").empty());
}

TEST_CASE("ExtractSingleCodeAction marks a real CodeAction missing \"edit\" as resolvable", "[Lsp]") {
    const Json item = {{"title", "Remove #include directive"}, {"kind", "quickfix"}, {"data", {{"opaque", 42}}}};
    const CodeAction action = ExtractSingleCodeAction(item, "file:///a.c");

    REQUIRE(action.title == "Remove #include directive");
    REQUIRE_FALSE(action.hasEdit);
    REQUIRE(action.resolvable); // has "kind" -- a real CodeAction, not a bare Command
    REQUIRE(action.raw == item); // preserved verbatim for codeAction/resolve to send back
}

TEST_CASE("ExtractSingleCodeAction does NOT mark a bare Command (no \"kind\") as resolvable", "[Lsp]") {
    const Json item = {{"title", "Run a server-side fixit"}, {"command", "myserver.fixit"}};
    const CodeAction action = ExtractSingleCodeAction(item, "file:///a.c");

    REQUIRE_FALSE(action.hasEdit);
    REQUIRE_FALSE(action.resolvable);
}

TEST_CASE("ExtractSingleCodeAction is not resolvable once it already has an edit", "[Lsp]") {
    const Json textEdit = {{"range", MakeRange(0, 0, 0, 1)}, {"newText", "x"}};
    const Json changes   = {{"file:///a.c", Json::array({textEdit})}};
    const Json edit      = {{"changes", changes}};
    const Json item      = {{"title", "Fix"}, {"kind", "quickfix"}, {"edit", edit}};

    const CodeAction action = ExtractSingleCodeAction(item, "file:///a.c");

    REQUIRE(action.hasEdit);
    REQUIRE_FALSE(action.resolvable); // already has its edit -- nothing to resolve
}

TEST_CASE("ExtractDefinitionLocations parses a bare Location object", "[Lsp]") {
    const Json result = {{"uri", "file:///a.c"}, {"range", MakeRange(4, 2, 4, 10)}};
    const std::vector<DefinitionLocation> locations = ExtractDefinitionLocations(result);
    REQUIRE(locations.size() == 1);
    REQUIRE(locations[0].uri == "file:///a.c");
    REQUIRE(locations[0].position == LspPosition{.line = 4, .character = 2});
}

TEST_CASE("ExtractDefinitionLocations parses a Location[] array", "[Lsp]") {
    const Json result = Json::array({
        {{"uri", "file:///a.c"}, {"range", MakeRange(1, 0, 1, 1)}},
        {{"uri", "file:///b.c"}, {"range", MakeRange(2, 0, 2, 1)}},
    });
    const std::vector<DefinitionLocation> locations = ExtractDefinitionLocations(result);
    REQUIRE(locations.size() == 2);
    REQUIRE(locations[0].uri == "file:///a.c");
    REQUIRE(locations[1].uri == "file:///b.c");
    REQUIRE(locations[1].position == LspPosition{.line = 2, .character = 0});
}

TEST_CASE("ExtractDefinitionLocations parses a LocationLink[] array via targetUri/targetSelectionRange", "[Lsp]") {
    const Json result = Json::array({
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
    const Json result = Json::array({{{"range", MakeRange(0, 0, 0, 1)}}, {{"uri", "file:///a.c"}, {"range", MakeRange(0, 0, 0, 1)}}});
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
    const Json result = {{"documentChanges", Json::array()}};
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
