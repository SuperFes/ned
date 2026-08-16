#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "Text/Buffer.h"

using ned::text::Buffer;

TEST_CASE("Fresh buffer holds its initial content", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("hello"));

    REQUIRE(buffer.Name() == "scratch");
    REQUIRE(buffer.Text() == "hello");
    REQUIRE(buffer.Size() == 5);
    REQUIRE(buffer.Point() == 0);
    REQUIRE_FALSE(buffer.HasMark());
    REQUIRE_FALSE(buffer.CanUndo());
    REQUIRE_FALSE(buffer.CanRedo());
}

TEST_CASE("InsertAtPoint inserts and advances point", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("hello"));

    buffer.InsertAtPoint("abc");
    REQUIRE(buffer.Text() == "abchello");
    REQUIRE(buffer.Point() == 3);
}

TEST_CASE("Consecutive InsertAtPoint calls coalesce into one undo step", "[Buffer]") {
    Buffer buffer("scratch");

    buffer.InsertAtPoint("a");
    buffer.InsertAtPoint("b");
    buffer.InsertAtPoint("c");
    REQUIRE(buffer.Text() == "abc");

    buffer.Undo();
    REQUIRE(buffer.Text() == "");
    REQUIRE_FALSE(buffer.CanUndo());
}

TEST_CASE("Moving point between inserts breaks the coalescing run", "[Buffer]") {
    Buffer buffer("scratch");

    buffer.InsertAtPoint("a");
    buffer.MoveBackward(); // point: 1 -> 0, breaks the run
    buffer.InsertAtPoint("b");
    REQUIRE(buffer.Text() == "ba");

    buffer.Undo();
    REQUIRE(buffer.Text() == "a");

    buffer.Undo();
    REQUIRE(buffer.Text() == "");
    REQUIRE_FALSE(buffer.CanUndo());
}

TEST_CASE("Redo replays undone steps", "[Buffer]") {
    Buffer buffer("scratch");

    buffer.InsertAtPoint("a");
    buffer.MoveBackward();
    buffer.InsertAtPoint("b");

    buffer.Undo();
    buffer.Undo();
    REQUIRE(buffer.Text() == "");

    buffer.Redo();
    REQUIRE(buffer.Text() == "a");
    buffer.Redo();
    REQUIRE(buffer.Text() == "ba");
}

TEST_CASE("DeleteBackwardAtPoint removes a whole grapheme cluster, not one byte", "[Buffer]") {
    // "e" + combining acute accent (2 bytes) is one cluster.
    Buffer buffer("scratch", ned::text::Rope(std::string("xe\xCC\x81y")));

    buffer.SetPoint(4); // right after the cluster, before 'y'
    buffer.DeleteBackwardAtPoint();

    REQUIRE(buffer.Text() == "xy");
    REQUIRE(buffer.Point() == 1);
}

TEST_CASE("DeleteForwardAtPoint removes a whole grapheme cluster at point", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope(std::string("xe\xCC\x81y")));

    buffer.SetPoint(1); // start of the cluster
    buffer.DeleteForwardAtPoint();

    REQUIRE(buffer.Text() == "xy");
    REQUIRE(buffer.Point() == 1);
}

TEST_CASE("DeleteBackwardAtPoint and DeleteForwardAtPoint are no-ops at buffer edges", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("ab"));

    buffer.SetPoint(0);
    buffer.DeleteBackwardAtPoint();
    REQUIRE(buffer.Text() == "ab");

    buffer.SetPoint(2);
    buffer.DeleteForwardAtPoint();
    REQUIRE(buffer.Text() == "ab");
}

TEST_CASE("Mark and Region report the range regardless of point/mark order", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("hello world"));

    buffer.SetPoint(6);
    buffer.SetMark(0);
    REQUIRE(buffer.Region() == std::pair<std::size_t, std::size_t>{0, 6});

    buffer.SetPoint(0);
    buffer.SetMark(6);
    REQUIRE(buffer.Region() == std::pair<std::size_t, std::size_t>{0, 6});
}

TEST_CASE("DeleteRange returns the removed text and adjusts point/mark", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("hello world"));

    buffer.SetPoint(11); // end
    buffer.SetMark(0);

    const std::string removed = buffer.DeleteRange(5, 6); // " world"
    REQUIRE(removed == " world");
    REQUIRE(buffer.Text() == "hello");
    REQUIRE(buffer.Point() == 5); // was past the deleted range, shifted back
    REQUIRE(buffer.Mark() == 0);  // was before the deleted range, unaffected
}

