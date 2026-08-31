#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "Editor/QueryReplace.h"
#include "Editor/RegexPattern.h"
#include "Text/Buffer.h"

using ned::editor::QueryReplace;
using ned::text::Buffer;
using ned::text::Rope;

namespace {
void Type(QueryReplace& qr, std::string_view text) {
    for (const char c : text) {
        qr.AppendChar(static_cast<char32_t>(static_cast<unsigned char>(c)));
    }
}

// huge-file-regex-replace follow-up: same "each test file duplicates its own
// tiny helper" precedent BufferHugeFileTest.cpp/IncrementalSearchTest.cpp
// already establish -- builds a real on-disk file so Buffer::FromHugeFile has
// something to open (it forces IsHuge() regardless of actual file size).
std::filesystem::path WriteTempFile(const std::string& name, std::string_view content) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream               file(path, std::ios::binary);
    file << content;
    return path;
}
} // namespace

TEST_CASE("Full flow: pattern, replacement, confirm each match in turn", "[QueryReplace]") {
    Buffer       buffer("scratch", Rope("cat sat on the cat mat"));
    QueryReplace qr(buffer);

    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::EnteringPattern);
    Type(qr, "cat");
    qr.ConfirmPattern();
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::EnteringReplacement);

    Type(qr, "dog");
    qr.ConfirmReplacement();
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Confirming);

    qr.ReplaceAndNext();
    REQUIRE(buffer.Text() == "dog sat on the cat mat");
    REQUIRE(qr.ReplacementCount() == 1);
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Confirming); // second "cat" still pending

    qr.ReplaceAndNext();
    REQUIRE(buffer.Text() == "dog sat on the dog mat");
    REQUIRE(qr.ReplacementCount() == 2);
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Done);
}

TEST_CASE("SkipAndNext leaves a match untouched and moves to the next", "[QueryReplace]") {
    Buffer       buffer("scratch", Rope("cat sat on the cat mat"));
    QueryReplace qr(buffer);
    Type(qr, "cat");
    qr.ConfirmPattern();
    Type(qr, "dog");
    qr.ConfirmReplacement();

    qr.SkipAndNext();
    REQUIRE(buffer.Text() == "cat sat on the cat mat");
    REQUIRE(qr.ReplacementCount() == 0);

    qr.ReplaceAndNext();
    REQUIRE(buffer.Text() == "cat sat on the dog mat"); // only the second one replaced
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Done);
}

TEST_CASE("ReplaceAll replaces every remaining match", "[QueryReplace]") {
    Buffer       buffer("scratch", Rope("cat sat on the cat mat"));
    QueryReplace qr(buffer);
    Type(qr, "cat");
    qr.ConfirmPattern();
    Type(qr, "dog");
    qr.ConfirmReplacement();

    qr.ReplaceAll();
    REQUIRE(buffer.Text() == "dog sat on the dog mat");
    REQUIRE(qr.ReplacementCount() == 2);
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Done);
}

TEST_CASE("Finish stops the session without touching the pending match", "[QueryReplace]") {
    Buffer       buffer("scratch", Rope("cat sat on the cat mat"));
    QueryReplace qr(buffer);
    Type(qr, "cat");
    qr.ConfirmPattern();
    Type(qr, "dog");
    qr.ConfirmReplacement();

    qr.Finish();
    REQUIRE(buffer.Text() == "cat sat on the cat mat");
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Done);
}

TEST_CASE("ConfirmPattern throws on invalid regex and doesn't advance the stage", "[QueryReplace]") {
    Buffer       buffer("scratch", Rope("text"));
    QueryReplace qr(buffer);
    Type(qr, "(unclosed");

    REQUIRE_THROWS_AS(qr.ConfirmPattern(), ned::editor::RegexPatternError);
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::EnteringPattern);
}

