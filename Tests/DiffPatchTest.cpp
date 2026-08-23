#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Editor/Vcs/DiffPatch.h"

using ned::editor::vcs::DiffHunkText;
using ned::editor::vcs::ExtractHunkPatch;
using ned::editor::vcs::ParseDiffHunks;

namespace {

// A realistic two-hunk `git diff -U0` output: a single-line modification
// at new line 2 and a two-line addition at new lines 6-7.
const std::string kTwoHunkDiff = "diff --git a/file.txt b/file.txt\n"
                                 "index 1234567..89abcde 100644\n"
                                 "--- a/file.txt\n"
                                 "+++ b/file.txt\n"
                                 "@@ -2 +2 @@ context text\n"
                                 "-old line two\n"
                                 "+new line two\n"
                                 "@@ -5,0 +6,2 @@\n"
                                 "+added line six\n"
                                 "+added line seven\n";

// A pure deletion between new lines 3 and 4 (old lines 4-5 removed).
const std::string kDeletionDiff = "diff --git a/file.txt b/file.txt\n"
                                  "index 1234567..89abcde 100644\n"
                                  "--- a/file.txt\n"
                                  "+++ b/file.txt\n"
                                  "@@ -4,2 +3,0 @@\n"
                                  "-deleted line four\n"
                                  "-deleted line five\n";

} // namespace

TEST_CASE("ExtractHunkPatch slices the covering hunk plus the file header verbatim", "[DiffPatch]") {
    const auto patch = ExtractHunkPatch(kTwoHunkDiff, 2);
    REQUIRE(patch.has_value());
    REQUIRE(*patch == "diff --git a/file.txt b/file.txt\n"
                      "index 1234567..89abcde 100644\n"
                      "--- a/file.txt\n"
                      "+++ b/file.txt\n"
                      "@@ -2 +2 @@ context text\n"
                      "-old line two\n"
                      "+new line two\n");
}

TEST_CASE("ExtractHunkPatch selects by new-side line across multiple hunks", "[DiffPatch]") {
    // Both covered lines of the second (addition) hunk select it; the gap
    // between the hunks selects nothing.
    for (const std::size_t line : {std::size_t{6}, std::size_t{7}}) {
        const auto patch = ExtractHunkPatch(kTwoHunkDiff, line);
        REQUIRE(patch.has_value());
        REQUIRE(*patch == "diff --git a/file.txt b/file.txt\n"
                          "index 1234567..89abcde 100644\n"
                          "--- a/file.txt\n"
                          "+++ b/file.txt\n"
                          "@@ -5,0 +6,2 @@\n"
                          "+added line six\n"
                          "+added line seven\n");
    }
    REQUIRE_FALSE(ExtractHunkPatch(kTwoHunkDiff, 4).has_value());
    REQUIRE_FALSE(ExtractHunkPatch(kTwoHunkDiff, 8).has_value());
}

TEST_CASE("ExtractHunkPatch matches a pure-deletion hunk on both boundary lines", "[DiffPatch]") {
    // The deletion sits between new lines 3 and 4 -- line 4 is where the
    // gutter draws its notch, line 3 the line before the gap; both match.
    for (const std::size_t line : {std::size_t{3}, std::size_t{4}}) {
        const auto patch = ExtractHunkPatch(kDeletionDiff, line);
        REQUIRE(patch.has_value());
        REQUIRE(patch->find("@@ -4,2 +3,0 @@") != std::string::npos);
        REQUIRE(patch->find("-deleted line four") != std::string::npos);
    }
    REQUIRE_FALSE(ExtractHunkPatch(kDeletionDiff, 2).has_value());
    REQUIRE_FALSE(ExtractHunkPatch(kDeletionDiff, 5).has_value());
}

TEST_CASE("ExtractHunkPatch matches a deletion at the very top of the file", "[DiffPatch]") {
    // Deleting a file's first line yields "+0,0" -- new-side lines 0 and 1
    // are the boundaries, and line 1 is the only real buffer line.
    const std::string diff = "diff --git a/file.txt b/file.txt\n"
                             "--- a/file.txt\n"
                             "+++ b/file.txt\n"
                             "@@ -1 +0,0 @@\n"
                             "-first line\n";
    REQUIRE(ExtractHunkPatch(diff, 1).has_value());
    REQUIRE_FALSE(ExtractHunkPatch(diff, 2).has_value());
}

TEST_CASE("ExtractHunkPatch keeps the no-newline marker and repairs a missing final newline", "[DiffPatch]") {
    // Output whose last hunk line has no trailing newline at all (EOF cut)
    // and carries the "\ No newline at end of file" marker.
    const std::string diff  = "diff --git a/file.txt b/file.txt\n"
                              "--- a/file.txt\n"
                              "+++ b/file.txt\n"
                              "@@ -1 +1 @@\n"
                              "-old\n"
                              "+new\n"
                              "\\ No newline at end of file";
    const auto        patch = ExtractHunkPatch(diff, 1);
    REQUIRE(patch.has_value());
    REQUIRE(patch->find("\\ No newline at end of file\n") != std::string::npos);
    REQUIRE(patch->back() == '\n');
}

