#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
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
        // ensureFinalNewline=false: this test is about round-trip fidelity,
        // not the final-newline feature (see FinalNewlineTest.cpp for that).
        buffer.SaveToFile(path, false);
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

TEST_CASE("SaveToFile appends a trailing newline by default when the content is missing one", "[Buffer]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_test_final_newline.txt";

    Buffer buffer("scratch", ned::text::Rope("no newline here"));
    buffer.SaveToFile(path);

    std::ifstream     file(path, std::ios::binary);
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(written == "no newline here\n");

    // Disk-only: the buffer's own live content is untouched (see
    // Editor/FinalNewline.h for why).
    REQUIRE(buffer.Text() == "no newline here");

    std::filesystem::remove(path);
}

TEST_CASE("SaveToFile doesn't duplicate an already-present trailing newline", "[Buffer]") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "ned_buffer_test_final_newline_present.txt";

    Buffer buffer("scratch", ned::text::Rope("already has one\n"));
    buffer.SaveToFile(path);

    std::ifstream     file(path, std::ios::binary);
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(written == "already has one\n");

    std::filesystem::remove(path);
}

TEST_CASE("SaveToFile(path, false) leaves content without a trailing newline as-is", "[Buffer]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_test_final_newline_off.txt";

    Buffer buffer("scratch", ned::text::Rope("no newline here"));
    buffer.SaveToFile(path, false);

    std::ifstream     file(path, std::ios::binary);
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(written == "no newline here");

    std::filesystem::remove(path);
}

TEST_CASE("SaveToFile doesn't turn an empty buffer into a bare newline", "[Buffer]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_test_final_newline_empty.txt";

    Buffer buffer("scratch");
    buffer.SaveToFile(path);

    std::ifstream     file(path, std::ios::binary);
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(written.empty());

    std::filesystem::remove(path);
}

TEST_CASE("SaveToFile trims trailing whitespace from every line by default", "[Buffer]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_test_trim_trailing_ws.txt";

    Buffer buffer("scratch", ned::text::Rope("line one   \nline two\t\t\nline three"));
    buffer.SaveToFile(path, /*ensureFinalNewline=*/false);

    std::ifstream     file(path, std::ios::binary);
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(written == "line one\nline two\nline three");

    // Disk-only: the buffer's own live content is untouched.
    REQUIRE(buffer.Text() == "line one   \nline two\t\t\nline three");

    std::filesystem::remove(path);
}

TEST_CASE("SaveToFile collapses trailing blank lines by default", "[Buffer]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_test_trim_trailing_blank.txt";

    Buffer buffer("scratch", ned::text::Rope("content\n\n\n   \n"));
    buffer.SaveToFile(path);

    std::ifstream     file(path, std::ios::binary);
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    // Trimmed down to zero trailing newlines, then ensureFinalNewline (also
    // default true) puts exactly one back.
    REQUIRE(written == "content\n");

    std::filesystem::remove(path);
}

