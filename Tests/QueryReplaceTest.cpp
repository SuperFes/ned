#include <catch2/catch_test_macros.hpp>

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