TEST_CASE("Buffer round-trips through SaveToFile/FromFile", "[Buffer]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_test_roundtrip.txt";

    {
        Buffer buffer("scratch", ned::text::Rope("hello, file"));
        buffer.SaveToFile(path);
        REQUIRE(buffer.Path().has_value());
        REQUIRE(*buffer.Path() == path);
    }

    Buffer loaded = Buffer::FromFile(path);
    REQUIRE(loaded.Text() == "hello, file");

    std::filesystem::remove(path);
}

TEST_CASE("Buffer::FromFile throws for a missing file", "[Buffer]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_test_does_not_exist.txt";
    std::filesystem::remove(path);

    REQUIRE_THROWS_AS(Buffer::FromFile(path), std::runtime_error);
}

TEST_CASE("FromFile strips a leading UTF-8 byte-order mark", "[Buffer]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_test_bom.txt";
    {
        std::ofstream file(path, std::ios::binary);
        file << "\xEF\xBB\xBF"
             << "hello";
    }

    const Buffer buffer = Buffer::FromFile(path);
    REQUIRE(buffer.Text() == "hello");

    std::filesystem::remove(path);
}

TEST_CASE("FromFile reads a file with no BOM unchanged", "[Buffer]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_test_no_bom.txt";
    {
        std::ofstream file(path, std::ios::binary);
        file << "hello";
    }

    const Buffer buffer = Buffer::FromFile(path);
    REQUIRE(buffer.Text() == "hello");

    std::filesystem::remove(path);
}

TEST_CASE("SaveToFile leaves no leftover temp file after a successful save", "[Buffer]") {
    const std::filesystem::path path     = std::filesystem::temp_directory_path() / "ned_buffer_test_no_leftover.txt";
    const std::filesystem::path tempPath = path.string() + ".ned-tmp";
    std::filesystem::remove(path);
    std::filesystem::remove(tempPath);

    Buffer buffer("scratch", ned::text::Rope("content"));
    buffer.SaveToFile(path);

    REQUIRE(std::filesystem::exists(path));
    REQUIRE_FALSE(std::filesystem::exists(tempPath));

    std::filesystem::remove(path);
}

TEST_CASE("A failed save doesn't corrupt or replace the original file", "[Buffer]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_buffer_test_atomic_save";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path path     = dir / "file.txt";
    const std::filesystem::path tempPath = path.string() + ".ned-tmp";

    {
        Buffer original("scratch", ned::text::Rope("original content"));
        original.SaveToFile(path);
    }

    // Occupy the exact temp-file path with a directory, so opening it as a
    // regular file for writing is guaranteed to fail regardless of
    // permissions/user (unlike e.g. chmod-based tricks, which root ignores).
    std::filesystem::create_directory(tempPath);

    Buffer replacement("scratch", ned::text::Rope("this must never land on disk"));
    REQUIRE_THROWS_AS(replacement.SaveToFile(path), std::runtime_error);

    std::ifstream     check(path, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(check)), std::istreambuf_iterator<char>());
    REQUIRE(content == "original content");

    std::filesystem::remove_all(dir);
}

TEST_CASE("MoveToNextLine/MoveToPreviousLine land on the same column", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("abcde\nfghij\nklmno"));

    buffer.SetPoint(2); // line 0, column 2 ('c')
    buffer.MoveToNextLine();
    REQUIRE(buffer.Point() == 8); // line 1 starts at byte 6, column 2 -> 'h'

    buffer.MoveToNextLine();
    REQUIRE(buffer.Point() == 14); // line 2 starts at byte 12, column 2 -> 'm'

    buffer.MoveToPreviousLine();
    REQUIRE(buffer.Point() == 8);
}

TEST_CASE("MoveToNextLine/MoveToPreviousLine are no-ops at the buffer's first/last line", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("only one line"));

    buffer.SetPoint(4);
    buffer.MoveToPreviousLine();
    REQUIRE(buffer.Point() == 4);

    buffer.MoveToNextLine();
    REQUIRE(buffer.Point() == 4);
}

TEST_CASE("A run of vertical moves remembers the goal column through a shorter line", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("abcdefg\nab\nabcdefg"));

    buffer.SetPoint(5); // line 0, column 5

    buffer.MoveToNextLine();
    REQUIRE(buffer.Point() == 10); // line 1 ("ab") only has 2 columns -- clamped to its end

    buffer.MoveToNextLine();
    REQUIRE(buffer.Point() == 16); // line 2, back to column 5 -- the original goal, not the clamped one
}

