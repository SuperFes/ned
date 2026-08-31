#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/IncrementalSearch.h"
#include "Text/Buffer.h"

using ned::editor::IncrementalSearch;
using ned::text::Buffer;
using ned::text::Rope;

namespace {
// t0 h1 e2 _3 q4 u5 i6 c7 k8 _9 b10 r11 o12 w13 n14 _15 f16 o17 x18 _19 j20 ...
const char* kText = "the quick brown fox jumps over the lazy dog";

// huge-file-search-and-save follow-up: same "each test file duplicates its
// own tiny helper" precedent BufferHugeFileTest.cpp already establishes --
// builds a real on-disk file so Buffer::FromHugeFile has something to open
// (it forces IsHuge() regardless of actual file size, same as that file's
// own first test relies on).
std::filesystem::path WriteTempFile(const std::string& name, std::string_view content) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream                file(path, std::ios::binary);
    file << content;
    return path;
}
}

TEST_CASE("Forward search finds a substring and leaves point after the match", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope(kText));
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendChar(U'f');
    search.AppendChar(U'o');
    search.AppendChar(U'x');

    REQUIRE(search.Found());
    REQUIRE(search.Query() == "fox");
    REQUIRE(buffer.Point() == 19); // right after "fox" (starts at 16)
}

TEST_CASE("RepeatSearch advances to the next occurrence", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope(kText));
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendChar(U'o');
    REQUIRE(buffer.Point() == 13); // "brown"'s o, at index 12

    search.RepeatSearch();
    REQUIRE(buffer.Point() == 18); // "fox"'s o, at index 17

    search.RepeatSearch();
    REQUIRE(buffer.Point() == 27); // "over"'s o, at index 26
}

TEST_CASE("Backward search leaves point at the start of the match", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope(kText));
    buffer.SetPoint(buffer.Size()); // start searching from the end

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Backward);
    search.AppendChar(U'o');
    search.AppendChar(U'g'); // "og" only appears in "dog"

    REQUIRE(search.Found());
    const std::size_t dogStart = std::string(kText).rfind("dog");
    REQUIRE(buffer.Point() == dogStart + 1); // "og" starts one past "d"
}

TEST_CASE("RepeatSearch wraps around to the top of the document when it runs off the end", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope(kText));
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendChar(U'o');
    REQUIRE(buffer.Point() == 13); // "brown"'s o
    search.RepeatSearch();
    REQUIRE(buffer.Point() == 18); // "fox"'s o
    search.RepeatSearch();
    REQUIRE(buffer.Point() == 27); // "over"'s o
    search.RepeatSearch();
    REQUIRE(buffer.Point() == 42); // "dog"'s o, the last one in the document
    search.RepeatSearch();
    REQUIRE(search.Found());
    REQUIRE(buffer.Point() == 13); // ran off the end -- wrapped back to "brown"'s o
}

TEST_CASE("RepeatSearch (backward) wraps around to the bottom of the document when it runs off the start", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope(kText));
    buffer.SetPoint(0); // start searching from the very top

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Backward);
    search.AppendChar(U'o');
    REQUIRE(search.Found());
    const std::size_t dogO = std::string(kText).rfind('o');
    REQUIRE(buffer.Point() == dogO); // wrapped immediately: nothing before point 0
}

TEST_CASE("Search is case-insensitive when the query has no uppercase letter", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope("the quick brown FOX jumps over the lazy dog"));
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendChar(U'f');
    search.AppendChar(U'o');
    search.AppendChar(U'x');

    REQUIRE(search.Found());
    REQUIRE(buffer.Point() == 19); // matched uppercase "FOX" despite the all-lowercase query
}

TEST_CASE("Search becomes case-sensitive once the query contains an uppercase letter", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope("The Fox and the fox"));
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendChar(U'F');
    search.AppendChar(U'o');
    search.AppendChar(U'x');

    REQUIRE(search.Found());
    REQUIRE(buffer.Point() == 7); // "Fox" (capital F), not "fox" later in the string
}

TEST_CASE("ReverseDirection flips a forward search to backward and re-searches", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope(kText));
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendChar(U'o');
    REQUIRE(buffer.Point() == 13); // "brown"'s o, found searching forward from 0

    search.ReverseDirection();
    REQUIRE(search.Found());
    REQUIRE(search.StatusText() == "Backward I-search: o");
    REQUIRE(buffer.Point() <= 13); // re-searched backward from the current point, didn't move forward
}

TEST_CASE("ReverseDirection on an empty query is a no-op", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope(kText));
    buffer.SetPoint(5);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.ReverseDirection();

    REQUIRE(search.StatusText() == "Backward I-search: ");
    REQUIRE(buffer.Point() == 5); // untouched -- nothing to search for
}