TEST_CASE("SaveToFile(path, ensureFinalNewline, false) leaves trailing whitespace and blank lines as-is", "[Buffer]") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "ned_buffer_test_trim_trailing_off.txt";

    Buffer buffer("scratch", ned::text::Rope("line one   \n\n\n"));
    buffer.SaveToFile(path, /*ensureFinalNewline=*/false, /*trimTrailingWhitespace=*/false);

    std::ifstream     file(path, std::ios::binary);
    const std::string written((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    REQUIRE(written == "line one   \n\n\n");

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
        // ensureFinalNewline=false: this test asserts the raw on-disk bytes
        // exactly match what was written, unrelated to the final-newline
        // feature (see FinalNewlineTest.cpp for that).
        original.SaveToFile(path, false);
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

TEST_CASE("MoveForwardSentence lands at the start of the next sentence", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("Foo. Bar. Baz."));

    buffer.SetPoint(0);
    buffer.MoveForwardSentence();
    REQUIRE(buffer.Point() == 5); // past "Foo. " -- start of "Bar."

    buffer.MoveForwardSentence();
    REQUIRE(buffer.Point() == 10); // past "Bar. " -- start of "Baz."

    buffer.MoveForwardSentence();
    REQUIRE(buffer.Point() == 14); // no more sentence ends -- lands at buffer end
}

TEST_CASE("MoveBackwardSentence lands at the start of the current/previous sentence", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("Foo. Bar. Baz."));

    buffer.SetPoint(14); // buffer end
    buffer.MoveBackwardSentence();
    REQUIRE(buffer.Point() == 10); // start of "Baz.", the sentence point was in

    buffer.MoveBackwardSentence();
    REQUIRE(buffer.Point() == 5); // start of "Bar."

    buffer.MoveBackwardSentence();
    REQUIRE(buffer.Point() == 0); // start of "Foo."
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

TEST_CASE("Deleting the very last byte of a non-empty buffer still sets Modified()", "[Buffer]") {
    // Regression: MarkUnsavedRangeDeleted used to have no byte left after
    // the collapse point to mark whenever a delete ran through to the end
    // of whatever content remained, silently recording nothing --
    // harmless while Modified() was its own separately-tracked bool, but
    // once Modified() was unified onto UnsavedChangeRanges_ (see that
    // method's own doc comment) this meant deleting the last character of
    // a buffer reported Modified() == false for an edit that plainly
    // happened. Fixed by falling back to marking the preceding byte.
    Buffer buffer("scratch", ned::text::Rope("hello"));
    buffer.SetPoint(5);
    buffer.DeleteBackwardAtPoint(); // "hello" -> "hell"
    REQUIRE(buffer.Text() == "hell");
    REQUIRE(buffer.Modified());
    REQUIRE_FALSE(buffer.UnsavedChangeRanges().empty());
}

TEST_CASE("Deleting a buffer down to completely empty still sets Modified()", "[Buffer]") {
    // The one case a byte range genuinely cannot represent (nothing left
    // anywhere to point at) -- Modified() checks this directly, see its
    // own doc comment. UnsavedChangeRanges() itself stays empty here: there
    // really is no line left to highlight in the gutter, which is correct
    // for that signal even though Modified() must still report true.
    Buffer buffer("scratch", ned::text::Rope("x"));
    buffer.SetPoint(1);
    buffer.DeleteBackwardAtPoint(); // "x" -> ""
    REQUIRE(buffer.Text().empty());
    REQUIRE(buffer.Modified());
    REQUIRE(buffer.UnsavedChangeRanges().empty());
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

TEST_CASE("Undo/redo report not modified once they land back on the saved content", "[Buffer]") {
    // Was "mark the buffer modified even when they land back on the saved
    // content" -- that was itself a real, user-reported bug (found via
    // live testing: undoing a single trivial edit still prompted to save,
    // even though the gutter's own separate tracking correctly showed no
    // unsaved change). Modified() is now derived from the same
    // UnsavedChangeRanges_/SavedSnapshot_ tracking the gutter uses -- see
    // Modified()'s own doc comment -- so the two can no longer disagree.
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

    buffer.Redo(); // back to "x" -- matches disk again, no longer modified
    REQUIRE_FALSE(buffer.Modified());

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

TEST_CASE("DeleteBackwardAtPoint/DeleteForwardAtPoint shrink a narrowed range that crosses the delete point",
          "[Buffer]") {
    // Before the Buffer::RelocateForInsert/RelocateForDelete consolidation,
    // DeleteBackwardAtPoint/DeleteForwardAtPoint adjusted Point_/Mark_ but
    // never touched NarrowedRange_ at all -- a real latent gap, since a
    // single-grapheme delete right at a narrowed boundary should shrink it
    // exactly like a larger DeleteRange spanning the same boundary already
    // correctly does (see the DeleteRange/"shifts it the same way
    // Point_/Mark_ already do" case above).
    Buffer buffer("scratch", ned::text::Rope("line0\nline1\nline2\nline3"));
    buffer.NarrowToRegion(6, 12); // snaps to lines 1-2: [6, 18)
    REQUIRE(buffer.NarrowedRange() == std::pair<std::size_t, std::size_t>{6, 18});

    buffer.SetPoint(7);             // just past the narrowed start
    buffer.DeleteBackwardAtPoint(); // deletes byte 6, the narrowed start itself
    REQUIRE(buffer.NarrowedRange() == std::pair<std::size_t, std::size_t>{6, 17});

    buffer.SetPoint(16);           // just before the narrowed end
    buffer.DeleteForwardAtPoint(); // deletes byte 16
    REQUIRE(buffer.NarrowedRange() == std::pair<std::size_t, std::size_t>{6, 16});
}

TEST_CASE("SetFoldMarker/FoldMarkerAt/FoldMarkers round-trip and erase via nullopt", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("* a\n* b\n"));
    REQUIRE(buffer.FoldMarkers().empty());
    REQUIRE_FALSE(buffer.FoldMarkerAt(0).has_value());

    const std::size_t generationBefore = buffer.FoldGeneration();
    buffer.SetFoldMarker(0, Buffer::FoldMarker::Collapsed);
    REQUIRE(buffer.FoldGeneration() > generationBefore);
    REQUIRE(buffer.FoldMarkerAt(0) == Buffer::FoldMarker::Collapsed);
    REQUIRE(buffer.FoldMarkers().size() == 1);

    buffer.SetFoldMarker(4, Buffer::FoldMarker::ChildrenVisible);
    REQUIRE(buffer.FoldMarkers().size() == 2);

    buffer.SetFoldMarker(0, std::nullopt);
    REQUIRE_FALSE(buffer.FoldMarkerAt(0).has_value());
    REQUIRE(buffer.FoldMarkers().size() == 1);
}

TEST_CASE("SetDiagnostics/Diagnostics/DiagnosticsGeneration round-trip and replace wholesale", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("int x = 1;\nint y = 2;\n"));
    REQUIRE(buffer.Diagnostics().empty());

    const std::size_t generationBefore = buffer.DiagnosticsGeneration();
    buffer.SetDiagnostics({
        Buffer::Diagnostic{.startByte = 4, .endByte = 5, .severity = Buffer::Diagnostic::Severity::Warning, .message = "unused variable x"},
    });
    REQUIRE(buffer.DiagnosticsGeneration() > generationBefore);
    REQUIRE(buffer.Diagnostics().size() == 1);
    REQUIRE(buffer.Diagnostics()[0].message == "unused variable x");
    REQUIRE(buffer.Diagnostics()[0].severity == Buffer::Diagnostic::Severity::Warning);

    // A second SetDiagnostics call replaces the set wholesale, not merges
    // with the first -- matches LSP's own publishDiagnostics semantics
    // ("here is the full current set"), see Diagnostic's own doc comment.
    const std::size_t generationAfterFirst = buffer.DiagnosticsGeneration();
    buffer.SetDiagnostics({
        Buffer::Diagnostic{.startByte = 15, .endByte = 16, .severity = Buffer::Diagnostic::Severity::Error, .message = "unused variable y"},
    });
    REQUIRE(buffer.DiagnosticsGeneration() > generationAfterFirst);
    REQUIRE(buffer.Diagnostics().size() == 1);
    REQUIRE(buffer.Diagnostics()[0].message == "unused variable y");

    buffer.SetDiagnostics({});
    REQUIRE(buffer.Diagnostics().empty());
}

TEST_CASE("Fold markers relocate across inserts and deletes the same way Mark_ does", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("* a\n* b\n* c\n"));
    buffer.SetFoldMarker(4, Buffer::FoldMarker::Collapsed); // "* b"'s own line start

    buffer.InsertAt(0, "XX");
    REQUIRE(buffer.FoldMarkerAt(6) == Buffer::FoldMarker::Collapsed);
    REQUIRE_FALSE(buffer.FoldMarkerAt(4).has_value());

    buffer.DeleteRange(0, 2); // remove the just-inserted "XX", back to the original offsets
    REQUIRE(buffer.FoldMarkerAt(4) == Buffer::FoldMarker::Collapsed);

    // A delete that spans across the marker's own offset collapses it to
    // the delete's start, same as Mark_'s own precedent.
    buffer.DeleteRange(2, 4); // deletes bytes [2, 6), which includes offset 4
    REQUIRE(buffer.FoldMarkerAt(2) == Buffer::FoldMarker::Collapsed);
}