TEST_CASE("Any other point-moving call resets the remembered goal column", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("abcdefg\nab\nabcdefg"));

    buffer.SetPoint(5);
    buffer.MoveToNextLine();
    REQUIRE(buffer.Point() == 10); // clamped to column 2 (end of "ab"), goal (5) remembered

    buffer.MoveBackward();        // an ordinary point move, still on the same line -- must clear the remembered goal
    REQUIRE(buffer.Point() == 9); // column 1 on the short line ('b')

    buffer.MoveToNextLine();
    REQUIRE(buffer.Point() == 12); // uses the *new* current column (1), not the stale goal of 5
}

TEST_CASE("MoveForwardWord/MoveBackwardWord land just past/before a word", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("  hello, world!  "));

    buffer.SetPoint(0);
    buffer.MoveForwardWord();
    REQUIRE(buffer.Point() == 7); // skips "  ", lands right after "hello"

    buffer.MoveForwardWord();
    REQUIRE(buffer.Point() == 14); // skips ", ", lands right after "world"

    buffer.MoveBackwardWord();
    REQUIRE(buffer.Point() == 9); // lands right before "world"

    buffer.MoveBackwardWord();
    REQUIRE(buffer.Point() == 2); // skips back over ", " -- lands right before "hello"
}

TEST_CASE("MoveForwardWord/MoveBackwardWord are no-ops at the buffer's edges", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("word"));

    buffer.SetPoint(4);
    buffer.MoveForwardWord();
    REQUIRE(buffer.Point() == 4);

    buffer.SetPoint(0);
    buffer.MoveBackwardWord();
    REQUIRE(buffer.Point() == 0);
}

TEST_CASE("MoveDownLines/MoveUpLines move by more than one line at once", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("aaaa\nbbbb\ncccc\ndddd\neeee"));

    buffer.SetPoint(2); // line 0, column 2
    buffer.MoveDownLines(3);
    REQUIRE(buffer.Point() == 17); // line 3 ("dddd"), column 2 -> byte 15 + 2

    buffer.MoveUpLines(2);
    REQUIRE(buffer.Point() == 7); // line 1 ("bbbb"), column 2
}

TEST_CASE("MoveDownLines/MoveUpLines clamp to the last/first line instead of no-op-ing when the count overshoots", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("aaaa\nbbbb\ncccc"));

    buffer.SetPoint(2); // line 0, column 2
    buffer.MoveDownLines(100);
    REQUIRE(buffer.Point() == 12); // clamped to line 2 ("cccc"), column 2 -- not a no-op

    buffer.MoveUpLines(100);
    REQUIRE(buffer.Point() == 2); // clamped back to line 0, column 2
}

TEST_CASE("ByteOffsetForLineAndColumn finds an exact position", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("abcde\nfghij\nklmno"));

    REQUIRE(buffer.ByteOffsetForLineAndColumn(0, 0) == 0);
    REQUIRE(buffer.ByteOffsetForLineAndColumn(0, 3) == 3);
    REQUIRE(buffer.ByteOffsetForLineAndColumn(1, 2) == 8);  // line 1 starts at byte 6
    REQUIRE(buffer.ByteOffsetForLineAndColumn(2, 4) == 16); // line 2 starts at byte 12
}

TEST_CASE("ByteOffsetForLineAndColumn clamps an out-of-range column to the line's end", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("ab\nabcdefg"));

    REQUIRE(buffer.ByteOffsetForLineAndColumn(0, 100) == 2); // line 0 ("ab") is only 2 columns wide
}

TEST_CASE("ByteOffsetForLineAndColumn clamps an out-of-range line to the buffer's last line", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("abc\ndef"));

    REQUIRE(buffer.ByteOffsetForLineAndColumn(100, 1) == 5); // line 1 ("def"), column 1
}

TEST_CASE("ByteOffsetForLineAndColumn with the default tabWidth treats a tab as a single column", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("a\tbc"));

    // Plain codepoint counting -- unchanged from before tab-awareness existed.
    REQUIRE(buffer.ByteOffsetForLineAndColumn(0, 0) == 0); // 'a'
    REQUIRE(buffer.ByteOffsetForLineAndColumn(0, 1) == 1); // '\t'
    REQUIRE(buffer.ByteOffsetForLineAndColumn(0, 2) == 2); // 'b'
}