TEST_CASE("AppendText appends arbitrary text to the query and re-searches", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope(kText));
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendText("fox");

    REQUIRE(search.Query() == "fox");
    REQUIRE(search.Found());
    REQUIRE(buffer.Point() == 19);
}

TEST_CASE("AppendWordAtPoint pulls the word at the current point into the query", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope(kText));
    buffer.SetPoint(16); // start of "fox"

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendWordAtPoint();

    REQUIRE(search.Query() == "fox");
}

TEST_CASE("A query with no match reports Found() == false and leaves point unmoved", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope(kText));
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    // '9' doesn't appear anywhere in kText, so this never matches even as a
    // single character (unlike e.g. 'z', which alone matches inside "lazy").
    search.AppendChar(U'9');
    search.AppendChar(U'9');
    search.AppendChar(U'9');

    REQUIRE_FALSE(search.Found());
    REQUIRE(buffer.Point() == 0); // never found anything to move to
}

TEST_CASE("DeleteChar shortens the query and re-searches, recovering from a failing search", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope(kText));
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendChar(U'f');
    search.AppendChar(U'o');
    search.AppendChar(U'y'); // "foy" doesn't exist
    REQUIRE_FALSE(search.Found());

    search.DeleteChar();
    REQUIRE(search.Query() == "fo");
    REQUIRE(search.Found());
    REQUIRE(buffer.Point() == 18); // "fo" matched inside "fox", ends at 16+2
}

TEST_CASE("DeleteChar removes one whole codepoint even for multi-byte UTF-8", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope(std::string("caf\xC3\xA9 bar"))); // "café bar"
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendChar(U'\x00E9'); // 'é', 2 bytes
    REQUIRE(search.Query() == "\xC3\xA9");

    search.DeleteChar();
    REQUIRE(search.Query().empty()); // removed the whole 2-byte codepoint, not just one byte
}

TEST_CASE("Cancel restores the original point", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope(kText));
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendChar(U'f');
    search.AppendChar(U'o');
    search.AppendChar(U'x');
    REQUIRE(buffer.Point() != 0);

    search.Cancel();
    REQUIRE(buffer.Point() == 0);
}

TEST_CASE("Accept leaves point at the current match", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope(kText));
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendChar(U'f');
    search.AppendChar(U'o');
    search.AppendChar(U'x');

    search.Accept();
    REQUIRE(buffer.Point() == 19);
}

TEST_CASE("StatusText reflects direction and failing state", "[IncrementalSearch]") {
    Buffer buffer("scratch", Rope(kText));
    buffer.SetPoint(0);

    IncrementalSearch forward(buffer, IncrementalSearch::Direction::Forward);
    forward.AppendChar(U'f');
    REQUIRE(forward.StatusText() == "I-search: f");

    IncrementalSearch backward(buffer, IncrementalSearch::Direction::Backward);
    backward.AppendChar(U'f');
    REQUIRE(backward.StatusText() == "Backward I-search: f");

    IncrementalSearch failing(buffer, IncrementalSearch::Direction::Forward);
    failing.AppendChar(U'z');
    failing.AppendChar(U'z');
    REQUIRE(failing.StatusText() == "Failing I-search: zz");
}

// huge-file-search-and-save follow-up: SearchHuge, the windowed branch of
// Search()/AppendWordAtPoint() taken for a huge (ITextStorage::IsHuge())
// buffer -- mirrors the in-memory tests above (same expected offsets) to
// prove the two paths agree, plus two tests specific to the windowing
// itself (a match past the first window, and one straddling a window
// boundary) that the in-memory path has no equivalent for.

TEST_CASE("Forward isearch on a huge buffer finds a match and leaves point after it", "[IncrementalSearch][HugeFile]") {
    const std::filesystem::path path = WriteTempFile("ned_isearch_huge_forward.txt", kText);
    Buffer                      buffer = Buffer::FromHugeFile(path);
    REQUIRE(buffer.Content().IsHuge());
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendChar(U'f');
    search.AppendChar(U'o');
    search.AppendChar(U'x');

    REQUIRE(search.Found());
    REQUIRE(buffer.Point() == 19);

    std::filesystem::remove(path);
}

TEST_CASE("Backward isearch on a huge buffer leaves point at the start of the match", "[IncrementalSearch][HugeFile]") {
    const std::filesystem::path path = WriteTempFile("ned_isearch_huge_backward.txt", kText);
    Buffer                      buffer = Buffer::FromHugeFile(path);
    buffer.SetPoint(buffer.Size());

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Backward);
    search.AppendChar(U'o');
    search.AppendChar(U'g');

    REQUIRE(search.Found());
    const std::size_t dogStart = std::string(kText).rfind("dog");
    REQUIRE(buffer.Point() == dogStart + 1);

    std::filesystem::remove(path);
}

