#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Janet/InitFile.h"

using ned::janet::WithSetThemeCall;

// Rewriting somebody's config file is the part that has to be predictable,
// so the rule is pinned here rather than inferred from behaviour: replace the
// last line that is *exactly* a (ned/set-theme "literal") call, else append.
// Anything else in the file is left byte for byte alone.

TEST_CASE("An empty or missing init.janet gets the call appended", "[InitFileThemeWrite]") {
    REQUIRE(WithSetThemeCall("", "nord") == "(ned/set-theme \"nord\")\n");
}

TEST_CASE("An existing simple call is replaced in place", "[InitFileThemeWrite]") {
    const std::string before = "(ned/set-theme \"dark\")\n(ned/set-tab-width 4)\n";
    REQUIRE(WithSetThemeCall(before, "nord") == "(ned/set-theme \"nord\")\n(ned/set-tab-width 4)\n");
}

TEST_CASE("A file with no call keeps every line and gains one at the end", "[InitFileThemeWrite]") {
    const std::string before = "(ned/set-tab-width 4)\n(ned/set-auto-revert true)\n";
    REQUIRE(WithSetThemeCall(before, "nord") ==
            "(ned/set-tab-width 4)\n(ned/set-auto-revert true)\n(ned/set-theme \"nord\")\n");
}

TEST_CASE("A file with no trailing newline gets one, and keeps its last line", "[InitFileThemeWrite]") {
    REQUIRE(WithSetThemeCall("(ned/set-tab-width 4)", "nord") == "(ned/set-tab-width 4)\n(ned/set-theme \"nord\")\n");
}

TEST_CASE("The last simple call wins, since that is the one that takes effect", "[InitFileThemeWrite]") {
    const std::string before = "(ned/set-theme \"dark\")\n(ned/set-tab-width 4)\n(ned/set-theme \"light\")\n";
    // Only the second is rewritten -- a later ned/set-theme overwrites an
    // earlier one, so rewriting the first would change nothing visible.
    REQUIRE(WithSetThemeCall(before, "nord") ==
            "(ned/set-theme \"dark\")\n(ned/set-tab-width 4)\n(ned/set-theme \"nord\")\n");
}

TEST_CASE("Indentation on an existing call is preserved", "[InitFileThemeWrite]") {
    REQUIRE(WithSetThemeCall("  (ned/set-theme \"dark\")\n", "nord") == "  (ned/set-theme \"nord\")\n");
}

TEST_CASE("A call ned did not write is left alone, and a plain one is appended", "[InitFileThemeWrite]") {
    // A computed call cannot be rewritten without understanding Janet, and
    // guessing at somebody's config is worse than adding a line. Appending is
    // correct rather than merely safe: the later call is the one that wins.
    const std::string before = "(ned/set-theme (if night \"nord\" \"gruvbox-light\"))\n";
    REQUIRE(WithSetThemeCall(before, "dracula") == before + "(ned/set-theme \"dracula\")\n");

    // Same for a call sharing its line with anything else.
    const std::string inline_ = "(do (ned/set-theme \"dark\") (ned/set-tab-width 2))\n";
    REQUIRE(WithSetThemeCall(inline_, "nord") == inline_ + "(ned/set-theme \"nord\")\n");
}

TEST_CASE("Comments and blank lines survive untouched", "[InitFileThemeWrite]") {
    const std::string before = "# my config\n\n(ned/set-theme \"dark\")\n\n# the end\n";
    REQUIRE(WithSetThemeCall(before, "nord") == "# my config\n\n(ned/set-theme \"nord\")\n\n# the end\n");
}
