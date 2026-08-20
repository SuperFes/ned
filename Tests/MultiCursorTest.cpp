#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Commands.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/KillRing.h"
#include "Text/Rope.h"

using ned::text::Buffer;
using ned::text::Rope;

namespace {

// The command-level half of these tests -- mirrors CommandsTest.cpp's own
// fixture shape.
struct CommandFixture {
    Buffer                       buffer{"scratch"};
    ned::text::KillRing          killRing;
    ned::text::BufferList        bufferList;
    ned::editor::CommandRegistry registry;
    std::string                  message;

    CommandFixture() {
        ned::editor::RegisterBuiltinCommands(registry);
    }

    void Invoke(const std::string& name) {
        ned::editor::CommandContext context{buffer, killRing, bufferList};
        context.message = &message;
        registry.Invoke(name, context);
    }

    void TypeChar(char32_t codepoint) {
        ned::editor::CommandContext context{buffer, killRing, bufferList};
        context.message                 = &message;
        context.triggeringKey.Codepoint = codepoint;
        registry.Invoke("self-insert-command", context);
    }
};

} // namespace

TEST_CASE("AddCursorAt keeps secondaries sorted, deduplicated, and off the primary", "[MultiCursor]") {
    Buffer buffer("test", Rope("alpha\nbravo\ncharlie\n"));
    buffer.SetPoint(0);

    REQUIRE_FALSE(buffer.HasSecondaryCursors());
    buffer.AddCursorAt(12);
    buffer.AddCursorAt(6);
    REQUIRE(buffer.SecondaryCursors().size() == 2);
    REQUIRE(buffer.SecondaryCursors()[0].point == 6);
    REQUIRE(buffer.SecondaryCursors()[1].point == 12);

    buffer.AddCursorAt(6); // duplicate secondary -- silent no-op
    REQUIRE(buffer.SecondaryCursors().size() == 2);
    buffer.AddCursorAt(0); // the primary's own position -- silent no-op
    REQUIRE(buffer.SecondaryCursors().size() == 2);

    buffer.ClearSecondaryCursors();
    REQUIRE_FALSE(buffer.HasSecondaryCursors());
}

TEST_CASE("AddCursorAt snaps into the buffer and onto grapheme boundaries", "[MultiCursor]") {
    Buffer buffer("test", Rope("a\xC3\xA9z")); // 'a', e-acute (2 bytes), 'z'
    buffer.SetPoint(0);

    buffer.AddCursorAt(2); // mid-codepoint -- must snap, not split the character
    REQUIRE(buffer.SecondaryCursors().size() == 1);
    const std::size_t snapped = buffer.SecondaryCursors()[0].point;
    REQUIRE((snapped == 1 || snapped == 3));

    buffer.AddCursorAt(999); // past the end -- clamps to the end
    REQUIRE(buffer.SecondaryCursors().back().point == buffer.Content().ByteLength());
}

TEST_CASE("Secondary cursors relocate across ordinary single-cursor edits", "[MultiCursor]") {
    Buffer buffer("test", Rope("alpha\nbravo\n"));
    buffer.SetPoint(0);
    buffer.AddCursorAt(6, 8); // cursor at "bravo"'s start, mark inside it

    buffer.InsertAtPoint("xy"); // primary inserts at 0 -- everything after shifts
    REQUIRE(buffer.SecondaryCursors()[0].point == 8);
    REQUIRE(buffer.SecondaryCursors()[0].mark == 10);

    buffer.DeleteRange(0, 2); // and shifts back
    REQUIRE(buffer.SecondaryCursors()[0].point == 6);
    REQUIRE(buffer.SecondaryCursors()[0].mark == 8);
}

