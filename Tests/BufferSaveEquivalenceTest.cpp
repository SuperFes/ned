// huge-file-editing follow-up (streaming save): Buffer::SaveToFile's
// trim-trailing-whitespace / ensure-final-newline / line-ending-expansion
// pipeline now has two implementations -- the original whole-string
// algorithm (non-huge buffers, unchanged) and StreamingSaveWriter (huge
// buffers, Buffer.cpp). This file exists to prove they produce byte-
// identical output across every combination of flags and content shape
// that actually stresses the streaming algorithm's own state (trailing
// whitespace runs, trailing blank-line runs, and both kinds of run
// straddling a PieceTable chunk boundary) -- not just "both save
// something."

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "Text/Buffer.h"
#include "Text/LineEnding.h"

using ned::text::Buffer;
using ned::text::LineEnding;

namespace {
std::filesystem::path WriteTempFile(const std::string& name, std::string_view content) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream                file(path, std::ios::binary);
    file << content;
    return path;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// Saves the same source content via both the normal (non-huge, whole-string)
// path and the huge (streaming) path with identical flags, and returns
// {normalOutput, hugeOutput} for the caller to compare. Uses a fresh pair of
// source/output paths per call (suffixed by `tag`) so parallel-ish reuse
// within one TEST_CASE never collides.
std::pair<std::string, std::string> SaveBothWays(const std::string& tag, const std::string& content, bool ensureFinalNewline,
                                                  bool trimTrailingWhitespace, std::optional<LineEnding> lineEndingOverride) {
    const std::filesystem::path sourcePath = WriteTempFile("ned_save_equiv_src_" + tag + ".txt", content);

    Buffer normalBuffer = Buffer::FromFile(sourcePath);
    Buffer hugeBuffer    = Buffer::FromHugeFile(sourcePath);
    REQUIRE_FALSE(normalBuffer.Content().IsHuge());
    REQUIRE(hugeBuffer.Content().IsHuge());

    const std::filesystem::path normalOut = std::filesystem::temp_directory_path() / ("ned_save_equiv_normal_" + tag + ".txt");
    const std::filesystem::path hugeOut    = std::filesystem::temp_directory_path() / ("ned_save_equiv_huge_" + tag + ".txt");

    normalBuffer.SaveToFile(normalOut, ensureFinalNewline, trimTrailingWhitespace, lineEndingOverride);
    hugeBuffer.SaveToFile(hugeOut, ensureFinalNewline, trimTrailingWhitespace, lineEndingOverride);

    const std::string normalResult = ReadFile(normalOut);
    const std::string hugeResult   = ReadFile(hugeOut);

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(normalOut);
    std::filesystem::remove(hugeOut);

    return {normalResult, hugeResult};
}

void RequireBothWaysMatch(const std::string& tag, const std::string& content, bool ensureFinalNewline, bool trimTrailingWhitespace,
                          std::optional<LineEnding> lineEndingOverride = std::nullopt) {
    const auto [normalResult, hugeResult] = SaveBothWays(tag, content, ensureFinalNewline, trimTrailingWhitespace, lineEndingOverride);
    REQUIRE(hugeResult == normalResult);
}
} // namespace

TEST_CASE("Streaming save matches non-streaming save: empty content", "[Buffer][HugeFile][SaveEquivalence]") {
    RequireBothWaysMatch("empty", "", true, true);
    RequireBothWaysMatch("empty_notrim", "", true, false);
    RequireBothWaysMatch("empty_nonewline", "", false, true);
}

TEST_CASE("Streaming save matches non-streaming save: no trailing newline", "[Buffer][HugeFile][SaveEquivalence]") {
    RequireBothWaysMatch("no_trailing_nl_ensure", "hello world", true, true);
    RequireBothWaysMatch("no_trailing_nl_noensure", "hello world", false, true);
}

TEST_CASE("Streaming save matches non-streaming save: several trailing blank lines", "[Buffer][HugeFile][SaveEquivalence]") {
    RequireBothWaysMatch("trailing_blanks", "line one\nline two\n\n\n\n\n", true, true);
    RequireBothWaysMatch("trailing_blanks_notrim", "line one\nline two\n\n\n\n\n", true, false);
    RequireBothWaysMatch("trailing_blanks_only", "\n\n\n\n", true, true);
    RequireBothWaysMatch("trailing_whitespace_only_blanks", "a\n   \n\t\t\n  \t \n", true, true);
}

