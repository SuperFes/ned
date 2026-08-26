#include <catch2/catch_test_macros.hpp>

#include "Text/LineEnding.h"

using namespace ned::text;

TEST_CASE("DetectLineEnding reports LF for pure-LF and newline-free content", "[LineEnding]") {
    CHECK(DetectLineEnding("one\ntwo\nthree\n") == LineEnding::LF);
    CHECK(DetectLineEnding("no newlines at all") == LineEnding::LF);
    CHECK(DetectLineEnding("") == LineEnding::LF);
}

TEST_CASE("DetectLineEnding reports CRLF for pure-CRLF content", "[LineEnding]") {
    CHECK(DetectLineEnding("one\r\ntwo\r\nthree\r\n") == LineEnding::CRLF);
}

TEST_CASE("DetectLineEnding reports CR for pure-CR (classic Mac) content", "[LineEnding]") {
    CHECK(DetectLineEnding("one\rtwo\rthree\r") == LineEnding::CR);
}

TEST_CASE("DetectLineEnding is a majority vote over mixed content", "[LineEnding]") {
    CHECK(DetectLineEnding("a\r\nb\r\nc\nd\r\n") == LineEnding::CRLF); // 3 CRLF vs 1 lone LF
    CHECK(DetectLineEnding("a\nb\nc\nd\r\n") == LineEnding::LF);       // 3 lone LF vs 1 CRLF
}

TEST_CASE("HasCarriageReturn", "[LineEnding]") {
    CHECK_FALSE(HasCarriageReturn("no carriage returns\nhere\n"));
    CHECK(HasCarriageReturn("has one\r\n"));
    CHECK(HasCarriageReturn("lone cr\rhere"));
}

TEST_CASE("NormalizeToLf collapses CRLF pairs to a single LF", "[LineEnding]") {
    CHECK(NormalizeToLf("one\r\ntwo\r\nthree\r\n") == "one\ntwo\nthree\n");
}

TEST_CASE("NormalizeToLf turns lone CR into LF", "[LineEnding]") {
    CHECK(NormalizeToLf("one\rtwo\rthree") == "one\ntwo\nthree");
}

TEST_CASE("NormalizeToLf leaves already-LF content unchanged", "[LineEnding]") {
    CHECK(NormalizeToLf("one\ntwo\nthree\n") == "one\ntwo\nthree\n");
}

TEST_CASE("NormalizeToLf is idempotent on mixed content", "[LineEnding]") {
    const std::string mixed = "a\r\nb\rc\nd";
    const std::string once  = NormalizeToLf(mixed);
    CHECK(once == "a\nb\nc\nd");
    CHECK(NormalizeToLf(once) == once);
}

TEST_CASE("ApplyLineEnding is a no-op for LF", "[LineEnding]") {
    CHECK(ApplyLineEnding("one\ntwo\n", LineEnding::LF) == "one\ntwo\n");
}

TEST_CASE("ApplyLineEnding expands LF to CRLF", "[LineEnding]") {
    CHECK(ApplyLineEnding("one\ntwo\nthree\n", LineEnding::CRLF) == "one\r\ntwo\r\nthree\r\n");
}

TEST_CASE("ApplyLineEnding expands LF to CR", "[LineEnding]") {
    CHECK(ApplyLineEnding("one\ntwo\nthree\n", LineEnding::CR) == "one\rtwo\rthree\r");
}

TEST_CASE("Detect -> normalize -> apply round-trips a CRLF file byte-for-byte", "[LineEnding]") {
    const std::string original = "line one\r\nline two\r\nline three\r\n";
    const LineEnding  detected = DetectLineEnding(original);
    REQUIRE(detected == LineEnding::CRLF);
    const std::string normalized = NormalizeToLf(original);
    CHECK(ApplyLineEnding(normalized, detected) == original);
}

TEST_CASE("LineEndingName", "[LineEnding]") {
    CHECK(std::string(LineEndingName(LineEnding::LF)) == "LF");
    CHECK(std::string(LineEndingName(LineEnding::CRLF)) == "CRLF");
    CHECK(std::string(LineEndingName(LineEnding::CR)) == "CR");
}
