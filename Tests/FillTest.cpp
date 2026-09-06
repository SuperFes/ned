#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "Editor/Fill.h"
#include "Text/Buffer.h"
#include "Text/Rope.h"

using ned::editor::FillParagraph;
using ned::editor::FindParagraphRange;
using ned::editor::WrapWords;
using ned::text::Buffer;
using ned::text::Rope;

TEST_CASE("WrapWords packs words greedily up to width", "[Fill]") {
    const std::vector<std::string> words = {"the", "quick", "brown", "fox", "jumps"};
    const auto                     lines = WrapWords(words, 11);
    REQUIRE(lines.size() == 3);
    REQUIRE(lines[0] == "the quick");
    REQUIRE(lines[1] == "brown fox");
    REQUIRE(lines[2] == "jumps");
}

TEST_CASE("WrapWords never splits a word wider than width", "[Fill]") {
    const std::vector<std::string> words = {"a", "supercalifragilisticexpialidocious", "b"};
    const auto                     lines = WrapWords(words, 5);
    REQUIRE(lines.size() == 3);
    REQUIRE(lines[0] == "a");
    REQUIRE(lines[1] == "supercalifragilisticexpialidocious");
    REQUIRE(lines[2] == "b");
}

TEST_CASE("WrapWords on an empty word list returns no lines", "[Fill]") {
    REQUIRE(WrapWords({}, 40).empty());
}

TEST_CASE("WrapWords widths multi-byte words by codepoint count, not byte count", "[Fill]") {
    // "café" is 4 codepoints / 5 bytes, "中文" is 2 codepoints / 6 bytes --
    // a byte-counting width would wrap these far too early.
    const std::vector<std::string> words = {"caf\xc3\xa9", "\xe4\xb8\xad\xe6\x96\x87", "ok"};
    const auto                     lines = WrapWords(words, 10);
    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0] == "caf\xc3\xa9 \xe4\xb8\xad\xe6\x96\x87 ok");
}

TEST_CASE("WrapWords never splits a multi-byte codepoint mid-word", "[Fill]") {
    // Each word's bytes must always travel together -- WrapWords only ever
    // concatenates whole words with plain ASCII spaces, so a codepoint can
    // never straddle a wrap point.
    const std::vector<std::string> words = {"\xe4\xb8\xad\xe6\x96\x87", "caf\xc3\xa9"};
    const auto                     lines = WrapWords(words, 1); // forces each word onto its own line
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0] == "\xe4\xb8\xad\xe6\x96\x87");
    REQUIRE(lines[1] == "caf\xc3\xa9");
}

TEST_CASE("FindParagraphRange spans a run of non-blank lines around point", "[Fill]") {
    Buffer buffer("test", Rope("intro\n\nfirst line\nsecond line\n\noutro"));
    buffer.SetPoint(buffer.Content().LineToByteOffset(3)); // "second line"

    const auto range = FindParagraphRange(buffer.Content(), buffer.Point());
    REQUIRE(range.has_value());
    REQUIRE(buffer.Content().Substring(range->first, range->second - range->first) == "first line\nsecond line");
}

TEST_CASE("FindParagraphRange on a blank line scans forward to the next paragraph", "[Fill]") {
    Buffer buffer("test", Rope("first\n\nsecond"));
    buffer.SetPoint(buffer.Content().LineToByteOffset(1)); // the blank line

    const auto range = FindParagraphRange(buffer.Content(), buffer.Point());
    REQUIRE(range.has_value());
    REQUIRE(buffer.Content().Substring(range->first, range->second - range->first) == "second");
}

TEST_CASE("FindParagraphRange returns nullopt when nothing non-blank follows", "[Fill]") {
    Buffer buffer("test", Rope("text\n\n  \n"));
    buffer.SetPoint(buffer.Content().LineToByteOffset(1));

    REQUIRE_FALSE(FindParagraphRange(buffer.Content(), buffer.Point()).has_value());
}

TEST_CASE("FillParagraph rewraps a plain paragraph to fill-column", "[Fill]") {
    Buffer buffer("test", Rope("the quick brown fox jumps over the lazy dog"));
    buffer.SetPoint(0);

    FillParagraph(buffer, 20);

    REQUIRE(buffer.Content().Substring(0, buffer.Content().ByteLength()) ==
            "the quick brown fox\njumps over the lazy\ndog");
    REQUIRE(buffer.Point() == buffer.Content().ByteLength());
}

TEST_CASE("FillParagraph preserves the first line's indentation on every wrapped line", "[Fill]") {
    Buffer buffer("test", Rope("    the quick brown fox jumps"));
    buffer.SetPoint(0);

    FillParagraph(buffer, 14);

    REQUIRE(buffer.Content().Substring(0, buffer.Content().ByteLength()) == "    the quick\n    brown fox\n    jumps");
}

TEST_CASE("FillParagraph rewraps multi-byte UTF-8 text without corrupting any codepoint", "[Fill]") {
    // "café" (5 bytes/4 codepoints) and "中文" (6 bytes/2 codepoints) mixed
    // with plain ASCII words -- end-to-end through Buffer, not just
    // WrapWords in isolation, so line/byte-offset bookkeeping is exercised
    // too.
    Buffer buffer("test", Rope("go to the caf\xc3\xa9 and read \xe4\xb8\xad\xe6\x96\x87 today"));
    buffer.SetPoint(0);

    FillParagraph(buffer, 15);

    const std::string result = buffer.Content().Substring(0, buffer.Content().ByteLength());
    // Every original word must survive intact, in order -- confirms no
    // codepoint was split or dropped by the rewrap, regardless of exactly
    // where the line breaks landed.
    REQUIRE(result.find("caf\xc3\xa9") != std::string::npos);
    REQUIRE(result.find("\xe4\xb8\xad\xe6\x96\x87") != std::string::npos);
    // A wrap point replaces the word-separating space with a newline (not
    // both), so restore it as a space to compare against the original.
    std::string collapsed = result;
    std::replace(collapsed.begin(), collapsed.end(), '\n', ' ');
    REQUIRE(collapsed == "go to the caf\xc3\xa9 and read \xe4\xb8\xad\xe6\x96\x87 today");
}

TEST_CASE("FillParagraph strips and reattaches a uniform comment prefix", "[Fill]") {
    Buffer buffer("test", Rope("// the quick brown fox\n// jumps over the lazy dog"));
    buffer.SetPoint(0);

    FillParagraph(buffer, 16, "//");

    REQUIRE(buffer.Content().Substring(0, buffer.Content().ByteLength()) ==
            "// the quick\n// brown fox\n// jumps over\n// the lazy dog");
}

TEST_CASE("FillParagraph leaves a mixed comment/non-comment paragraph as plain text", "[Fill]") {
    Buffer buffer("test", Rope("// commented\nnot commented"));
    buffer.SetPoint(0);

    FillParagraph(buffer, 40, "//");

    // No line in range uniformly carries "//", so the prefix passes through
    // untouched as ordinary paragraph text rather than being stripped.
    REQUIRE(buffer.Content().Substring(0, buffer.Content().ByteLength()) == "// commented not commented");
}

TEST_CASE("FillParagraph is a no-op when point sits in trailing blank lines", "[Fill]") {
    Buffer buffer("test", Rope("text\n\n  \n"));
    buffer.SetPoint(buffer.Content().LineToByteOffset(1));

    FillParagraph(buffer, 40);

    REQUIRE(buffer.Content().Substring(0, buffer.Content().ByteLength()) == "text\n\n  \n");
}
