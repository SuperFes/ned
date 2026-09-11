//
// The pure half of the rename review (Editor/RenameReview.h): classifying a
// hit against highlight spans, finding the comment/string occurrences a
// rename didn't offer, and laying rows out with their two proposed bodies.
// No Buffer, no Parser, no Screen -- every span here is hand-built, which is
// the point of the module being pure.
//

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/RenameReview.h"

using ned::editor::HighlightSpan;
using ned::editor::SyntaxClass;
using ned::editor::rename::BuildReviewExcerpts;
using ned::editor::rename::ClassifyHit;
using ned::editor::rename::FileRenameHits;
using ned::editor::rename::FindExtraCandidates;
using ned::editor::rename::HitKind;
using ned::editor::rename::RenameHit;
using ned::editor::rename::ReviewExcerpt;

namespace {

HighlightSpan Span(std::size_t start, std::size_t end, SyntaxClass cls) {
    return HighlightSpan{start, end, cls, ned::editor::kNoCapture};
}

// Every hit of `name` in `text`, as a reference -- the shape a server's
// edit list arrives in once resolved to byte offsets.
std::vector<RenameHit> ReferencesTo(const std::string& text, const std::string& name) {
    std::vector<RenameHit> hits;
    for (std::size_t at = text.find(name); at != std::string::npos; at = text.find(name, at + 1)) {
        hits.push_back(RenameHit{at, at + name.size(), HitKind::Reference});
    }
    return hits;
}

} // namespace

TEST_CASE("ClassifyHit reads the last covering span", "[RenameReview]") {
    const std::vector<HighlightSpan> spans{Span(0, 20, SyntaxClass::Comment), Span(5, 10, SyntaxClass::String)};

    SECTION("an uncovered offset is code") {
        REQUIRE(ClassifyHit(spans, 25) == HitKind::Reference);
    }
    SECTION("a single covering span decides") {
        REQUIRE(ClassifyHit(spans, 2) == HitKind::Comment);
    }
    SECTION("the later of two overlapping spans wins, per Mode.h's rule") {
        REQUIRE(ClassifyHit(spans, 7) == HitKind::String);
    }
    SECTION("a doc comment is a comment and an include path is a string") {
        REQUIRE(ClassifyHit({Span(0, 4, SyntaxClass::DocComment)}, 1) == HitKind::Comment);
        REQUIRE(ClassifyHit({Span(0, 4, SyntaxClass::IncludePath)}, 1) == HitKind::String);
    }
    SECTION("no spans at all means every hit is a reference") {
        REQUIRE(ClassifyHit({}, 0) == HitKind::Reference);
    }
}

TEST_CASE("FindExtraCandidates keeps only uncovered comment and string hits", "[RenameReview]") {
    //           0         1         2         3         4
    //           0123456789012345678901234567890123456789012345
    const std::string text         = "int total = 1; // total so far\nchar* s = \"total\";\n";
    const std::size_t commentStart = text.find("// total");
    const std::size_t stringStart  = text.find("\"total\"");

    const std::vector<HighlightSpan> spans{Span(commentStart, text.find('\n'), SyntaxClass::Comment),
                                           Span(stringStart, stringStart + 7, SyntaxClass::String)};

    const std::vector<RenameHit> covered{RenameHit{4, 9, HitKind::Reference}}; // the declaration itself

    const std::vector<RenameHit> extra = FindExtraCandidates(text, spans, "total", covered);
    REQUIRE(extra.size() == 2);
    CHECK(extra[0].kind == HitKind::Comment);
    CHECK(extra[1].kind == HitKind::String);
    CHECK(text.substr(extra[0].startByte, extra[0].endByte - extra[0].startByte) == "total");
    CHECK(text.substr(extra[1].startByte, extra[1].endByte - extra[1].startByte) == "total");

    SECTION("a code occurrence the edit set already covers is never re-offered") {
        for (const RenameHit& hit : extra) {
            CHECK(hit.startByte != 4);
        }
    }
}

TEST_CASE("FindExtraCandidates respects identifier boundaries", "[RenameReview]") {
    const std::string                text = "// total totals subtotal total_x _total\n";
    const std::vector<HighlightSpan> spans{Span(0, text.size(), SyntaxClass::Comment)};

    const std::vector<RenameHit> extra = FindExtraCandidates(text, spans, "total", {});
    REQUIRE(extra.size() == 1);
    CHECK(extra[0].startByte == 3); // only the standalone word
}

TEST_CASE("FindExtraCandidates treats a name's own punctuation as part of a word", "[RenameReview]") {
    // A Clojure-shaped name: without the name's own '-' counting as an
    // identifier byte, "foo" inside "foo-bar" would look like a whole word.
    const std::string                text = "; foo-bar and foo-barbaz\n";
    const std::vector<HighlightSpan> spans{Span(0, text.size(), SyntaxClass::Comment)};

    const std::vector<RenameHit> extra = FindExtraCandidates(text, spans, "foo-bar", {});
    REQUIRE(extra.size() == 1);
    CHECK(extra[0].startByte == 2);
}

TEST_CASE("FindExtraCandidates ignores code occurrences", "[RenameReview]") {
    const std::string text = "total = total + 1;\n";
    // No comment/string spans at all: every uncovered occurrence is code,
    // and a rename that didn't ask for it is not this module's business.
    REQUIRE(FindExtraCandidates(text, {}, "total", {}).empty());
}

