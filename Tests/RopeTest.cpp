#include <catch2/catch_test_macros.hpp>

#include <random>
#include <string>

#include "Text/Rope.h"

using ned::text::Rope;

TEST_CASE("Empty rope", "[Rope]") {
    const Rope rope;

    REQUIRE(rope.Empty());
    REQUIRE(rope.ByteLength() == 0);
    REQUIRE(rope.CodepointLength() == 0);
    REQUIRE(rope.LineCount() == 1);
    REQUIRE(rope.ToString().empty());
}

TEST_CASE("Rope constructed from ASCII text", "[Rope]") {
    const Rope rope("hello");

    REQUIRE_FALSE(rope.Empty());
    REQUIRE(rope.ByteLength() == 5);
    REQUIRE(rope.CodepointLength() == 5);
    REQUIRE(rope.ToString() == "hello");
}

TEST_CASE("Rope insert and erase are non-mutating", "[Rope]") {
    const Rope original("hello world");

    const Rope inserted = original.Inserted(5, ",");
    REQUIRE(inserted.ToString() == "hello, world");
    REQUIRE(original.ToString() == "hello world"); // unchanged

    const Rope erased = original.Erased(5, 6);
    REQUIRE(erased.ToString() == "hello");
    REQUIRE(original.ToString() == "hello world"); // unchanged
}

TEST_CASE("Rope insert at boundaries", "[Rope]") {
    const Rope rope("world");

    REQUIRE(rope.Inserted(0, "hello ").ToString() == "hello world");
    REQUIRE(rope.Inserted(5, "!").ToString() == "world!");
}

TEST_CASE("Rope multi-byte UTF-8 counts bytes vs codepoints correctly", "[Rope]") {
    // "h", "e", U+00E9 (2 bytes), "l", "l", "o" -> "héllo" is not what we want;
    // use an explicit accented character: "h" + U+00E9 + "llo".
    const std::string text  = "h\xC3\xA9llo"; // "héllo"
    const Rope         rope(text);

    REQUIRE(rope.ByteLength() == 6);
    REQUIRE(rope.CodepointLength() == 5);
    REQUIRE(rope.ToString() == text);

    const auto decoded = rope.CodepointAt(1);
    REQUIRE(decoded.codepoint == static_cast<char32_t>(0x00E9));
    REQUIRE(decoded.byteLength == 2);

    REQUIRE(rope.NextCodepointBoundary(1) == 3);
    REQUIRE(rope.PreviousCodepointBoundary(3) == 1);
}

TEST_CASE("Rope line counting", "[Rope]") {
    const Rope rope("a\nb\nc");

    REQUIRE(rope.LineCount() == 3);
    REQUIRE(rope.ByteOffsetToLine(0) == 0);
    REQUIRE(rope.ByteOffsetToLine(2) == 1);
    REQUIRE(rope.ByteOffsetToLine(4) == 2);
    REQUIRE(rope.LineToByteOffset(0) == 0);
    REQUIRE(rope.LineToByteOffset(1) == 2);
    REQUIRE(rope.LineToByteOffset(2) == 4);
}

TEST_CASE("Rope codepoint/byte offset conversions", "[Rope]") {
    const std::string text = "h\xC3\xA9llo"; // "héllo"
    const Rope         rope(text);

    REQUIRE(rope.ByteOffsetToCodepointOffset(0) == 0);
    REQUIRE(rope.ByteOffsetToCodepointOffset(1) == 1);
    REQUIRE(rope.ByteOffsetToCodepointOffset(3) == 2);
    REQUIRE(rope.ByteOffsetToCodepointOffset(6) == 5);

    REQUIRE(rope.CodepointOffsetToByteOffset(0) == 0);
    REQUIRE(rope.CodepointOffsetToByteOffset(1) == 1);
    REQUIRE(rope.CodepointOffsetToByteOffset(2) == 3);
    REQUIRE(rope.CodepointOffsetToByteOffset(5) == 6);
}

TEST_CASE("Rope survives random edits across chunk boundaries", "[Rope]") {
    // kChunkSize is 512; drive well past several chunks and rebalance thresholds.
    std::string reference;
    Rope        rope;

    std::mt19937                       rng(1234); // fixed seed: deterministic test
    std::uniform_int_distribution<int> opDist(0, 1);
    std::uniform_int_distribution<int> lenDist(1, 40);

    for (int iteration = 0; iteration < 2000; ++iteration) {
        if (reference.empty() || opDist(rng) == 0) {
            std::uniform_int_distribution<std::size_t> posDist(0, reference.size());
            const std::size_t                          pos = posDist(rng);
            const int                                  len = lenDist(rng);

            std::string chunk;
            chunk.reserve(static_cast<std::size_t>(len));
            for (int i = 0; i < len; ++i) {
                chunk.push_back(static_cast<char>('a' + (i % 26)));
            }

            reference.insert(pos, chunk);
            rope = rope.Inserted(pos, chunk);
        } else {
            std::uniform_int_distribution<std::size_t> posDist(0, reference.size());
            const std::size_t                          pos    = posDist(rng);
            const std::size_t                          maxLen = reference.size() - pos;
            std::uniform_int_distribution<std::size_t> eraseLenDist(0, maxLen);
            const std::size_t                          len = eraseLenDist(rng);

            reference.erase(pos, len);
            rope = rope.Erased(pos, len);
        }

        REQUIRE(rope.ByteLength() == reference.size());
        REQUIRE(rope.ToString() == reference);
    }
}
