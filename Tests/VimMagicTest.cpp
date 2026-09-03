#include <catch2/catch_test_macros.hpp>

#include "Editor/RegexPattern.h"
#include "Editor/Vim/VimMagic.h"

using ned::editor::RegexPattern;
using ned::editor::vim::TranslateVimMagicPattern;
using ned::editor::vim::TranslateVimMagicReplacement;

TEST_CASE("Characters already special without a backslash in both flavors pass through unchanged", "[VimMagic]") {
    REQUIRE(TranslateVimMagicPattern("foo.*bar") == "foo.*bar");
    REQUIRE(TranslateVimMagicPattern("^start") == "^start");
    REQUIRE(TranslateVimMagicPattern("end$") == "end$");
    REQUIRE(TranslateVimMagicPattern("[abc]") == "[abc]");
}

TEST_CASE("Backslash-escaped grouping/alternation/quantifier atoms are unescaped", "[VimMagic]") {
    REQUIRE(TranslateVimMagicPattern("\\(foo\\)") == "(foo)");
    REQUIRE(TranslateVimMagicPattern("foo\\|bar") == "foo|bar");
    REQUIRE(TranslateVimMagicPattern("a\\+") == "a+");
    REQUIRE(TranslateVimMagicPattern("a\\?") == "a?");
    REQUIRE(TranslateVimMagicPattern("a\\=") == "a?"); // \= is vim's own \? synonym
}

TEST_CASE("Bare grouping/alternation/quantifier characters are escaped to stay literal", "[VimMagic]") {
    REQUIRE(TranslateVimMagicPattern("(foo)") == "\\(foo\\)");
    REQUIRE(TranslateVimMagicPattern("a|b") == "a\\|b");
    REQUIRE(TranslateVimMagicPattern("a+b") == "a\\+b");
    REQUIRE(TranslateVimMagicPattern("a?b") == "a\\?b");
    REQUIRE(TranslateVimMagicPattern("{}") == "\\{\\}");
}

TEST_CASE("Word boundaries translate to \\b", "[VimMagic]") {
    REQUIRE(TranslateVimMagicPattern("\\<foo\\>") == "\\bfoo\\b");
}

TEST_CASE("Interval quantifiers translate braces and keep the body verbatim", "[VimMagic]") {
    // Real vim's own \{...} syntax is asymmetric: only the opening brace is
    // backslash-escaped, the closer is a bare '}' (:help /\{).
    REQUIRE(TranslateVimMagicPattern("a\\{3,5}") == "a{3,5}");
    REQUIRE(TranslateVimMagicPattern("a\\{3}") == "a{3}");
    REQUIRE(TranslateVimMagicPattern("a\\{,5}") == "a{,5}");
}

TEST_CASE("Non-greedy interval quantifiers translate the leading - to a trailing ?", "[VimMagic]") {
    REQUIRE(TranslateVimMagicPattern("a\\{-3,5}") == "a{3,5}?");
    REQUIRE(TranslateVimMagicPattern("a\\{-}") == "a*?");
}

TEST_CASE("An empty-body interval translates to a plain star", "[VimMagic]") {
    REQUIRE(TranslateVimMagicPattern("a\\{}") == "a*");
}

TEST_CASE("Non-capturing groups and \\zs translate", "[VimMagic]") {
    REQUIRE(TranslateVimMagicPattern("\\%(foo\\)") == "(?:foo)");
    REQUIRE(TranslateVimMagicPattern("foo\\zsbar") == "foo\\Kbar");
}

TEST_CASE("A leading very-magic prefix strips and passes the remainder through unmodified", "[VimMagic]") {
    REQUIRE(TranslateVimMagicPattern("\\v(foo|bar)+") == "(foo|bar)+");
    REQUIRE(TranslateVimMagicPattern("\\vfoo.*") == "foo.*");
}

TEST_CASE("Grouping/quantifier characters inside a bracket expression are never translated", "[VimMagic]") {
    REQUIRE(TranslateVimMagicPattern("[(){}|+?]") == "[(){}|+?]");
    // A leading ']' (or right after '^') is a literal member, not the class closer.
    REQUIRE(TranslateVimMagicPattern("[]()]") == "[]()]");
    REQUIRE(TranslateVimMagicPattern("[^]()]") == "[^]()]");
}

TEST_CASE("An unrecognized backslash escape passes through unchanged", "[VimMagic]") {
    REQUIRE(TranslateVimMagicPattern("\\d\\s\\w") == "\\d\\s\\w"); // already correct in PCRE2 too
    REQUIRE(TranslateVimMagicPattern("\\ze") == "\\ze");           // documented cut, no PCRE2 equivalent
}

TEST_CASE("A translated vim pattern actually compiles and matches via RegexPattern", "[VimMagic]") {
    const RegexPattern re(TranslateVimMagicPattern("\\(foo\\|bar\\)\\+"));
    const auto          match = re.Search("xx foobar yy", 0);
    REQUIRE(match.has_value());
    REQUIRE(match->start == 3);
    REQUIRE(match->end == 9);
}

TEST_CASE("A translated interval pattern actually compiles and matches via RegexPattern", "[VimMagic]") {
    const RegexPattern re(TranslateVimMagicPattern("a\\{2,3}"));
    const auto          match = re.Search("baaaab", 0);
    REQUIRE(match.has_value());
    REQUIRE(match->end - match->start == 3); // greedy -- takes all 3 available
}

TEST_CASE("Replacement backreferences translate from vim's \\N form to FormatReplacement's $N", "[VimMagic]") {
    REQUIRE(TranslateVimMagicReplacement("\\2\\1") == "$2$1");
    REQUIRE(TranslateVimMagicReplacement("\\0") == "$0");
}

TEST_CASE("Bare & is the whole match; \\& is a literal ampersand", "[VimMagic]") {
    REQUIRE(TranslateVimMagicReplacement("[&]") == "[$&]");
    REQUIRE(TranslateVimMagicReplacement("[\\&]") == "[&]");
}

TEST_CASE("A literal dollar sign is escaped to stay literal for FormatReplacement", "[VimMagic]") {
    REQUIRE(TranslateVimMagicReplacement("$5") == "$$5");
}

TEST_CASE("A doubled backslash is one literal backslash", "[VimMagic]") {
    REQUIRE(TranslateVimMagicReplacement("a\\\\b") == "a\\b");
}

TEST_CASE("An unrecognized replacement escape passes through unchanged", "[VimMagic]") {
    REQUIRE(TranslateVimMagicReplacement("\\u\\1") == "\\u$1"); // \u (case conversion) is a documented cut
}