TEST_CASE("A fresh buffer has no unsaved changes; an insert marks a range", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("hello world"));
    REQUIRE(buffer.UnsavedChangeRanges().empty());

    const std::size_t generationBefore = buffer.UnsavedChangeGeneration();
    buffer.InsertAt(5, "XXX");
    REQUIRE(buffer.UnsavedChangeGeneration() > generationBefore);
    REQUIRE(buffer.UnsavedChangeRanges() == std::vector<std::pair<std::size_t, std::size_t>>{{5, 8}});
}

TEST_CASE("Consecutive nearby inserts merge into one unsaved-change range", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("hello world"));
    buffer.InsertAt(5, "A");
    buffer.InsertAt(6, "B"); // right after the first insert -- adjacent, should merge
    REQUIRE(buffer.UnsavedChangeRanges() == std::vector<std::pair<std::size_t, std::size_t>>{{5, 7}});
}

TEST_CASE("A delete marks the collapse point, not an empty range", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("hello world"));
    buffer.DeleteRange(0, 5); // "hello" -- collapses to offset 0
    REQUIRE(buffer.UnsavedChangeRanges() == std::vector<std::pair<std::size_t, std::size_t>>{{0, 1}});
}

TEST_CASE("Unsaved-change ranges relocate across an edit before the tracked range", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("hello world"));
    buffer.InsertAt(6, "X"); // marks [6, 7)
    REQUIRE(buffer.UnsavedChangeRanges() == std::vector<std::pair<std::size_t, std::size_t>>{{6, 7}});

    buffer.InsertAt(0, "AB"); // shifts everything after it forward by 2
    REQUIRE(buffer.UnsavedChangeRanges() == std::vector<std::pair<std::size_t, std::size_t>>{{0, 2}, {8, 9}});
}

