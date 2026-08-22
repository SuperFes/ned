#include <catch2/catch_test_macros.hpp>

#include "Editor/PrefixArgument.h"

using ned::editor::ParseKeyChord;
using ned::editor::PrefixArgumentReader;

TEST_CASE("A bare C-u resolves to 4 and a terminating key is not consumed", "[PrefixArgument]") {
    PrefixArgumentReader reader;
    REQUIRE(reader.Value() == 4);

    const auto outcome = reader.HandleKey(ParseKeyChord("x"));
    REQUIRE(outcome == PrefixArgumentReader::Outcome::Terminate);
    REQUIRE(reader.Value() == 4);
}

TEST_CASE("Repeated bare C-u multiplies by 4 each time", "[PrefixArgument]") {
    PrefixArgumentReader reader;
    REQUIRE(reader.HandleKey(ParseKeyChord("C-u")) == PrefixArgumentReader::Outcome::Continue);
    REQUIRE(reader.Value() == 16);

    REQUIRE(reader.HandleKey(ParseKeyChord("C-u")) == PrefixArgumentReader::Outcome::Continue);
    REQUIRE(reader.Value() == 64);
}

TEST_CASE("Explicit digits accumulate as a literal value", "[PrefixArgument]") {
    PrefixArgumentReader reader;
    REQUIRE(reader.HandleKey(ParseKeyChord("4")) == PrefixArgumentReader::Outcome::Continue);
    REQUIRE(reader.HandleKey(ParseKeyChord("2")) == PrefixArgumentReader::Outcome::Continue);
    REQUIRE(reader.Value() == 42);
}

TEST_CASE("A bare minus with no digits resolves to -1", "[PrefixArgument]") {
    PrefixArgumentReader reader;
    REQUIRE(reader.HandleKey(ParseKeyChord("-")) == PrefixArgumentReader::Outcome::Continue);
    REQUIRE(reader.Value() == -1);
}

TEST_CASE("A minus followed by digits negates the value", "[PrefixArgument]") {
    PrefixArgumentReader reader;
    REQUIRE(reader.HandleKey(ParseKeyChord("-")) == PrefixArgumentReader::Outcome::Continue);
    REQUIRE(reader.HandleKey(ParseKeyChord("5")) == PrefixArgumentReader::Outcome::Continue);
    REQUIRE(reader.Value() == -5);
}

TEST_CASE("A further C-u after digits have started terminates instead of continuing", "[PrefixArgument]") {
    PrefixArgumentReader reader;
    REQUIRE(reader.HandleKey(ParseKeyChord("4")) == PrefixArgumentReader::Outcome::Continue);
    REQUIRE(reader.HandleKey(ParseKeyChord("C-u")) == PrefixArgumentReader::Outcome::Terminate);
    REQUIRE(reader.Value() == 4);
}

TEST_CASE("StatusText reflects the resolved value while reading", "[PrefixArgument]") {
    PrefixArgumentReader reader;
    REQUIRE(reader.StatusText() == "C-u 4-");

    REQUIRE(reader.HandleKey(ParseKeyChord("4")) == PrefixArgumentReader::Outcome::Continue);
    REQUIRE(reader.HandleKey(ParseKeyChord("2")) == PrefixArgumentReader::Outcome::Continue);
    REQUIRE(reader.StatusText() == "C-u 42-");
}
