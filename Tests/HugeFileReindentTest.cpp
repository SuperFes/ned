//
// Editor/HugeFileReindent.h: the lexical, string/line-comment-aware
// bracket-depth streaming reindent that stands in for a real tree-sitter
// reindent on a document too large to parse at all. Most cases feed
// HugeReindentStream directly, at varying chunk splits, to pin the
// chunk-boundary-insensitivity ITextStorage::ForEachChunk's own contract
// demands; a handful go through StreamHugeReindent against a real
// PieceTableStorage-backed huge buffer to cover the convenience wrapper
// itself.
//

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Editor/HugeFileReindent.h"
#include "Editor/IndentStyle.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using ned::editor::HugeReindentOutcome;
using ned::editor::HugeReindentStream;
using ned::editor::IndentStyle;
using ned::editor::StreamHugeReindent;

namespace {

// ctest runs each test case in its own process, several at a time, so a
// fixed scratch filename is one shared file every concurrent case truncates
// and deletes under the others -- the pid keeps each run's temp files its
// own.
std::filesystem::path ScratchPath(const std::string& suffix) {
    return std::filesystem::temp_directory_path() /
           ("ned_huge_reindent_test_" + std::to_string(::getpid()) + "_" + suffix);
}

// Feeds `text` to a fresh stream split at every offset in `splits` (each a
// byte offset marking where one chunk ends and the next begins) and
// returns {reindented output, outcome}. splits = {} feeds the whole text as
// one chunk.
std::pair<std::string, HugeReindentOutcome> Run(const std::string& text, const std::string& lineCommentPrefix,
                                                 IndentStyle style, const std::vector<std::size_t>& splits = {}) {
    std::ostringstream buffer;
    // HugeReindentStream writes through a std::ofstream&, not an ostream in
    // general -- a real temp file stands in, cheap and matches what the
    // real caller (a sibling-temp-file swap) actually does.
    const std::filesystem::path path = ScratchPath("scratch.txt");
    std::ofstream               out(path, std::ios::binary | std::ios::trunc);
    HugeReindentStream          stream(out, lineCommentPrefix, style);

    std::size_t prev = 0;
    for (const std::size_t split : splits) {
        stream(std::string_view(text).substr(prev, split - prev));
        prev = split;
    }
    stream(std::string_view(text).substr(prev));
    const HugeReindentOutcome outcome = stream.Finish();
    out.close();

    std::ifstream     in(path, std::ios::binary);
    const std::string result((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::filesystem::remove(path);
    return {result, outcome};
}

constexpr IndentStyle kFourSpaces{.useTabs = false, .width = 4};

} // namespace

TEST_CASE("HugeReindentStream reindents simple nested braces to depth*width", "[HugeFileReindent]") {
    const std::string text = "void f() {\nint x = 1;\nif (x) {\nreturn x;\n}\n}\n";
    const auto [result, outcome] = Run(text, "//", kFourSpaces);
    REQUIRE(outcome.success);
    REQUIRE(result == "void f() {\n    int x = 1;\n    if (x) {\n        return x;\n    }\n}\n");
}

TEST_CASE("HugeReindentStream's result is identical regardless of chunk boundaries", "[HugeFileReindent]") {
    const std::string text = "void f() {\nint x = 1;\nif (x) {\nreturn x;\n}\n}\n";
    const auto [whole, wholeOutcome] = Run(text, "//", kFourSpaces);
    REQUIRE(wholeOutcome.success);

    // Split at every single byte, and at a few arbitrary mid-token points.
    std::vector<std::size_t> everyByte;
    for (std::size_t i = 1; i < text.size(); ++i) {
        everyByte.push_back(i);
    }
    const auto [splitEveryByte, splitOutcome] = Run(text, "//", kFourSpaces, everyByte);
    REQUIRE(splitOutcome.success);
    REQUIRE(splitEveryByte == whole);
}

TEST_CASE("HugeReindentStream does not count a brace inside a string toward depth", "[HugeFileReindent]") {
    const std::string text = "void f() {\nconst char* s = \"{{{\";\nreturn;\n}\n";
    const auto [result, outcome] = Run(text, "//", kFourSpaces);
    REQUIRE(outcome.success);
    REQUIRE(result == "void f() {\n    const char* s = \"{{{\";\n    return;\n}\n");
}

TEST_CASE("HugeReindentStream's generic quote detection never fires inside a real line comment, even one "
          "containing an English contraction",
          "[HugeFileReindent]") {
    // A bare quote character is treated generically as a string opener
    // (this file's own header comment explains why) -- but line-comment
    // content is checked BEFORE string detection and returns immediately,
    // so an ordinary "don't"/"it's"-shaped comment must never be
    // misread as an unterminated string that swallows the rest of the file.
    const std::string text = "void f() {\n// don't reindent this, it's already fine\nint x = 1;\n}\n";
    const auto [result, outcome] = Run(text, "//", kFourSpaces);
    REQUIRE(outcome.success);
    REQUIRE(result == "void f() {\n    // don't reindent this, it's already fine\n    int x = 1;\n}\n");
}

TEST_CASE("HugeReindentStream does not count a brace inside a line comment toward depth", "[HugeFileReindent]") {
    const std::string text = "void f() {\n// closing early: }\nreturn;\n}\n";
    const auto [result, outcome] = Run(text, "//", kFourSpaces);
    REQUIRE(outcome.success);
    REQUIRE(result == "void f() {\n    // closing early: }\n    return;\n}\n");
}

TEST_CASE("HugeReindentStream handles a closing-delimiter line by aligning it with its opener's own level",
          "[HugeFileReindent]") {
    const std::string text = "if (x) {\nfoo();\n} else {\nbar();\n}\n";
    const auto [result, outcome] = Run(text, "//", kFourSpaces);
    REQUIRE(outcome.success);
    // "} else {" is a closer-then-opener line -- it dedents to match "if",
    // and its OWN trailing "{" still opens a fresh level for "bar();".
    REQUIRE(result == "if (x) {\n    foo();\n} else {\n    bar();\n}\n");
}

TEST_CASE("HugeReindentStream leaves a blank line blank, never inserting indentation into it", "[HugeFileReindent]") {
    const std::string text = "void f() {\n\nint x = 1;\n}\n";
    const auto [result, outcome] = Run(text, "//", kFourSpaces);
    REQUIRE(outcome.success);
    REQUIRE(result == "void f() {\n\n    int x = 1;\n}\n");
}

TEST_CASE("HugeReindentStream fails on an extra closing delimiter, leaving success false", "[HugeFileReindent]") {
    const std::string text = "void f() {\nint x = 1;\n}\n}\n";
    const auto [result, outcome] = Run(text, "//", kFourSpaces);
    REQUIRE_FALSE(outcome.success);
    REQUIRE_FALSE(outcome.errorMessage.empty());
    (void)result; // the caller is expected to discard this on failure, not inspect it
}

TEST_CASE("HugeReindentStream fails on an unclosed delimiter at end of input", "[HugeFileReindent]") {
    const std::string text = "void f() {\nint x = 1;\n";
    const auto [result, outcome] = Run(text, "//", kFourSpaces);
    REQUIRE_FALSE(outcome.success);
    (void)result;
}

TEST_CASE("HugeReindentStream fails on an unterminated string literal", "[HugeFileReindent]") {
    const std::string text = "void f() {\nconst char* s = \"unterminated\nint x = 1;\n}\n";
    const auto [result, outcome] = Run(text, "//", kFourSpaces);
    REQUIRE_FALSE(outcome.success);
    (void)result;
}

TEST_CASE("HugeReindentStream reports zero lines changed when the file is already correctly indented",
          "[HugeFileReindent]") {
    const std::string alreadyClean = "void f() {\n    int x = 1;\n}\n";
    const auto [result, outcome] = Run(alreadyClean, "//", kFourSpaces);
    REQUIRE(outcome.success);
    REQUIRE(outcome.linesChanged == 0);
    REQUIRE(result == alreadyClean);
}

TEST_CASE("HugeReindentStream counts exactly the lines whose own indent actually changed", "[HugeFileReindent]") {
    const std::string text = "void f() {\n  int x = 1;\nint y = 2;\n}\n"; // line 2 wrong (2sp), line 3 wrong (0sp)
    const auto [result, outcome] = Run(text, "//", kFourSpaces);
    REQUIRE(outcome.success);
    REQUIRE(outcome.linesChanged == 2);
}

TEST_CASE("HugeReindentStream renders tab-style indentation via the same smart-tabs convention as Indent.h",
          "[HugeFileReindent]") {
    constexpr IndentStyle tabs{.useTabs = true, .width = 4};
    const std::string      text          = "void f() {\nif (x) {\nreturn;\n}\n}\n";
    const auto [result, outcome]         = Run(text, "//", tabs);
    REQUIRE(outcome.success);
    REQUIRE(result == "void f() {\n\tif (x) {\n\t\treturn;\n\t}\n}\n");
}

TEST_CASE("HugeReindentStream never enters comment mode when no line-comment prefix is configured",
          "[HugeFileReindent]") {
    const std::string text = "void f() {\n# not a comment in this language\nreturn;\n}\n";
    const auto [result, outcome] = Run(text, "", kFourSpaces);
    REQUIRE(outcome.success);
    REQUIRE(result == "void f() {\n    # not a comment in this language\n    return;\n}\n");
}

TEST_CASE("HugeReindentStream tracks a Python-style '#' line comment as its own prefix", "[HugeFileReindent]") {
    const std::string text = "def f():\nif x:\n# closing early: )\nreturn\n";
    const auto [result, outcome] = Run(text, "#", kFourSpaces);
    REQUIRE(outcome.success);
    // No brackets at all in this Python-shaped snippet -- depth never
    // moves, so every line lands at column 0 regardless of the comment;
    // this test exists to confirm '#' comment detection doesn't crash or
    // mis-track state on its own multi-language prefix path.
    REQUIRE(result.find("# closing early: )") != std::string::npos);
}

TEST_CASE("StreamHugeReindent reindents a real PieceTableStorage-backed huge buffer end to end",
          "[HugeFileReindent]") {
    const std::filesystem::path inPath = ScratchPath("input.cpp");
    { std::ofstream(inPath) << "void f() {\nint x = 1;\n}\n"; }

    ned::text::SetHugeFileThreshold(4); // well under this file's real size
    ned::text::Buffer buffer = ned::text::Buffer::FromHugeFile(inPath);
    REQUIRE(buffer.Content().IsHuge());

    const std::filesystem::path outPath = ScratchPath("output.cpp");
    std::ofstream               out(outPath, std::ios::binary | std::ios::trunc);
    const HugeReindentOutcome   outcome = StreamHugeReindent(buffer.Content(), out, "//", kFourSpaces);
    out.close();
    REQUIRE(outcome.success);

    std::ifstream     in(outPath, std::ios::binary);
    const std::string result((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(result == "void f() {\n    int x = 1;\n}\n");

    ned::text::SetHugeFileThreshold(1024ull * 1024 * 1024);
    std::filesystem::remove(inPath);
    std::filesystem::remove(outPath);
}