TEST_CASE("SaveToFile clears unsaved-change ranges on success", "[Buffer]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-unsaved-change-test.txt";
    Buffer                      buffer("scratch", ned::text::Rope("hello"));
    buffer.InsertAt(5, " world");
    REQUIRE_FALSE(buffer.UnsavedChangeRanges().empty());

    buffer.SaveToFile(path, /*ensureFinalNewline=*/false);
    REQUIRE(buffer.UnsavedChangeRanges().empty());

    std::filesystem::remove(path);
}

TEST_CASE("Undo/Redo mark only the byte range that actually differs, not the whole buffer", "[Buffer]") {
    // Was "conservatively mark the whole buffer" -- that turned out to be a
    // real, user-visible bug (reported after live terminal testing:
    // undoing one trivial edit lit up the entire gutter as changed), not
    // just an approximation worth tightening later. Undo/Redo now diff the
    // pre- and post- Rope snapshots (a common-prefix/suffix scan) and feed
    // the result through the same MarkUnsavedRangeDeleted/Inserted
    // machinery every real edit already uses. Two separate edits after the
    // save (rather than one) so undoing just the second one still leaves a
    // real, non-empty diff from the saved content to assert on -- see the
    // "back to exactly the saved content" test below for the all-the-way
    // case.
    Buffer buffer("scratch", ned::text::Rope("hello"));
    buffer.InsertAt(5, " world");
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-unsaved-change-undo-test.txt";
    buffer.SaveToFile(path, /*ensureFinalNewline=*/false);
    REQUIRE(buffer.UnsavedChangeRanges().empty());

    buffer.InsertAt(0, "Y"); // "Yhello world" -- its own undo step
    buffer.InsertAt(0, "X"); // "XYhello world" -- a separate undo step

    buffer.Undo(); // back to "Yhello world" -- differs from saved by one byte, not the whole buffer
    REQUIRE(buffer.UnsavedChangeRanges() == std::vector<std::pair<std::size_t, std::size_t>>{{0, 1}});

    // Back to "XYhello world" -- both byte 0 ('X', newly reinserted) and
    // byte 1 ('Y', already touched before this Redo, correctly relocated
    // rather than dropped) differ from saved, not the whole buffer.
    buffer.Redo();
    REQUIRE(buffer.UnsavedChangeRanges() == std::vector<std::pair<std::size_t, std::size_t>>{{0, 2}});

    std::filesystem::remove(path);
}

TEST_CASE("Undoing an edit that leaves content identical to before reports no unsaved change", "[Buffer]") {
    Buffer                      buffer("scratch", ned::text::Rope("hello world"));
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned-unsaved-change-undo-noop-test.txt";
    buffer.SaveToFile(path, /*ensureFinalNewline=*/false);
    REQUIRE(buffer.UnsavedChangeRanges().empty());

    buffer.SetPoint(buffer.Content().ByteLength());
    buffer.InsertAtPoint(" there"); // append at the very end
    REQUIRE_FALSE(buffer.UnsavedChangeRanges().empty());

    buffer.Undo(); // back to exactly the saved content
    REQUIRE(buffer.UnsavedChangeRanges().empty());

    std::filesystem::remove(path);
}

