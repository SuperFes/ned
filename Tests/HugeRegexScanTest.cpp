#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "Editor/HugeRegexScan.h"
#include "Editor/RegexPattern.h"
#include "Text/Buffer.h"

using ned::editor::FindLastRegexMatchHugeBefore;
using ned::editor::FindNextRegexMatchHuge;
using ned::editor::RegexPattern;
using ned::text::Buffer;

namespace {
// huge-file-regex-replace follow-up: same "each test file duplicates its own
// tiny helper" precedent QueryReplaceTest.cpp/IncrementalSearchTest.cpp
// already establish -- builds a real on-disk file so Buffer::FromHugeFile has
// something to open (it forces IsHuge() regardless of actual file size).
std::filesystem::path WriteTempFile(const std::string& name, std::string_view content) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::ofstream               file(path, std::ios::binary);
    file << content;
    return path;
}
} // namespace

TEST_CASE("FindNextRegexMatchHuge finds a simple match and reports absolute offsets", "[HugeRegexScan]") {
    const std::filesystem::path path   = WriteTempFile("ned_hugeregex_fwd_basic.txt", "the quick brown fox jumps");
    Buffer                      buffer = Buffer::FromHugeFile(path);
    REQUIRE(buffer.Content().IsHuge());

    const RegexPattern pattern("\\bfox\\b");
    const auto         found = FindNextRegexMatchHuge(buffer, pattern, 0);

    REQUIRE(found.has_value());
    REQUIRE(found->windowStart + found->match.start == 16);
    REQUIRE(found->windowStart + found->match.end == 19);

    std::filesystem::remove(path);
}

TEST_CASE("FindNextRegexMatchHuge respects searchFrom -- no match reported before it", "[HugeRegexScan]") {
    const std::filesystem::path path   = WriteTempFile("ned_hugeregex_fwd_searchfrom.txt", "cat sat cat mat cat");
    Buffer                      buffer = Buffer::FromHugeFile(path);

    const RegexPattern pattern("cat");
    const auto         found = FindNextRegexMatchHuge(buffer, pattern, 4);

    REQUIRE(found.has_value());
    REQUIRE(found->windowStart + found->match.start == 8);

    std::filesystem::remove(path);
}

TEST_CASE("FindNextRegexMatchHuge returns nullopt with no match anywhere from searchFrom", "[HugeRegexScan]") {
    const std::filesystem::path path   = WriteTempFile("ned_hugeregex_fwd_nomatch.txt", "no digits here");
    Buffer                      buffer = Buffer::FromHugeFile(path);

    const RegexPattern pattern("[0-9]+");
    REQUIRE_FALSE(FindNextRegexMatchHuge(buffer, pattern, 0).has_value());

    std::filesystem::remove(path);
}

TEST_CASE("FindNextRegexMatchHuge finds a match past the first internal scan window", "[HugeRegexScan]") {
    constexpr std::size_t kWindowBody = 4 * 1024 * 1024; // mirrors HugeRegexScan.cpp's own constant
    const std::size_t     matchOffset = kWindowBody + 1000;

    std::string content(kWindowBody + 4096, '.');
    content.replace(matchOffset, 5, "alice");
    const std::filesystem::path path = WriteTempFile("ned_hugeregex_fwd_multiwindow.txt", content);

    Buffer             buffer = Buffer::FromHugeFile(path);
    const RegexPattern pattern("\\w+");
    const auto         found = FindNextRegexMatchHuge(buffer, pattern, 0);

    REQUIRE(found.has_value());
    REQUIRE(found->windowStart + found->match.start == matchOffset);
    REQUIRE(found->windowStart + found->match.end == matchOffset + 5);

    std::filesystem::remove(path);
}

TEST_CASE("FindNextRegexMatchHuge finds a match straddling an internal scan-window boundary", "[HugeRegexScan]") {
    constexpr std::size_t kWindowBody  = 4 * 1024 * 1024;
    const std::string     needle       = "a-bbbbbb"; // 4 bytes before, 4 after the boundary
    const std::size_t     needleOffset = kWindowBody - 4;

    std::string content(kWindowBody + 4096, '.');
    content.replace(needleOffset, needle.size(), needle);
    const std::filesystem::path path = WriteTempFile("ned_hugeregex_fwd_boundary.txt", content);

    Buffer             buffer = Buffer::FromHugeFile(path);
    const RegexPattern pattern("[a-z]+-[a-z]+");
    const auto         found = FindNextRegexMatchHuge(buffer, pattern, 0);

    REQUIRE(found.has_value());
    REQUIRE(found->windowStart + found->match.start == needleOffset);
    REQUIRE(found->windowStart + found->match.end == needleOffset + needle.size());

    std::filesystem::remove(path);
}