TEST_CASE("Replacement text supports $1/$2 backreferences", "[QueryReplace]") {
    Buffer       buffer("scratch", Rope("alice@example"));
    QueryReplace qr(buffer);
    Type(qr, "(\\w+)@(\\w+)");
    qr.ConfirmPattern();
    Type(qr, "$2@$1");
    qr.ConfirmReplacement();

    qr.ReplaceAndNext();
    REQUIRE(buffer.Text() == "example@alice");
}

TEST_CASE("ConfirmReplacement goes straight to Done when there are no matches", "[QueryReplace]") {
    Buffer       buffer("scratch", Rope("hello world"));
    QueryReplace qr(buffer);
    Type(qr, "zzz");
    qr.ConfirmPattern();
    Type(qr, "xyz");
    qr.ConfirmReplacement();

    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Done);
    REQUIRE(qr.ReplacementCount() == 0);
}

TEST_CASE("Cancel ends the session without undoing prior replacements", "[QueryReplace]") {
    Buffer       buffer("scratch", Rope("cat sat on the cat mat"));
    QueryReplace qr(buffer);
    Type(qr, "cat");
    qr.ConfirmPattern();
    Type(qr, "dog");
    qr.ConfirmReplacement();

    qr.ReplaceAndNext();
    REQUIRE(buffer.Text() == "dog sat on the cat mat");

    qr.Cancel();
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Done);
    REQUIRE(buffer.Text() == "dog sat on the cat mat"); // first replacement stands
}

TEST_CASE("ReplaceAll makes forward progress against a zero-width match with an empty replacement", "[QueryReplace]") {
    Buffer       buffer("scratch", Rope("bbb"));
    QueryReplace qr(buffer);
    Type(qr, "a*"); // matches the empty string at every position in "bbb"
    qr.ConfirmPattern();
    qr.ConfirmReplacement(); // empty replacement text

    qr.ReplaceAll(); // must terminate, not hang
    REQUIRE(buffer.Text() == "bbb");
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Done);
}

TEST_CASE("^ anchors at real line starts, including for matches after the first", "[QueryReplace]") {
    // in-file-regex follow-up: the old std::regex engine searched trimmed
    // subranges, so ^ was subject-start-only and blind to line structure --
    // a documented limitation, now real multiline behavior.
    Buffer       buffer("scratch", Rope("cat\ncatnip\ncat\n"));
    QueryReplace qr(buffer);
    Type(qr, "^cat$");
    qr.ConfirmPattern();
    Type(qr, "dog");
    qr.ConfirmReplacement();

    qr.ReplaceAll();
    REQUIRE(buffer.Text() == "dog\ncatnip\ndog\n");
    REQUIRE(qr.ReplacementCount() == 2);
}

TEST_CASE("Lookbehind patterns work and see context before the search cursor", "[QueryReplace]") {
    Buffer       buffer("scratch", Rope("blue sky, gray sky"));
    QueryReplace qr(buffer);
    Type(qr, "(?<=blue )sky");
    qr.ConfirmPattern();
    Type(qr, "sea");
    qr.ConfirmReplacement();

    qr.ReplaceAll();
    REQUIRE(buffer.Text() == "blue sea, gray sky");
}

TEST_CASE("Named groups can be referenced in the replacement", "[QueryReplace]") {
    Buffer       buffer("scratch", Rope("alice@example"));
    QueryReplace qr(buffer);
    Type(qr, "(?<user>\\w+)@(?<host>\\w+)");
    qr.ConfirmPattern();
    Type(qr, "${host}@${user}");
    qr.ConfirmReplacement();

    qr.ReplaceAndNext();
    REQUIRE(buffer.Text() == "example@alice");
}

TEST_CASE("ReplaceAll terminates on zero-width matches over multibyte content", "[QueryReplace]") {
    Buffer       buffer("scratch", Rope("héé"));
    QueryReplace qr(buffer);
    Type(qr, "x*"); // empty match at every codepoint position
    qr.ConfirmPattern();
    qr.ConfirmReplacement(); // empty replacement

    qr.ReplaceAll(); // must terminate and never split an é
    REQUIRE(buffer.Text() == "héé");
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Done);
}