TEST_CASE("isearch on a huge buffer is smart-case, same as the in-memory path", "[IncrementalSearch][HugeFile]") {
    const std::filesystem::path path = WriteTempFile("ned_isearch_huge_case.txt", "the quick brown FOX jumps over the lazy dog");
    Buffer                      buffer = Buffer::FromHugeFile(path);
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendChar(U'f');
    search.AppendChar(U'o');
    search.AppendChar(U'x');

    REQUIRE(search.Found());
    REQUIRE(buffer.Point() == 19); // matched uppercase "FOX" despite the all-lowercase query

    std::filesystem::remove(path);
}

TEST_CASE("A query with no match in a huge buffer reports Found() == false and leaves point unmoved",
          "[IncrementalSearch][HugeFile]") {
    const std::filesystem::path path = WriteTempFile("ned_isearch_huge_nomatch.txt", kText);
    Buffer                      buffer = Buffer::FromHugeFile(path);
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendChar(U'9');
    search.AppendChar(U'9');
    search.AppendChar(U'9');

    REQUIRE_FALSE(search.Found());
    REQUIRE(buffer.Point() == 0);

    std::filesystem::remove(path);
}

TEST_CASE("RepeatSearch wraps around to the top of a huge document when it runs off the end",
          "[IncrementalSearch][HugeFile]") {
    const std::filesystem::path path = WriteTempFile("ned_isearch_huge_wrap_fwd.txt", kText);
    Buffer                      buffer = Buffer::FromHugeFile(path);
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendChar(U'o');
    REQUIRE(buffer.Point() == 13);
    search.RepeatSearch();
    REQUIRE(buffer.Point() == 18);
    search.RepeatSearch();
    REQUIRE(buffer.Point() == 27);
    search.RepeatSearch();
    REQUIRE(buffer.Point() == 42); // "dog"'s o, the last one
    search.RepeatSearch();
    REQUIRE(search.Found());
    REQUIRE(buffer.Point() == 13); // wrapped back to "brown"'s o

    std::filesystem::remove(path);
}

TEST_CASE("Backward isearch on a huge buffer wraps around to the bottom of the document",
          "[IncrementalSearch][HugeFile]") {
    const std::filesystem::path path = WriteTempFile("ned_isearch_huge_wrap_back.txt", kText);
    Buffer                      buffer = Buffer::FromHugeFile(path);
    buffer.SetPoint(0); // start searching from the very top

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Backward);
    search.AppendChar(U'o');
    REQUIRE(search.Found());
    const std::size_t dogO = std::string(kText).rfind('o');
    REQUIRE(buffer.Point() == dogO); // wrapped immediately: nothing before point 0

    std::filesystem::remove(path);
}

TEST_CASE("AppendWordAtPoint pulls the word at point in a huge buffer", "[IncrementalSearch][HugeFile]") {
    const std::filesystem::path path = WriteTempFile("ned_isearch_huge_word.txt", kText);
    Buffer                      buffer = Buffer::FromHugeFile(path);
    buffer.SetPoint(16); // start of "fox"

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendWordAtPoint();

    REQUIRE(search.Query() == "fox");

    std::filesystem::remove(path);
}

TEST_CASE("Forward isearch on a huge buffer finds a match past the first internal scan window",
          "[IncrementalSearch][HugeFile]") {
    constexpr std::size_t kWindow = 4 * 1024 * 1024; // mirrors SearchHuge's own kWindow constant (IncrementalSearch.cpp)
    const std::string     needle  = "unique-token-past-first-window";
    const std::size_t     needleOffset = kWindow + 1000;

    std::string content(kWindow + 4096, 'a');
    content.replace(needleOffset, needle.size(), needle);
    const std::filesystem::path path = WriteTempFile("ned_isearch_huge_multiwindow.txt", content);

    Buffer buffer = Buffer::FromHugeFile(path);
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendText(needle);

    REQUIRE(search.Found());
    REQUIRE(buffer.Point() == needleOffset + needle.size());

    std::filesystem::remove(path);
}

TEST_CASE("isearch on a huge buffer finds a match straddling an internal scan-window boundary",
          "[IncrementalSearch][HugeFile]") {
    constexpr std::size_t kWindow      = 4 * 1024 * 1024;
    const std::string     needle       = "STRADDLE";
    const std::size_t     needleOffset = kWindow - 4; // 4 bytes before, 4 after the boundary

    std::string content(kWindow + 4096, 'a');
    content.replace(needleOffset, needle.size(), needle);
    const std::filesystem::path path = WriteTempFile("ned_isearch_huge_boundary.txt", content);

    Buffer buffer = Buffer::FromHugeFile(path);
    buffer.SetPoint(0);

    IncrementalSearch search(buffer, IncrementalSearch::Direction::Forward);
    search.AppendText(needle);

    REQUIRE(search.Found());
    REQUIRE(buffer.Point() == needleOffset + needle.size());

    std::filesystem::remove(path);
}