TEST_CASE("ForEachCursor applies an insert at every cursor as one undo step", "[MultiCursor]") {
    Buffer buffer("test", Rope("one\ntwo\nthree\n"));
    buffer.SetPoint(0);    // line 1 start
    buffer.AddCursorAt(4); // line 2 start
    buffer.AddCursorAt(8); // line 3 start

    buffer.ForEachCursor([&buffer] { buffer.InsertAtPoint("> "); });

    REQUIRE(buffer.Text() == "> one\n> two\n> three\n");
    // Every cursor sits just after its own inserted prefix.
    REQUIRE(buffer.Point() == 2);
    REQUIRE(buffer.SecondaryCursors()[0].point == 8);
    REQUIRE(buffer.SecondaryCursors()[1].point == 14);

    buffer.Undo(); // one step undoes all three insertions
    REQUIRE(buffer.Text() == "one\ntwo\nthree\n");
    REQUIRE_FALSE(buffer.CanUndo());
}

TEST_CASE("ForEachCursor works for deletion and motion operations too", "[MultiCursor]") {
    Buffer buffer("test", Rope("xone\nxtwo\n"));
    buffer.SetPoint(1);
    buffer.AddCursorAt(6);

    buffer.ForEachCursor([&buffer] { buffer.DeleteBackwardAtPoint(); });
    REQUIRE(buffer.Text() == "one\ntwo\n");
    REQUIRE(buffer.Point() == 0);
    REQUIRE(buffer.SecondaryCursors()[0].point == 4);

    buffer.ForEachCursor([&buffer] { buffer.MoveForward(); });
    REQUIRE(buffer.Point() == 1);
    REQUIRE(buffer.SecondaryCursors()[0].point == 5);
}

TEST_CASE("Cursors collapsed onto one position by an edit merge", "[MultiCursor]") {
    Buffer buffer("test", Rope("abcdef"));
    buffer.SetPoint(0);
    buffer.AddCursorAt(2);
    buffer.AddCursorAt(4);

    // Deleting [1, 5) collapses both secondaries onto offset 1.
    buffer.DeleteRange(1, 4);
    REQUIRE(buffer.Text() == "af");
    REQUIRE(buffer.SecondaryCursors().size() == 1);
    REQUIRE(buffer.SecondaryCursors()[0].point == 1);
}

TEST_CASE("Undo and redo clear secondary cursors", "[MultiCursor]") {
    Buffer buffer("test", Rope("hello"));
    buffer.SetPoint(0);
    buffer.InsertAtPoint("x");
    buffer.AddCursorAt(3);

    buffer.Undo();
    REQUIRE_FALSE(buffer.HasSecondaryCursors());

    buffer.AddCursorAt(3);
    buffer.Redo();
    REQUIRE_FALSE(buffer.HasSecondaryCursors());
}

TEST_CASE("Undo groups batch arbitrary edits into one step, nestably", "[MultiCursor]") {
    Buffer buffer("test", Rope("abc"));
    buffer.SetPoint(0);

    buffer.BeginUndoGroup();
    buffer.InsertAtPoint("1");
    buffer.BeginUndoGroup();
    buffer.SetPoint(buffer.Content().ByteLength());
    buffer.InsertAtPoint("2");
    buffer.EndUndoGroup(); // inner end -- still grouped
    buffer.DeleteBackwardAtPoint();
    buffer.EndUndoGroup();

    REQUIRE(buffer.Text() == "1abc");
    buffer.Undo();
    REQUIRE(buffer.Text() == "abc");
    REQUIRE_FALSE(buffer.CanUndo());

    // An empty group records nothing: still at the undo root afterward.
    buffer.BeginUndoGroup();
    buffer.EndUndoGroup();
    REQUIRE_FALSE(buffer.CanUndo());
}

TEST_CASE("An insert after an undo group does not amend into the group's step", "[MultiCursor]") {
    Buffer buffer("test", Rope(""));
    buffer.SetPoint(0);

    buffer.BeginUndoGroup();
    buffer.InsertAtPoint("grouped");
    buffer.EndUndoGroup();
    buffer.InsertAtPoint("X"); // must be its own step, not amended into the group

    REQUIRE(buffer.Text() == "groupedX");
    buffer.Undo();
    REQUIRE(buffer.Text() == "grouped");
    buffer.Undo();
    REQUIRE(buffer.Text().empty());
}

