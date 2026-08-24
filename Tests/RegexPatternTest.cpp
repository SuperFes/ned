#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "Editor/RegexPattern.h"

using ned::editor::RegexMatch;
using ned::editor::RegexPattern;
using ned::editor::RegexPatternError;

TEST_CASE("Invalid pattern throws RegexPatternError carrying PCRE2's diagnostic", "[RegexPattern]") {
    try {
        const RegexPattern pattern("(unclosed");
        FAIL("expected RegexPatternError");
    }
    catch (const RegexPatternError& e) {
        REQUIRE(std::string(e.what()).find("offset") != std::string::npos);
    }
}

TEST_CASE("Search finds a match with capture group spans", "[RegexPattern]") {
    const RegexPattern              pattern("(\\w+)@(\\w+)");
    const std::string               subject = "mail alice@example today";
    const std::optional<RegexMatch> match   = pattern.Search(subject);

    REQUIRE(match.has_value());
    REQUIRE(subject.substr(match->start, match->end - match->start) == "alice@example");
    REQUIRE(match->groups.size() == 3);
    REQUIRE(match->groups[1].matched);
    REQUIRE(subject.substr(match->groups[1].start, match->groups[1].end - match->groups[1].start) == "alice");
    REQUIRE(subject.substr(match->groups[2].start, match->groups[2].end - match->groups[2].start) == "example");
}

TEST_CASE("Search returns nullopt for no match and for a start offset past the end", "[RegexPattern]") {
    const RegexPattern pattern("zzz");
    REQUIRE_FALSE(pattern.Search("hello").has_value());
    REQUIRE_FALSE(pattern.Search("zzz", 10).has_value());
}

TEST_CASE("An unmatched optional group is reported unmatched, not garbage", "[RegexPattern]") {
    const RegexPattern              pattern("(a)|(b)");
    const std::optional<RegexMatch> match = pattern.Search("b");

    REQUIRE(match.has_value());
    REQUIRE(match->groups.size() == 3);
    REQUIRE_FALSE(match->groups[1].matched);
    REQUIRE(match->groups[2].matched);
}

TEST_CASE("^ and $ anchor at line boundaries, not just subject ends", "[RegexPattern]") {
    const RegexPattern pattern("^cat$");
    const std::string  subject = "catnip\ncat\nbobcat\n";

    const std::optional<RegexMatch> match = pattern.Search(subject);
    REQUIRE(match.has_value());
    REQUIRE(match->start == 7); // the standalone "cat" line, not the "catnip" prefix or "bobcat" suffix
    REQUIRE(match->end == 10);
}

TEST_CASE("Search from an offset still sees preceding context for ^, \\b, and lookbehind", "[RegexPattern]") {
    // ^ must not match mid-line just because the search starts there -- the
    // exact bug QueryReplace.h used to document as a known limitation.
    const RegexPattern              anchored("^X");
    const std::string               subject   = "aXb\nXc\n";
    const std::optional<RegexMatch> lineStart = anchored.Search(subject, 1); // starts exactly on the mid-line X
    REQUIRE(lineStart.has_value());
    REQUIRE(lineStart->start == 4); // the X that genuinely starts a line

    // \b at the start offset must consider the character before it.
    const RegexPattern              boundary("\\bcat");
    const std::string               wordSubject = "bobcat cat";
    const std::optional<RegexMatch> word        = boundary.Search(wordSubject, 3); // offset 3 is inside "bobcat"
    REQUIRE(word.has_value());
    REQUIRE(word->start == 7); // the standalone "cat", not "cat" inside "bobcat"

    // Lookbehind across the start offset.
    const RegexPattern              behind("(?<=blue )sky");
    const std::string               skySubject = "blue sky";
    const std::optional<RegexMatch> sky        = behind.Search(skySubject, 5);
    REQUIRE(sky.has_value());
    REQUIRE(sky->start == 5);
}

TEST_CASE("Lookahead, backreferences, and Unicode classes are supported", "[RegexPattern]") {
    REQUIRE(RegexPattern("cat(?=s)").Search("cat cats")->start == 4);
    REQUIRE(RegexPattern("(\\w)\\1").Search("abba")->start == 1); // "bb" via backreference
    REQUIRE(RegexPattern("\\p{L}+").Search("123 héllo")->start == 4);
    // UCP: \w is Unicode-aware, so a fully accented word matches whole.
    const std::optional<RegexMatch> word = RegexPattern("\\w+").Search("état");
    REQUIRE(word.has_value());
    REQUIRE(word->end == 5); // é is two bytes
}