TEST_CASE("FindLastRegexMatchHugeBefore finds the rightmost match before the offset", "[HugeRegexScan]") {
    const std::filesystem::path path   = WriteTempFile("ned_hugeregex_back_basic.txt", "cat sat cat mat cat");
    Buffer                      buffer = Buffer::FromHugeFile(path);

    const RegexPattern pattern("cat");
    const auto         found = FindLastRegexMatchHugeBefore(buffer, pattern, buffer.Content().ByteLength());

    REQUIRE(found.has_value());
    REQUIRE(found->windowStart + found->match.start == 16); // the last "cat", not the first two

    std::filesystem::remove(path);
}

TEST_CASE("FindLastRegexMatchHugeBefore only considers matches strictly before the offset", "[HugeRegexScan]") {
    const std::filesystem::path path   = WriteTempFile("ned_hugeregex_back_before.txt", "cat sat cat mat cat");
    Buffer                      buffer = Buffer::FromHugeFile(path);

    const RegexPattern pattern("cat");
    const auto         found = FindLastRegexMatchHugeBefore(buffer, pattern, 10); // between the 2nd and 3rd "cat"

    REQUIRE(found.has_value());
    REQUIRE(found->windowStart + found->match.start == 8); // the second "cat", not the third

    std::filesystem::remove(path);
}

TEST_CASE("FindLastRegexMatchHugeBefore returns nullopt when nothing precedes the offset", "[HugeRegexScan]") {
    const std::filesystem::path path   = WriteTempFile("ned_hugeregex_back_none.txt", "xxx cat");
    Buffer                      buffer = Buffer::FromHugeFile(path);

    const RegexPattern pattern("cat");
    REQUIRE_FALSE(FindLastRegexMatchHugeBefore(buffer, pattern, 3).has_value()); // "cat" starts at 4

    std::filesystem::remove(path);
}

TEST_CASE("FindLastRegexMatchHugeBefore finds a match by widening the window past the first attempt",
          "[HugeRegexScan]") {
    constexpr std::size_t kWindowBody = 4 * 1024 * 1024;
    const std::size_t     matchOffset = 1000; // near the real document start

    std::string content(2 * kWindowBody, '.');
    content.replace(matchOffset, 5, "alice");
    const std::filesystem::path path = WriteTempFile("ned_hugeregex_back_widen.txt", content);

    Buffer             buffer = Buffer::FromHugeFile(path);
    const RegexPattern pattern("\\w+");
    // beforeOffset is deep into the document -- the only match is far to the left,
    // well outside the first (reach == kWindowBody) window's own start.
    const auto found = FindLastRegexMatchHugeBefore(buffer, pattern, 2 * kWindowBody - 500);

    REQUIRE(found.has_value());
    REQUIRE(found->windowStart + found->match.start == matchOffset);
    REQUIRE(found->windowStart + found->match.end == matchOffset + 5);

    std::filesystem::remove(path);
}

TEST_CASE("FindLastRegexMatchHugeBefore resolves a match whose completion needs trailing context past the offset",
          "[HugeRegexScan]") {
    // The match starts a few bytes before beforeOffset but only completes a few
    // bytes after it -- exercises the fixed kOverlapMargin trailing-completion
    // window rather than the leading (reach-growing) side.
    const std::filesystem::path path   = WriteTempFile("ned_hugeregex_back_trailing.txt", "prefix cat123 suffix");
    Buffer                      buffer = Buffer::FromHugeFile(path);

    const RegexPattern pattern("cat[0-9]+");
    const auto         found = FindLastRegexMatchHugeBefore(buffer, pattern, 9); // beforeOffset lands inside "cat123" itself

    REQUIRE(found.has_value());
    REQUIRE(found->windowStart + found->match.start == 7);
    REQUIRE(found->windowStart + found->match.end == 13); // "cat123" fully resolved, past beforeOffset

    std::filesystem::remove(path);
}

TEST_CASE("FindLastRegexMatchHugeBefore terminates against zero-width matches", "[HugeRegexScan]") {
    const std::filesystem::path path   = WriteTempFile("ned_hugeregex_back_zerowidth.txt", "bbb");
    Buffer                      buffer = Buffer::FromHugeFile(path);

    const RegexPattern pattern("a*"); // matches the empty string at every position
    const auto         found = FindLastRegexMatchHugeBefore(buffer, pattern, 3);

    REQUIRE(found.has_value());
    REQUIRE(found->windowStart + found->match.start == 2); // rightmost zero-width start strictly before offset 3

    std::filesystem::remove(path);
}