TEST_CASE("DeleteChar edits whichever string is currently being entered", "[QueryReplace]") {
    Buffer       buffer("scratch", Rope("cat"));
    QueryReplace qr(buffer);
    Type(qr, "cats");
    qr.DeleteChar();
    qr.ConfirmPattern(); // pattern should be "cat", not "cats"

    Type(qr, "dogg");
    qr.DeleteChar();
    qr.ConfirmReplacement(); // replacement should be "dog", not "dogg"

    qr.ReplaceAndNext();
    REQUIRE(buffer.Text() == "dog");
}

// binary-safety-guardrails follow-up: ConfirmReplacement refuses outright
// for a buffer whose BinarySafeguardsActive() is true, same posture as
// save-buffer's format-on-save/line-ending guards (see CommandsTest.cpp's
// own [BinarySafety]-tagged tests for that precedent).

TEST_CASE("ConfirmReplacement refuses a LikelyBinary buffer without touching its content",
          "[QueryReplace][BinarySafety]") {
    Buffer buffer("scratch", Rope("cat sat on the cat mat"));
    buffer.SetLikelyBinary(true);

    QueryReplace qr(buffer);
    Type(qr, "cat");
    qr.ConfirmPattern(); // pattern entry itself is unaffected -- read-only
    Type(qr, "dog");
    qr.ConfirmReplacement();

    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Done);
    REQUIRE(qr.ReplacementCount() == 0);
    REQUIRE(buffer.Text() == "cat sat on the cat mat"); // untouched
    REQUIRE(qr.StatusText().find("binary content") != std::string::npos);
    REQUIRE(qr.StatusText().find("toggle-binary-safeguards") != std::string::npos);
}

TEST_CASE("toggle-binary-safeguards' override lets ConfirmReplacement proceed normally",
          "[QueryReplace][BinarySafety]") {
    Buffer buffer("scratch", Rope("cat sat on the cat mat"));
    buffer.SetLikelyBinary(true);
    buffer.SetBinarySafetyOverride(true);

    QueryReplace qr(buffer);
    Type(qr, "cat");
    qr.ConfirmPattern();
    Type(qr, "dog");
    qr.ConfirmReplacement();

    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Confirming);
    qr.ReplaceAll();
    REQUIRE(buffer.Text() == "dog sat on the dog mat");
    REQUIRE(qr.ReplacementCount() == 2);
}

// huge-file-regex-replace follow-up: FindNextMatchHuge, the windowed branch
// taken for a huge (ITextStorage::IsHuge()) buffer -- mirrors several of the
// in-memory tests above (same expected outcomes) to prove the two paths
// agree, plus tests specific to the windowing itself that the in-memory path
// has no equivalent for.

TEST_CASE("Full query-replace flow works the same on a huge buffer as in-memory", "[QueryReplace][HugeFile]") {
    const std::filesystem::path path   = WriteTempFile("ned_queryreplace_huge_flow.txt", "cat sat on the cat mat");
    Buffer                      buffer = Buffer::FromHugeFile(path);
    REQUIRE(buffer.Content().IsHuge());

    QueryReplace qr(buffer);
    Type(qr, "cat");
    qr.ConfirmPattern();
    Type(qr, "dog");
    qr.ConfirmReplacement();

    qr.ReplaceAndNext();
    REQUIRE(buffer.Text() == "dog sat on the cat mat");
    REQUIRE(qr.ReplacementCount() == 1);
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Confirming);

    qr.ReplaceAndNext();
    REQUIRE(buffer.Text() == "dog sat on the dog mat");
    REQUIRE(qr.ReplacementCount() == 2);
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Done);

    std::filesystem::remove(path);
}

TEST_CASE("ReplaceAll replaces every match on a huge buffer", "[QueryReplace][HugeFile]") {
    const std::filesystem::path path   = WriteTempFile("ned_queryreplace_huge_replaceall.txt", "cat sat on the cat mat");
    Buffer                      buffer = Buffer::FromHugeFile(path);

    QueryReplace qr(buffer);
    Type(qr, "cat");
    qr.ConfirmPattern();
    Type(qr, "dog");
    qr.ConfirmReplacement();

    qr.ReplaceAll();
    REQUIRE(buffer.Text() == "dog sat on the dog mat");
    REQUIRE(qr.ReplacementCount() == 2);
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Done);

    std::filesystem::remove(path);
}