TEST_CASE("ByteOffsetForLineAndColumn expands tabs when tabWidth > 1", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("a\tbc"));
    // Visual columns with tabWidth=4: 'a'=0, tab spans [1,5), 'b'=5, 'c'=6.

    REQUIRE(buffer.ByteOffsetForLineAndColumn(0, 0, 4) == 0); // 'a'
    REQUIRE(buffer.ByteOffsetForLineAndColumn(0, 1, 4) == 1); // start of the tab
    REQUIRE(buffer.ByteOffsetForLineAndColumn(0, 3, 4) == 2); // inside the tab's span -- snaps past it
    REQUIRE(buffer.ByteOffsetForLineAndColumn(0, 5, 4) == 2); // 'b', right where the tab ends
    REQUIRE(buffer.ByteOffsetForLineAndColumn(0, 6, 4) == 3); // 'c'
}

TEST_CASE("Vertical motion tracks the visual column across tab-containing lines when tabWidth > 1", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("x\ty\nabcdef"));
    // Visual columns (tabWidth=4): line 0 -- 'x'=0, tab spans [1,5), 'y'=5.
    //                              line 1 -- plain, "abcdef" columns 0..5.

    buffer.SetPoint(2); // 'y' on line 0, visual column 5
    buffer.MoveToNextLine(4);
    REQUIRE(buffer.Point() == 9); // line 1 starts at byte 4; column 5 -> 'f' (byte 9)

    buffer.MoveToPreviousLine(4);
    REQUIRE(buffer.Point() == 2); // back to 'y', not off by the tab's width
}

TEST_CASE("Vertical motion with the default tabWidth still treats a tab as a single column", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("x\ty\nabcdef"));

    buffer.SetPoint(2); // 'y' on line 0, codepoint column 2
    buffer.MoveToNextLine();
    REQUIRE(buffer.Point() == 6); // line 1 starts at byte 4; column 2 -> 'c' (byte 6)
}

TEST_CASE("Buffer::NewFile is empty and already associated with the given path", "[Buffer]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_test_newfile.txt";
    std::filesystem::remove(path);

    Buffer buffer = Buffer::NewFile(path);

    REQUIRE(buffer.Name() == "ned_buffer_test_newfile.txt");
    REQUIRE(buffer.Text().empty());
    REQUIRE(buffer.Path().has_value());
    REQUIRE(*buffer.Path() == path);
    REQUIRE_FALSE(std::filesystem::exists(path)); // NewFile doesn't touch disk

    buffer.Save();
    REQUIRE(std::filesystem::exists(path));

    std::filesystem::remove(path);
}

TEST_CASE("SetPath rebinds the buffer's associated path without touching its content or name",
          "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("hello"));
    REQUIRE_FALSE(buffer.Path().has_value());

    buffer.SetPath("/some/new/path.txt");

    REQUIRE(buffer.Path().has_value());
    REQUIRE(*buffer.Path() == std::filesystem::path("/some/new/path.txt"));
    REQUIRE(buffer.Text() == "hello");
    REQUIRE(buffer.Name() == "scratch"); // unchanged -- SetPath doesn't rename
}

TEST_CASE("Modified() tracks unsaved changes, cleared by a successful save", "[Buffer]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_test_modified.txt";
    std::filesystem::remove(path);

    Buffer buffer("scratch");
    REQUIRE_FALSE(buffer.Modified());

    buffer.InsertAtPoint("hello");
    REQUIRE(buffer.Modified());

    buffer.SaveToFile(path);
    REQUIRE_FALSE(buffer.Modified());

    buffer.InsertAtPoint(" again");
    REQUIRE(buffer.Modified());

    std::filesystem::remove(path);
}

TEST_CASE("Modified() is set by deletes, InsertAt, and undo/redo, not just InsertAtPoint", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("hello"));
    REQUIRE_FALSE(buffer.Modified());

    buffer.SetPoint(5);
    buffer.DeleteBackwardAtPoint();
    REQUIRE(buffer.Modified());

    Buffer buffer2("scratch", ned::text::Rope("hello"));
    buffer2.SetPoint(0);
    buffer2.DeleteForwardAtPoint();
    REQUIRE(buffer2.Modified());

    Buffer buffer3("scratch", ned::text::Rope("hello"));
    buffer3.InsertAt(0, "!");
    REQUIRE(buffer3.Modified());

    Buffer buffer4("scratch", ned::text::Rope("hello"));
    buffer4.DeleteRange(0, 1);
    REQUIRE(buffer4.Modified());
}

TEST_CASE("A no-op edit (empty insert, delete at buffer edge) does not set Modified()", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("hello"));

    buffer.InsertAtPoint(""); // empty -- returns early
    REQUIRE_FALSE(buffer.Modified());

    buffer.SetPoint(0);
    buffer.DeleteBackwardAtPoint(); // already at the start -- no-op
    REQUIRE_FALSE(buffer.Modified());
}

