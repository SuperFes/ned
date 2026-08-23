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

    // multi-cursor-round-2 follow-up: like Invoke, but hands back the
    // CommandContext afterward so a test can inspect an outbound field
    // (newlyAddedCursorPoint) a plain Invoke() would otherwise discard.
    ned::editor::CommandContext InvokeAndReturnContext(const std::string& name) {
        ned::editor::CommandContext context{buffer, killRing, bufferList};
        context.message = &message;
        registry.Invoke(name, context);
        return context;
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

// multi-cursor-round-2 follow-up: per-cursor kill-ring, and scroll-to-new-cursor
// reporting via context.newlyAddedCursorPoint.

TEST_CASE("add-cursor-below/add-cursor-above/select-next-occurrence report the newly added offset",
          "[MultiCursor]") {
    {
        CommandFixture fixture;
        fixture.buffer.InsertAtPoint("alpha\nbravo\ncharlie\n");
        fixture.buffer.SetPoint(2); // line 0, column 2

        auto context = fixture.InvokeAndReturnContext("add-cursor-below");
        REQUIRE(context.newlyAddedCursorPoint.has_value());
        REQUIRE(*context.newlyAddedCursorPoint == 8); // line 1, column 2 -- same as the earlier column test
    }
    {
        CommandFixture fixture;
        fixture.buffer.InsertAtPoint("foo bar foo baz foo\n");
        fixture.buffer.SetPoint(1);
        fixture.Invoke("select-next-occurrence"); // first press: word selection, no cursor added

        auto context = fixture.InvokeAndReturnContext("select-next-occurrence"); // second press: adds a cursor
        REQUIRE(context.newlyAddedCursorPoint.has_value());
        REQUIRE(*context.newlyAddedCursorPoint == 11);
    }
    {
        // select-all-occurrences deliberately never sets this -- no single
        // natural target when many cursors are added at once.
        CommandFixture fixture;
        fixture.buffer.InsertAtPoint("x = x + x\n");
        fixture.buffer.SetPoint(0);

        auto context = fixture.InvokeAndReturnContext("select-all-occurrences");
        REQUIRE_FALSE(context.newlyAddedCursorPoint.has_value());
    }
}

TEST_CASE("Multi-cursor kill-line kills one piece per cursor; yank distributes 1:1", "[MultiCursor]") {
    CommandFixture fixture;
    fixture.buffer.InsertAtPoint("foo\nbar\n");
    fixture.buffer.SetPoint(0);    // start of "foo"
    fixture.buffer.AddCursorAt(4); // start of "bar"

    fixture.Invoke("kill-line");
    REQUIRE(fixture.buffer.Text() == "\n\n");

    fixture.buffer.ClearSecondaryCursors();
    fixture.buffer.SetPoint(0);
    fixture.buffer.AddCursorAt(1);

    fixture.Invoke("yank");
    REQUIRE(fixture.buffer.Text() == "foo\nbar\n");
}

TEST_CASE("Multi-cursor yank falls back to the whole joined blob on a piece-count mismatch", "[MultiCursor]") {
    CommandFixture fixture;
    fixture.buffer.InsertAtPoint("foo\nbar\n");
    fixture.buffer.SetPoint(0);
    fixture.buffer.AddCursorAt(4);
    fixture.Invoke("kill-line"); // pushes a 2-piece entry: "foo", "bar"
    REQUIRE(fixture.buffer.Text() == "\n\n");

    fixture.buffer.ClearSecondaryCursors();
    fixture.buffer.SetPoint(0);
    fixture.buffer.AddCursorAt(1);
    fixture.buffer.AddCursorAt(2); // 3 live cursors now, but the entry only has 2 pieces

    fixture.Invoke("yank");

    std::size_t       count = 0;
    std::size_t       pos   = 0;
    const std::string text  = fixture.buffer.Text();
    while ((pos = text.find("foo\nbar", pos)) != std::string::npos) {
        ++count;
        ++pos;
    }
    REQUIRE(count == 3); // the whole joined blob, at every cursor
}

TEST_CASE("Multi-cursor kill-region contributes an empty piece for a cursor with no mark", "[MultiCursor]") {
    CommandFixture fixture;
    fixture.buffer.InsertAtPoint("hello world");
    fixture.buffer.SetPoint(0);
    fixture.buffer.SetMark(5);      // primary's own region: "hello"
    fixture.buffer.AddCursorAt(11); // no mark of its own

    fixture.Invoke("kill-region");
    REQUIRE(fixture.buffer.Text() == " world");
}