TEST_CASE("ReplaceAll makes forward progress on a huge buffer against a zero-width match with an empty replacement",
          "[QueryReplace][HugeFile]") {
    const std::filesystem::path path   = WriteTempFile("ned_queryreplace_huge_zerowidth.txt", "bbb");
    Buffer                      buffer = Buffer::FromHugeFile(path);

    QueryReplace qr(buffer);
    Type(qr, "a*"); // matches the empty string at every position in "bbb"
    qr.ConfirmPattern();
    qr.ConfirmReplacement(); // empty replacement text

    qr.ReplaceAll(); // must terminate, not hang
    REQUIRE(buffer.Text() == "bbb");
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Done);

    std::filesystem::remove(path);
}

TEST_CASE("^ anchors at real line starts on a huge buffer", "[QueryReplace][HugeFile]") {
    const std::filesystem::path path   = WriteTempFile("ned_queryreplace_huge_anchor.txt", "cat\ncatnip\ncat\n");
    Buffer                      buffer = Buffer::FromHugeFile(path);

    QueryReplace qr(buffer);
    Type(qr, "^cat$");
    qr.ConfirmPattern();
    Type(qr, "dog");
    qr.ConfirmReplacement();

    qr.ReplaceAll();
    REQUIRE(buffer.Text() == "dog\ncatnip\ndog\n");
    REQUIRE(qr.ReplacementCount() == 2);

    std::filesystem::remove(path);
}

TEST_CASE("query-replace-regexp on a huge buffer finds and replaces a match past the first internal scan window",
          "[QueryReplace][HugeFile]") {
    constexpr std::size_t kWindowBody  = 4 * 1024 * 1024; // mirrors QueryReplace.cpp's own constant
    const std::string     needle       = "alice@example";
    const std::size_t     needleOffset = kWindowBody + 1000;

    // '.' filler (not a word character) keeps \w+ from merging into the
    // surrounding padding -- unlike IncrementalSearchTest's plain literal
    // search, a regex word-class match needs a real boundary either side.
    std::string content(kWindowBody + 4096, '.');
    content.replace(needleOffset, needle.size(), needle);
    const std::filesystem::path path = WriteTempFile("ned_queryreplace_huge_multiwindow.txt", content);

    Buffer buffer = Buffer::FromHugeFile(path);
    REQUIRE(buffer.Content().IsHuge());

    QueryReplace qr(buffer);
    Type(qr, "(\\w+)@(\\w+)");
    qr.ConfirmPattern();
    Type(qr, "$2@$1");
    qr.ConfirmReplacement();

    qr.ReplaceAndNext();
    REQUIRE(qr.ReplacementCount() == 1);
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Done); // the only match in the document
    REQUIRE(buffer.Content().Substring(needleOffset, 13) == "example@alice");

    std::filesystem::remove(path);
}

TEST_CASE("query-replace-regexp on a huge buffer finds a match straddling an internal scan-window boundary",
          "[QueryReplace][HugeFile]") {
    constexpr std::size_t kWindowBody  = 4 * 1024 * 1024;
    const std::string     needle       = "a@bbbbbb"; // 4 bytes before, 4 after the boundary
    const std::size_t     needleOffset = kWindowBody - 4;

    std::string content(kWindowBody + 4096, '.');
    content.replace(needleOffset, needle.size(), needle);
    const std::filesystem::path path = WriteTempFile("ned_queryreplace_huge_boundary.txt", content);

    Buffer buffer = Buffer::FromHugeFile(path);

    QueryReplace qr(buffer);
    Type(qr, "(\\w+)@(\\w+)");
    qr.ConfirmPattern();
    Type(qr, "$2-$1");
    qr.ConfirmReplacement();

    qr.ReplaceAndNext();
    REQUIRE(qr.ReplacementCount() == 1);
    REQUIRE(buffer.Content().Substring(needleOffset, 8) == "bbbbbb-a");

    std::filesystem::remove(path);
}