TEST_CASE("FromFile/NewFile start out unmodified", "[Buffer]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_test_modified_fresh.txt";
    {
        std::ofstream file(path, std::ios::binary);
        file << "content";
    }

    const Buffer loaded = Buffer::FromFile(path);
    REQUIRE_FALSE(loaded.Modified());

    std::filesystem::remove(path);

    const Buffer created = Buffer::NewFile(path);
    REQUIRE_FALSE(created.Modified());
}

TEST_CASE("Undo/redo mark the buffer modified even when they land back on the saved content", "[Buffer]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_test_modified_undo.txt";
    std::filesystem::remove(path);

    Buffer buffer("scratch");
    buffer.SaveToFile(path);
    REQUIRE_FALSE(buffer.Modified());

    buffer.InsertAtPoint("x");
    buffer.SaveToFile(path);
    REQUIRE_FALSE(buffer.Modified());

    buffer.Undo(); // back to "" -- not what's on disk anymore
    REQUIRE(buffer.Modified());

    buffer.Redo(); // back to "x" -- matches disk again, but still reported modified (see Buffer.h)
    REQUIRE(buffer.Modified());

    std::filesystem::remove(path);
}

// narrow-to-region/widen follow-up. Content used throughout:
// "line0\nline1\nline2\nline3" -- line0 [0,6), line1 [6,12), line2 [12,18),
// line3 [18,23) (no trailing newline, ByteLength() == 23).

TEST_CASE("NarrowToRegion snaps a mid-line region to whole lines", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("line0\nline1\nline2\nline3"));

    buffer.NarrowToRegion(8, 15); // mid-line1 to mid-line2

    REQUIRE(buffer.IsNarrowed());
    REQUIRE(buffer.NarrowedRange() == std::pair<std::size_t, std::size_t>{6, 18});
}

TEST_CASE("Widen clears narrowing", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("line0\nline1\nline2\nline3"));
    REQUIRE_FALSE(buffer.IsNarrowed());

    buffer.NarrowToRegion(6, 12);
    REQUIRE(buffer.IsNarrowed());

    buffer.Widen();
    REQUIRE_FALSE(buffer.IsNarrowed());
}

TEST_CASE("InsertAtPoint at the exact narrowed-range boundary extends the range", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("line0\nline1\nline2\nline3"));
    buffer.NarrowToRegion(0, 10); // snaps to lines 0-1: [0, 12)
    REQUIRE(buffer.NarrowedRange() == std::pair<std::size_t, std::size_t>{0, 12});

    buffer.SetPoint(12);
    buffer.InsertAtPoint("XX");

    // The narrowed range grows to keep including the newly-typed text,
    // rather than desyncing from it -- extending a narrowed region by
    // typing at its own boundary is the single most common narrowing
    // workflow there is, not an edge case.
    REQUIRE(buffer.NarrowedRange() == std::pair<std::size_t, std::size_t>{0, 14});
}

TEST_CASE("DeleteRange spanning across the narrowed start shifts it the same way Point_/Mark_ already do",
          "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("line0\nline1\nline2\nline3"));
    buffer.NarrowToRegion(6, 12); // snaps to lines 1-2: [6, 18)
    REQUIRE(buffer.NarrowedRange() == std::pair<std::size_t, std::size_t>{6, 18});

    buffer.DeleteRange(3, 6); // deletes bytes [3, 9) -- spans across the narrowed start (byte 6)

    REQUIRE(buffer.NarrowedRange() == std::pair<std::size_t, std::size_t>{3, 12});
}

TEST_CASE("Undo restoring shorter content auto-widens a narrowed range that would otherwise become degenerate",
          "[Buffer]") {
    Buffer buffer("scratch");
    buffer.InsertAtPoint("line0\nline1"); // one real undo step
    REQUIRE(buffer.CanUndo());

    buffer.NarrowToRegion(6, 11); // narrow to line1: [6, 11)
    REQUIRE(buffer.NarrowedRange() == std::pair<std::size_t, std::size_t>{6, 11});

    // Narrowing isn't part of undo history (a deliberate design choice,
    // matching real Emacs) -- undoing back to the empty buffer leaves the
    // recorded range referencing byte offsets past the new (zero-length)
    // content entirely.
    buffer.Undo();

    REQUIRE(buffer.Text().empty());
    REQUIRE_FALSE(buffer.IsNarrowed());
}