TEST_CASE("Streaming save matches non-streaming save: trailing whitespace on various lines", "[Buffer][HugeFile][SaveEquivalence]") {
    RequireBothWaysMatch("trailing_ws", "line one   \nline two\t\t\n  line three  \n", true, true);
    RequireBothWaysMatch("trailing_ws_interior_ws_preserved", "a  b\tc   \n", true, true); // interior whitespace must survive
    RequireBothWaysMatch("all_whitespace_line", "   \t  \n", true, true);
}

TEST_CASE("Streaming save matches non-streaming save: line-ending override expands correctly", "[Buffer][HugeFile][SaveEquivalence]") {
    const std::string content = "line one  \nline two\n\n\n";
    RequireBothWaysMatch("crlf_trim", content, true, true, LineEnding::CRLF);
    RequireBothWaysMatch("cr_trim", content, true, true, LineEnding::CR);
    RequireBothWaysMatch("crlf_notrim", content, true, false, LineEnding::CRLF);
    RequireBothWaysMatch("lf_explicit", content, true, true, LineEnding::LF);
}

TEST_CASE("Streaming save matches non-streaming save: a trailing-whitespace run straddles a chunk boundary",
         "[Buffer][HugeFile][SaveEquivalence]") {
    // PieceTable's original-file leaves are 256 KiB (kOriginalChunkSize,
    // Text/PieceTable.cpp) -- place a long trailing-whitespace run (and the
    // real content the state machine must flush once it ends) straddling
    // that exact boundary so Feed()'s pendingWhitespace_ is guaranteed to
    // carry state across a ForEachChunk sink call, not just within one.
    constexpr std::size_t kChunkSize = 256 * 1024;
    std::string           content(kChunkSize - 10, 'a');
    content += "line-before   "; // trailing spaces starting just before the boundary
    content.append(30, ' ');     // push the run well past it
    content += "\nline-after\n";

    RequireBothWaysMatch("ws_straddles_chunk", content, true, true);
}

TEST_CASE("Streaming save matches non-streaming save: a trailing-blank-line run straddles a chunk boundary",
         "[Buffer][HugeFile][SaveEquivalence]") {
    constexpr std::size_t kChunkSize = 256 * 1024;
    std::string           content(kChunkSize - 20, 'b');
    content += "\nreal last line\n";
    content.append(40, '\n'); // a long run of trailing blank lines straddling the boundary

    RequireBothWaysMatch("blanks_straddle_chunk", content, true, true);
    RequireBothWaysMatch("blanks_straddle_chunk_notrim", content, true, false);
}

TEST_CASE("Streaming save matches non-streaming save: randomized content/flag combinations", "[Buffer][HugeFile][SaveEquivalence]") {
    std::mt19937                       rng(9001); // fixed seed: deterministic test
    std::uniform_int_distribution<int> lineCountDist(0, 40);
    std::uniform_int_distribution<int> lineLenDist(0, 20);
    std::uniform_int_distribution<int> trailingWsDist(0, 6);
    std::uniform_int_distribution<int> trailingBlankLinesDist(0, 5);
    std::uniform_int_distribution<int> boolDist(0, 1);
    std::uniform_int_distribution<int> endingDist(0, 3);

    for (int iteration = 0; iteration < 40; ++iteration) {
        std::string content;
        const int   lineCount = lineCountDist(rng);
        for (int line = 0; line < lineCount; ++line) {
            const int lineLen = lineLenDist(rng);
            for (int i = 0; i < lineLen; ++i) {
                content.push_back(static_cast<char>('a' + (i % 26)));
            }
            const int trailingWs = trailingWsDist(rng);
            for (int i = 0; i < trailingWs; ++i) {
                content.push_back((i % 2 == 0) ? ' ' : '\t');
            }
            content.push_back('\n');
        }
        const int trailingBlankLines = trailingBlankLinesDist(rng);
        for (int i = 0; i < trailingBlankLines; ++i) {
            const int trailingWs = trailingWsDist(rng);
            for (int j = 0; j < trailingWs; ++j) {
                content.push_back((j % 2 == 0) ? ' ' : '\t');
            }
            content.push_back('\n');
        }

        const bool ensureFinalNewline    = boolDist(rng) == 1;
        const bool trimTrailingWhitespace = boolDist(rng) == 1;
        std::optional<LineEnding> ending;
        switch (endingDist(rng)) {
            case 0: ending = std::nullopt; break;
            case 1: ending = LineEnding::LF; break;
            case 2: ending = LineEnding::CRLF; break;
            default: ending = LineEnding::CR; break;
        }

        RequireBothWaysMatch("rand_" + std::to_string(iteration), content, ensureFinalNewline, trimTrailingWhitespace, ending);
    }
}