TEST_CASE("A pattern with no match anywhere in a huge multi-window document terminates cleanly",
          "[QueryReplace][HugeFile]") {
    // Exercises the "no match in this window either -- advance searchFrom
    // past it and keep going" path across several internal scan windows
    // with nothing ever found, proving it terminates rather than looping.
    constexpr std::size_t kWindowBody = 4 * 1024 * 1024;

    const std::string           content(2 * kWindowBody + 500, '.');
    const std::filesystem::path path = WriteTempFile("ned_queryreplace_huge_nomatch_multiwindow.txt", content);

    Buffer buffer = Buffer::FromHugeFile(path);

    QueryReplace qr(buffer);
    Type(qr, "TARGETXYZ");
    qr.ConfirmPattern();
    Type(qr, "X");
    qr.ConfirmReplacement();

    REQUIRE(qr.ReplacementCount() == 0);
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Done);

    std::filesystem::remove(path);
}

TEST_CASE("Lookbehind sees real preceding context for a match found in a later window on a huge buffer",
          "[QueryReplace][HugeFile]") {
    constexpr std::size_t kWindowBody = 4 * 1024 * 1024;
    const std::size_t     matchOffset = kWindowBody + 40000; // well past the first window

    std::string content(kWindowBody + 60000, '.');
    content.replace(matchOffset, 8, "blue sky");
    const std::filesystem::path path = WriteTempFile("ned_queryreplace_huge_lookbehind.txt", content);

    Buffer buffer = Buffer::FromHugeFile(path);

    QueryReplace qr(buffer);
    Type(qr, "(?<=blue )sky");
    qr.ConfirmPattern();
    Type(qr, "sea");
    qr.ConfirmReplacement();

    qr.ReplaceAndNext();
    REQUIRE(qr.ReplacementCount() == 1);
    REQUIRE(buffer.Content().Substring(matchOffset, 5) == "blue ");
    REQUIRE(buffer.Content().Substring(matchOffset + 5, 3) == "sea");

    std::filesystem::remove(path);
}

TEST_CASE("ReplaceAll on a huge buffer correctly shifts offsets across multiple windows",
          "[QueryReplace][HugeFile]") {
    constexpr std::size_t kWindowBody = 4 * 1024 * 1024;
    const std::string     needle      = "TOKEN";
    const std::string     replacement = "REPLACED";             // longer than needle -- exercises offset drift across replaces
    const std::size_t     offset1     = 100;                    // first window
    const std::size_t     offset2     = kWindowBody + 1000;     // second window
    const std::size_t     offset3     = 2 * kWindowBody + 1000; // third window

    std::string content(2 * kWindowBody + 50000, '.');
    content.replace(offset1, needle.size(), needle);
    content.replace(offset2, needle.size(), needle);
    content.replace(offset3, needle.size(), needle);
    const std::filesystem::path path = WriteTempFile("ned_queryreplace_huge_multireplace.txt", content);

    Buffer buffer = Buffer::FromHugeFile(path);

    QueryReplace qr(buffer);
    Type(qr, "TOKEN");
    qr.ConfirmPattern();
    Type(qr, replacement);
    qr.ConfirmReplacement();

    qr.ReplaceAll();
    REQUIRE(qr.ReplacementCount() == 3);
    REQUIRE(qr.CurrentStage() == QueryReplace::Stage::Done);

    const std::size_t drift = replacement.size() - needle.size();
    REQUIRE(buffer.Content().Substring(offset1, replacement.size()) == replacement);
    REQUIRE(buffer.Content().Substring(offset2 + drift, replacement.size()) == replacement);
    REQUIRE(buffer.Content().Substring(offset3 + 2 * drift, replacement.size()) == replacement);

    std::filesystem::remove(path);
}