TEST_CASE("add-cursor-below/above grow a column of cursors at the same visual column", "[MultiCursor]") {
    CommandFixture fixture;
    fixture.buffer.InsertAtPoint("alpha\nbravo\ncharlie\n");
    fixture.buffer.SetPoint(2); // line 0, column 2

    fixture.Invoke("add-cursor-below");
    REQUIRE(fixture.buffer.SecondaryCursors().size() == 1);
    REQUIRE(fixture.buffer.SecondaryCursors()[0].point == 8); // line 1, column 2

    fixture.Invoke("add-cursor-below"); // extends from the bottom-most cursor
    REQUIRE(fixture.buffer.SecondaryCursors().size() == 2);
    REQUIRE(fixture.buffer.SecondaryCursors()[1].point == 14); // line 2, column 2

    // At the last content line: pressing again against the trailing empty
    // line clamps to its (column-0) start; one more is a no-op at the end.
    fixture.Invoke("add-cursor-above"); // top-most is the primary at line 0 -- no line above
    REQUIRE(fixture.buffer.SecondaryCursors().size() == 2);
}

TEST_CASE("Typing with multiple cursors edits every line and undoes as one step", "[MultiCursor]") {
    CommandFixture fixture;
    fixture.buffer.InsertAtPoint("one\ntwo\n");
    fixture.buffer.SetPoint(0);
    fixture.buffer.AddCursorAt(4);

    fixture.TypeChar(U'#');
    REQUIRE(fixture.buffer.Text() == "#one\n#two\n");

    fixture.Invoke("backward-delete-char");
    REQUIRE(fixture.buffer.Text() == "one\ntwo\n");

    fixture.Invoke("undo"); // undoes the multi-cursor delete as one step
    REQUIRE(fixture.buffer.Text() == "#one\n#two\n");
    REQUIRE_FALSE(fixture.buffer.HasSecondaryCursors()); // undo collapses, by design
}

TEST_CASE("select-next-occurrence selects the word first, then adds selecting cursors", "[MultiCursor]") {
    CommandFixture fixture;
    fixture.buffer.InsertAtPoint("foo bar foo baz foo\n");
    fixture.buffer.SetPoint(1); // inside the first "foo"

    fixture.Invoke("select-next-occurrence"); // first press: select the word
    REQUIRE(fixture.buffer.HasMark());
    REQUIRE(fixture.buffer.Region() == std::pair<std::size_t, std::size_t>{0, 3});
    REQUIRE_FALSE(fixture.buffer.HasSecondaryCursors());

    fixture.Invoke("select-next-occurrence"); // second press: cursor at occurrence 2
    REQUIRE(fixture.buffer.SecondaryCursors().size() == 1);
    REQUIRE(fixture.buffer.SecondaryCursors()[0].mark == 8);
    REQUIRE(fixture.buffer.SecondaryCursors()[0].point == 11);

    fixture.Invoke("select-next-occurrence"); // occurrence 3
    REQUIRE(fixture.buffer.SecondaryCursors().size() == 2);

    fixture.Invoke("select-next-occurrence"); // exhausted -- wraps, finds all owned
    REQUIRE(fixture.buffer.SecondaryCursors().size() == 2);
    REQUIRE(fixture.message.find("No more occurrences") != std::string::npos);
}

TEST_CASE("select-all-occurrences adds a cursor per match in one shot", "[MultiCursor]") {
    CommandFixture fixture;
    fixture.buffer.InsertAtPoint("x = x + x\n");
    fixture.buffer.SetPoint(0);

    fixture.Invoke("select-all-occurrences");
    REQUIRE(fixture.buffer.SecondaryCursors().size() == 2);
    REQUIRE(fixture.message == "3 cursors");
}

TEST_CASE("keyboard-quit collapses to a single cursor", "[MultiCursor]") {
    CommandFixture fixture;
    fixture.buffer.InsertAtPoint("one two\n");
    fixture.buffer.SetPoint(0);
    fixture.buffer.AddCursorAt(4);
    fixture.buffer.SetMark(2);

    fixture.Invoke("keyboard-quit");
    REQUIRE_FALSE(fixture.buffer.HasSecondaryCursors());
    REQUIRE_FALSE(fixture.buffer.HasMark());
}