TEST_CASE("Invalid UTF-8 in the subject is tolerated, not an error", "[RegexPattern]") {
    const RegexPattern              pattern("cat");
    std::string                     subject = "a\xFF\xFE cat";
    const std::optional<RegexMatch> match   = pattern.Search(subject);
    REQUIRE(match.has_value());
    REQUIRE(subject.substr(match->start, match->end - match->start) == "cat");
}

TEST_CASE("FormatReplacement expands the ECMAScript reference set", "[RegexPattern]") {
    const RegexPattern              pattern("(\\w+)@(\\w+)");
    const std::string               subject = "to alice@example!";
    const std::optional<RegexMatch> match   = pattern.Search(subject);
    REQUIRE(match.has_value());

    REQUIRE(pattern.FormatReplacement(subject, *match, "$2@$1") == "example@alice");
    REQUIRE(pattern.FormatReplacement(subject, *match, "${2}@${1}") == "example@alice");
    REQUIRE(pattern.FormatReplacement(subject, *match, "[$&]") == "[alice@example]");
    REQUIRE(pattern.FormatReplacement(subject, *match, "[$0]") == "[alice@example]");
    REQUIRE(pattern.FormatReplacement(subject, *match, "$$1") == "$1");
    REQUIRE(pattern.FormatReplacement(subject, *match, "$`") == "to ");
    REQUIRE(pattern.FormatReplacement(subject, *match, "$'") == "!");
    REQUIRE(pattern.FormatReplacement(subject, *match, "plain") == "plain");
}

TEST_CASE("FormatReplacement handles named groups and passes unknowns through literally", "[RegexPattern]") {
    const RegexPattern              pattern("(?<user>\\w+)@(?<host>\\w+)");
    const std::string               subject = "alice@example";
    const std::optional<RegexMatch> match   = pattern.Search(subject);
    REQUIRE(match.has_value());

    REQUIRE(pattern.FormatReplacement(subject, *match, "${host}/${user}") == "example/alice");
    REQUIRE(pattern.FormatReplacement(subject, *match, "${nosuch}") == "${nosuch}");
}

TEST_CASE("FormatReplacement group-number edge cases", "[RegexPattern]") {
    const RegexPattern              pattern("(a)(b)");
    const std::string               subject = "ab";
    const std::optional<RegexMatch> match   = pattern.Search(subject);
    REQUIRE(match.has_value());

    // $12 has no group 12: falls back to $1 then literal "2" (std::regex's own rule).
    REQUIRE(pattern.FormatReplacement(subject, *match, "$12") == "a2");
    // $9 exceeds the group count even as one digit: the '$' is literal.
    REQUIRE(pattern.FormatReplacement(subject, *match, "$9") == "$9");
    // A trailing lone '$' is literal.
    REQUIRE(pattern.FormatReplacement(subject, *match, "x$") == "x$");

    // An unmatched-but-valid group expands to nothing.
    const RegexPattern              alt("(a)|(b)");
    const std::optional<RegexMatch> altMatch = alt.Search("b");
    REQUIRE(altMatch.has_value());
    REQUIRE(alt.FormatReplacement("b", *altMatch, "[$1][$2]") == "[][b]");
}

TEST_CASE("ReplaceAll replaces every occurrence with a count", "[RegexPattern]") {
    const RegexPattern                   pattern("cat");
    const RegexPattern::ReplaceAllResult result = pattern.ReplaceAll("cat scat cat", "dog");
    REQUIRE(result.text == "dog sdog dog");
    REQUIRE(result.count == 3);
}

TEST_CASE("ReplaceAll with ^ rewrites every line start", "[RegexPattern]") {
    const RegexPattern                   pattern("^");
    const RegexPattern::ReplaceAllResult result = pattern.ReplaceAll("one\ntwo", "> ");
    REQUIRE(result.text == "> one\n> two");
    REQUIRE(result.count == 2);
}

TEST_CASE("ReplaceAll terminates on zero-width matches, including over multibyte text", "[RegexPattern]") {
    const RegexPattern                   pattern("x*");
    const RegexPattern::ReplaceAllResult ascii = pattern.ReplaceAll("ab", "");
    REQUIRE(ascii.text == "ab");
    REQUIRE(ascii.count == 3); // empty match at offsets 0, 1, 2

    const RegexPattern::ReplaceAllResult multibyte = pattern.ReplaceAll("éé", "-");
    REQUIRE(multibyte.text == "-é-é-");
    REQUIRE(multibyte.count == 3); // codepoint positions, never mid-é
}

TEST_CASE("ReplaceAll on an empty subject with an empty-matching pattern", "[RegexPattern]") {
    const RegexPattern                   pattern("x*");
    const RegexPattern::ReplaceAllResult result = pattern.ReplaceAll("", "y");
    REQUIRE(result.text == "y");
    REQUIRE(result.count == 1);
}