TEST_CASE("ExtractHunkPatch picks the right file's header block in a multi-file diff", "[DiffPatch]") {
    // Both files carry a hunk covering new line 1 -- a single-file diff is
    // what VcsRunner actually feeds this, but a multi-file input must
    // still pair each hunk with its own file header, not the first one.
    const std::string diff  = "diff --git a/a.txt b/a.txt\n"
                              "--- a/a.txt\n"
                              "+++ b/a.txt\n"
                              "@@ -1 +1 @@\n"
                              "-a old\n"
                              "+a new\n"
                              "diff --git a/b.txt b/b.txt\n"
                              "--- a/b.txt\n"
                              "+++ b/b.txt\n"
                              "@@ -1 +1 @@\n"
                              "-b old\n"
                              "+b new\n";
    const auto        patch = ExtractHunkPatch(diff, 1);
    REQUIRE(patch.has_value());
    // First match wins; its header must be a.txt's, and nothing of b.txt's
    // block may leak in.
    REQUIRE(patch->find("+++ b/a.txt") != std::string::npos);
    REQUIRE(patch->find("b.txt") == std::string::npos);
}

TEST_CASE("ExtractHunkPatch returns nothing for empty or hunkless input", "[DiffPatch]") {
    REQUIRE_FALSE(ExtractHunkPatch("", 1).has_value());
    REQUIRE_FALSE(ExtractHunkPatch("diff --git a/x b/x\n--- a/x\n+++ b/x\n", 1).has_value());
    // A malformed hunk header is skipped, not crashed on.
    REQUIRE_FALSE(ExtractHunkPatch("@@ garbage @@\n+x\n", 1).has_value());
}

TEST_CASE("ParseDiffHunks pairs every hunk with its own file across a multi-file diff", "[DiffPatch]") {
    const auto hunks = ParseDiffHunks(kTwoHunkDiff);
    REQUIRE(hunks.size() == 2);

    REQUIRE(hunks[0].filePath == "file.txt");
    REQUIRE(hunks[0].hunkHeader == "@@ -2 +2 @@ context text");
    REQUIRE(hunks[0].bodyText == "-old line two\n+new line two\n");
    REQUIRE(hunks[0].oldStart == 2);
    REQUIRE(hunks[0].oldCount == 1);
    REQUIRE(hunks[0].newStart == 2);
    REQUIRE(hunks[0].newCount == 1);

    REQUIRE(hunks[1].filePath == "file.txt");
    REQUIRE(hunks[1].hunkHeader == "@@ -5,0 +6,2 @@");
    REQUIRE(hunks[1].bodyText == "+added line six\n+added line seven\n");
    REQUIRE(hunks[1].oldStart == 5);
    REQUIRE(hunks[1].oldCount == 0);
    REQUIRE(hunks[1].newStart == 6);
    REQUIRE(hunks[1].newCount == 2);
}

TEST_CASE("ParseDiffHunks skips a hunk whose header is malformed on either side", "[DiffPatch]") {
    // A well-formed new side but garbage old side (and vice versa) both
    // degrade to "skip this hunk" rather than a half-populated result.
    REQUIRE(ParseDiffHunks("diff --git a/x b/x\n--- a/x\n+++ b/x\n@@ garbage +1 @@\n+x\n").empty());
    REQUIRE(ParseDiffHunks("diff --git a/x b/x\n--- a/x\n+++ b/x\n@@ -1 garbage @@\n+x\n").empty());
}

TEST_CASE("ParseDiffHunks attributes each hunk to the right file in a multi-file diff", "[DiffPatch]") {
    const std::string diff  = "diff --git a/a.txt b/a.txt\n"
                              "--- a/a.txt\n"
                              "+++ b/a.txt\n"
                              "@@ -1 +1 @@\n"
                              "-a old\n"
                              "+a new\n"
                              "diff --git a/b.txt b/b.txt\n"
                              "--- a/b.txt\n"
                              "+++ b/b.txt\n"
                              "@@ -1 +1 @@\n"
                              "-b old\n"
                              "+b new\n";
    const auto        hunks = ParseDiffHunks(diff);
    REQUIRE(hunks.size() == 2);
    REQUIRE(hunks[0].filePath == "a.txt");
    REQUIRE(hunks[0].bodyText.find("a new") != std::string::npos);
    REQUIRE(hunks[1].filePath == "b.txt");
    REQUIRE(hunks[1].bodyText.find("b new") != std::string::npos);
}

TEST_CASE("ParseDiffHunks returns nothing for empty, hunkless, or fileless input", "[DiffPatch]") {
    REQUIRE(ParseDiffHunks("").empty());
    REQUIRE(ParseDiffHunks("diff --git a/x b/x\n--- a/x\n+++ b/x\n").empty());
    // A hunk with no preceding "diff --git" has no file to attribute it to.
    REQUIRE(ParseDiffHunks("@@ -1 +1 @@\n-x\n+y\n").empty());
    REQUIRE(ParseDiffHunks("@@ garbage @@\n+x\n").empty());
}