TEST_CASE("BuildReviewExcerpts proposes both bodies per row", "[RenameReview]") {
    const std::string text         = "int total = 1; // total so far\n";
    const std::size_t commentStart = text.find("// total");

    FileRenameHits file;
    file.file        = "/proj/Source/Foo.cpp";
    file.displayPath = "Source/Foo.cpp";
    file.text        = text;
    file.hits        = {RenameHit{4, 9, HitKind::Reference},
                        RenameHit{commentStart + 3, commentStart + 8, HitKind::Comment}};

    const std::vector<ReviewExcerpt> rows = BuildReviewExcerpts({file}, "sum");
    REQUIRE(rows.size() == 1);

    // Both hits share a line, so they share a row -- and the row's body is
    // the ORIGINAL line, which is what makes an unedited excerpt commit
    // nothing.
    CHECK(rows[0].source.bodyText == "int total = 1; // total so far");
    CHECK(rows[0].referencesOnlyBody == "int sum = 1; // total so far");
    CHECK(rows[0].allHitsBody == "int sum = 1; // sum so far");
    CHECK(rows[0].hasReference);
    CHECK(rows[0].hasRisky);
    CHECK(rows[0].source.editable);
    CHECK(rows[0].source.sourceStartLine == 1);
    CHECK(rows[0].source.headerText == "▸ Source/Foo.cpp:1  [+comment]");
}

TEST_CASE("BuildReviewExcerpts groups rows by risk, then keeps file order", "[RenameReview]") {
    const std::string text     = "// total\nint total = 1;\nchar* s = \"total\";\nreturn total;\n";
    const std::size_t stringAt = text.find("\"total\"") + 1;

    FileRenameHits file;
    file.file        = "/proj/a.cpp";
    file.displayPath = "a.cpp";
    file.text        = text;
    file.hits        = {RenameHit{3, 8, HitKind::Comment},
                        RenameHit{13, 18, HitKind::Reference},
                        RenameHit{stringAt, stringAt + 5, HitKind::String},
                        RenameHit{text.find("return total") + 7, text.find("return total") + 12, HitKind::Reference}};

    const std::vector<ReviewExcerpt> rows = BuildReviewExcerpts({file}, "sum");
    REQUIRE(rows.size() == 4);

    // References first (in line order), then the comment row, then the string row.
    CHECK(rows[0].source.sourceStartLine == 2);
    CHECK(rows[1].source.sourceStartLine == 4);
    CHECK(rows[2].source.sourceStartLine == 1);
    CHECK(rows[3].source.sourceStartLine == 3);

    CHECK(rows[2].source.headerText == "▸ a.cpp:1  [comment]");
    CHECK(rows[3].source.headerText == "▸ a.cpp:3  [string]");

    SECTION("a risky-only row proposes nothing until it is opted into") {
        CHECK(rows[2].referencesOnlyBody == rows[2].source.bodyText);
        CHECK(rows[2].allHitsBody == "// sum");
        CHECK_FALSE(rows[2].hasReference);
    }
    SECTION("a pure-reference row carries no tag and one proposal") {
        CHECK(rows[0].source.headerText == "▸ a.cpp:2");
        CHECK(rows[0].referencesOnlyBody == "int sum = 1;");
        CHECK(rows[0].allHitsBody == rows[0].referencesOnlyBody);
    }
}

TEST_CASE("BuildReviewExcerpts merges several hits on one line into one row", "[RenameReview]") {
    const std::string text = "total = total + total;\n";

    FileRenameHits file;
    file.file        = "/proj/a.cpp";
    file.displayPath = "a.cpp";
    file.text        = text;
    file.hits        = ReferencesTo(text, "total");
    REQUIRE(file.hits.size() == 3);

    const std::vector<ReviewExcerpt> rows = BuildReviewExcerpts({file}, "sum");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].referencesOnlyBody == "sum = sum + sum;");
}

TEST_CASE("BuildReviewExcerpts drops a hit whose offsets don't fit the text", "[RenameReview]") {
    FileRenameHits file;
    file.file        = "/proj/a.cpp";
    file.displayPath = "a.cpp";
    file.text        = "short\n";
    file.hits        = {RenameHit{100, 105, HitKind::Reference}};

    CHECK(BuildReviewExcerpts({file}, "sum").empty());
}

TEST_CASE("BuildReviewExcerpts keeps cross-file order within a rank group", "[RenameReview]") {
    FileRenameHits first;
    first.file        = "/proj/a.cpp";
    first.displayPath = "a.cpp";
    first.text        = "total;\n";
    first.hits        = {RenameHit{0, 5, HitKind::Reference}};

    FileRenameHits second;
    second.file        = "/proj/b.cpp";
    second.displayPath = "b.cpp";
    second.text        = "// total\ntotal;\n";
    second.hits        = {RenameHit{3, 8, HitKind::Comment}, RenameHit{9, 14, HitKind::Reference}};

    const std::vector<ReviewExcerpt> rows = BuildReviewExcerpts({first, second}, "sum");
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].source.sourcePath == std::filesystem::path("/proj/a.cpp"));
    CHECK(rows[1].source.sourcePath == std::filesystem::path("/proj/b.cpp"));
    CHECK(rows[1].source.sourceStartLine == 2);
    CHECK(rows[2].source.sourceStartLine == 1); // the comment row, demoted below both references
}