TEST_CASE("Undo restoring shorter content drops a fold marker past the new end", "[Buffer]") {
    Buffer buffer("scratch");
    buffer.InsertAtPoint("* a\n* b\n"); // one real undo step
    REQUIRE(buffer.CanUndo());

    buffer.SetFoldMarker(4, Buffer::FoldMarker::Collapsed); // "* b"'s own line start
    REQUIRE(buffer.FoldMarkerAt(4).has_value());

    buffer.Undo(); // back to the empty buffer -- offset 4 no longer exists

    REQUIRE(buffer.Text().empty());
    REQUIRE(buffer.FoldMarkers().empty());
}

TEST_CASE("A fresh buffer is not read-only by default", "[Buffer]") {
    Buffer buffer("scratch");
    REQUIRE_FALSE(buffer.ReadOnly());
}

TEST_CASE("Every content-mutating method throws once the buffer is read-only", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("hello"));
    buffer.SetReadOnly(true);
    REQUIRE(buffer.ReadOnly());

    REQUIRE_THROWS_AS(buffer.InsertAtPoint("x"), std::runtime_error);
    REQUIRE_THROWS_AS(buffer.InsertAt(0, "x"), std::runtime_error);
    REQUIRE_THROWS_AS(buffer.DeleteRange(0, 1), std::runtime_error);
    buffer.SetPoint(1);
    REQUIRE_THROWS_AS(buffer.DeleteBackwardAtPoint(), std::runtime_error);
    REQUIRE_THROWS_AS(buffer.DeleteForwardAtPoint(), std::runtime_error);

    // None of the attempted edits above actually changed anything.
    REQUIRE(buffer.Text() == "hello");
}

TEST_CASE("Toggling read-only back off re-allows edits", "[Buffer]") {
    Buffer buffer("scratch");
    buffer.SetReadOnly(true);
    REQUIRE_THROWS_AS(buffer.InsertAtPoint("x"), std::runtime_error);

    buffer.SetReadOnly(false);
    buffer.InsertAtPoint("x");
    REQUIRE(buffer.Text() == "x");
}

TEST_CASE("AppendWhileReadOnly throws std::logic_error on a writable buffer", "[Buffer]") {
    Buffer buffer("scratch");
    REQUIRE_FALSE(buffer.ReadOnly());
    REQUIRE_THROWS_AS(buffer.AppendWhileReadOnly("x"), std::logic_error);
}

TEST_CASE("AppendWhileReadOnly appends at the end regardless of Point", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("hello"));
    buffer.SetReadOnly(true);
    buffer.SetPoint(2); // not at the end -- must be left untouched by the append below

    buffer.AppendWhileReadOnly(" world");

    REQUIRE(buffer.Text() == "hello world");
    REQUIRE(buffer.Point() == 2); // Point was NOT at the end -- untouched, per InsertAt's own RelocateForInsert rule
}

TEST_CASE("AppendWhileReadOnly tail-follows when Point was already at the buffer's end", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("hello"));
    buffer.SetReadOnly(true);
    buffer.SetPoint(5); // exactly at the end

    buffer.AppendWhileReadOnly(" world");

    REQUIRE(buffer.Text() == "hello world");
    REQUIRE(buffer.Point() == 11); // Point WAS at the end -- moves forward with the appended text, tail -f style
}

TEST_CASE("Two AppendWhileReadOnly calls both land at the end, in order", "[Buffer]") {
    Buffer buffer("scratch");
    buffer.SetReadOnly(true);

    buffer.AppendWhileReadOnly("first\n");
    buffer.AppendWhileReadOnly("second\n");

    REQUIRE(buffer.Text() == "first\nsecond\n");
}

TEST_CASE("A fresh buffer is not loading by default", "[Buffer]") {
    Buffer buffer("scratch");
    REQUIRE_FALSE(buffer.IsLoading());
    REQUIRE_FALSE(buffer.ReadOnly());
}

TEST_CASE("MarkLoading makes IsLoading and ReadOnly both true", "[Buffer]") {
    Buffer buffer("scratch");
    buffer.MarkLoading();

    REQUIRE(buffer.IsLoading());
    REQUIRE(buffer.ReadOnly()); // every mutator must refuse edits while a load is still in flight

    REQUIRE_THROWS_AS(buffer.InsertAtPoint("x"), std::runtime_error);
}

