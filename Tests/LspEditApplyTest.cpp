#include <catch2/catch_test_macros.hpp>

#include "Editor/Lsp/LspEditApply.h"
#include "Text/Buffer.h"
#include "Text/Rope.h"

using ned::editor::lsp::ApplyWorkspaceTextEdits;
using ned::editor::lsp::ApplyWorkspaceTextEditsAndRelocate;
using ned::editor::lsp::LspPosition;
using ned::editor::lsp::WorkspaceTextEdit;
using ned::text::Buffer;
using ned::text::Rope;

namespace {

Buffer MakeBuffer(const std::string& content) {
    return Buffer("test", Rope(content));
}

WorkspaceTextEdit Edit(std::size_t startLine, std::size_t startChar, std::size_t endLine, std::size_t endChar, std::string newText) {
    return WorkspaceTextEdit{.start   = LspPosition{.line = startLine, .character = startChar},
                             .end     = LspPosition{.line = endLine, .character = endChar},
                             .newText = std::move(newText)};
}

} // namespace

TEST_CASE("ApplyWorkspaceTextEditsAndRelocate moves an anchor past an earlier insertion", "[LspEditApply]") {
    // The completion-accept case: an "#include" lands at the top of the file
    // while the range about to be replaced sits further down.
    Buffer            buffer = MakeBuffer("#include <a>\nint main() { vec }\n");
    const std::size_t anchor = buffer.Text().find("vec");

    const std::size_t moved = ApplyWorkspaceTextEditsAndRelocate(buffer, {Edit(1, 0, 1, 0, "#include <vector>\n")}, anchor);

    CHECK(moved == anchor + std::string("#include <vector>\n").size());
    CHECK(buffer.Text().substr(moved, 3) == "vec");
}

TEST_CASE("ApplyWorkspaceTextEditsAndRelocate leaves an anchor alone for a later edit", "[LspEditApply]") {
    Buffer            buffer = MakeBuffer("alpha\nomega\n");
    const std::size_t anchor = 0;

    CHECK(ApplyWorkspaceTextEditsAndRelocate(buffer, {Edit(1, 0, 1, 0, "inserted\n")}, anchor) == 0);
}

TEST_CASE("ApplyWorkspaceTextEditsAndRelocate accounts for a deletion before the anchor", "[LspEditApply]") {
    Buffer            buffer = MakeBuffer("delete-me\nkeep\n");
    const std::size_t anchor = buffer.Text().find("keep");

    const std::size_t moved = ApplyWorkspaceTextEditsAndRelocate(buffer, {Edit(0, 0, 1, 0, "")}, anchor);

    CHECK(moved == 0);
    CHECK(buffer.Text().substr(moved, 4) == "keep");
}

TEST_CASE("ApplyWorkspaceTextEditsAndRelocate relocates across several edits at once", "[LspEditApply]") {
    Buffer            buffer = MakeBuffer("a\nb\nTARGET\nc\n");
    const std::size_t anchor = buffer.Text().find("TARGET");

    const std::size_t moved = ApplyWorkspaceTextEditsAndRelocate(buffer,
                                                                 {
                                                                     Edit(0, 0, 0, 0, "xx"), // before
                                                                     Edit(1, 0, 1, 1, ""),   // before, shrinks
                                                                     Edit(3, 0, 3, 0, "yy"), // after -- must not count
                                                                 },
                                                                 anchor);

    CHECK(buffer.Text().substr(moved, 6) == "TARGET");
}

TEST_CASE("ApplyWorkspaceTextEdits still applies every edit as one undo group", "[LspEditApply]") {
    // The pre-existing behavior the relocating overload was factored out of.
    Buffer buffer = MakeBuffer("one\ntwo\n");
    ApplyWorkspaceTextEdits(buffer, {Edit(0, 0, 0, 3, "1"), Edit(1, 0, 1, 3, "2")});
    REQUIRE(buffer.Text() == "1\n2\n");

    buffer.Undo();
    CHECK(buffer.Text() == "one\ntwo\n");
}