TEST_CASE("ReplaceContentForLoad swaps content and bumps ContentGeneration without touching undo/modified state",
          "[Buffer]") {
    Buffer buffer("scratch");
    buffer.MarkLoading();
    const std::size_t generationBefore = buffer.ContentGeneration();

    buffer.ReplaceContentForLoad(ned::text::Rope("partial content"));

    REQUIRE(buffer.Text() == "partial content");
    REQUIRE(buffer.ContentGeneration() > generationBefore);
    REQUIRE(buffer.IsLoading());      // ReplaceContentForLoad is a preview swap, not the terminal call
    REQUIRE_FALSE(buffer.Modified()); // not a real edit -- must not mark the buffer dirty
}

TEST_CASE("FinishLoad clears IsLoading, restores editability, and leaves the buffer unmodified", "[Buffer]") {
    Buffer buffer("scratch");
    buffer.MarkLoading();
    buffer.ReplaceContentForLoad(ned::text::Rope("first preview"));

    buffer.FinishLoad(ned::text::Rope("final content"));

    REQUIRE(buffer.Text() == "final content");
    REQUIRE_FALSE(buffer.IsLoading());
    REQUIRE_FALSE(buffer.ReadOnly());
    REQUIRE_FALSE(buffer.Modified());

    buffer.InsertAtPoint("x"); // must not throw -- editability is genuinely restored
    REQUIRE(buffer.Modified());
}

TEST_CASE("FinishLoad starts undo history clean at the loaded content, not at an earlier preview", "[Buffer]") {
    Buffer buffer("scratch");
    buffer.MarkLoading();
    buffer.ReplaceContentForLoad(ned::text::Rope("preview"));
    buffer.FinishLoad(ned::text::Rope("final"));

    buffer.SetPoint(buffer.Size());
    buffer.InsertAtPoint("!");
    REQUIRE(buffer.Text() == "final!");
    buffer.Undo();
    REQUIRE(buffer.Text() == "final"); // undoing the one real edit lands exactly on FinishLoad's content
}

TEST_CASE("RestoreContent replaces content in one undoable step and leaves the buffer Modified", "[Buffer]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_test_restore.txt";
    std::ofstream(path) << "on disk\n";

    Buffer buffer = Buffer::FromFile(path);
    REQUIRE_FALSE(buffer.Modified());

    buffer.RestoreContent("recovered snapshot\n");

    REQUIRE(buffer.Text() == "recovered snapshot\n");
    // Unlike Revert(), the buffer does not match disk -- the restore only
    // becomes permanent via an explicit save.
    REQUIRE(buffer.Modified());
    // ... and the disk timestamp is untouched, so the file doesn't read as
    // externally modified either.
    REQUIRE_FALSE(buffer.ExternallyModified());

    // Exactly one undo step returns to the pre-restore content -- and lands
    // back on the saved snapshot, so Modified() clears again.
    buffer.Undo();
    REQUIRE(buffer.Text() == "on disk\n");
    REQUIRE_FALSE(buffer.Modified());

    std::filesystem::remove(path);
}

TEST_CASE("RestoreContent clamps point and clears mark, secondary cursors, narrowing, and folds", "[Buffer]") {
    Buffer buffer("scratch", ned::text::Rope("a long line of original content"));
    buffer.SetPoint(buffer.Size());
    buffer.SetMark(3);
    buffer.AddCursorAt(5);
    buffer.NarrowToRegion(2, 10);
    buffer.SetFoldMarker(4, Buffer::FoldMarker::Collapsed);

    buffer.RestoreContent("tiny");

    REQUIRE(buffer.Point() <= buffer.Size());
    REQUIRE_FALSE(buffer.HasMark());
    REQUIRE_FALSE(buffer.HasSecondaryCursors());
    REQUIRE_FALSE(buffer.IsNarrowed());
    REQUIRE(buffer.FoldMarkers().empty());
}

TEST_CASE("RestoreContent to empty over a nonempty saved snapshot still reads Modified", "[Buffer]") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ned_buffer_test_restore_empty.txt";
    std::ofstream(path) << "not empty\n";

    Buffer buffer = Buffer::FromFile(path);
    buffer.RestoreContent("");

    REQUIRE(buffer.Text().empty());
    REQUIRE(buffer.Modified()); // Modified()'s zero-length fallback covers what no byte range can represent

    std::filesystem::remove(path);
}
