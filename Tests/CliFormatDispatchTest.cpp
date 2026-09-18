#include <catch2/catch_test_macros.hpp>

#include "Editor/CliFormatDispatch.h"

using ned::editor::InvokedAsNedFormat;

TEST_CASE("InvokedAsNedFormat matches a bare PATH-resolved basename", "[CliFormatDispatch]") {
    REQUIRE(InvokedAsNedFormat("ned-format"));
}

TEST_CASE("InvokedAsNedFormat matches an absolute path to the installed symlink", "[CliFormatDispatch]") {
    REQUIRE(InvokedAsNedFormat("/usr/bin/ned-format"));
    REQUIRE(InvokedAsNedFormat("/tmp/some/prefix/bin/ned-format"));
}

TEST_CASE("InvokedAsNedFormat rejects the ordinary ned binary", "[CliFormatDispatch]") {
    REQUIRE_FALSE(InvokedAsNedFormat("ned"));
    REQUIRE_FALSE(InvokedAsNedFormat("/usr/bin/ned"));
    REQUIRE_FALSE(InvokedAsNedFormat("./build/Source/ned"));
}

TEST_CASE("InvokedAsNedFormat does not match a directory component merely spelled ned-format",
          "[CliFormatDispatch]") {
    REQUIRE_FALSE(InvokedAsNedFormat("/opt/ned-format/ned"));
}

TEST_CASE("InvokedAsNedLangc matches only the ned-langc basename", "[CliFormatDispatch]") {
    using ned::editor::InvokedAsNedLangc;
    REQUIRE(InvokedAsNedLangc("ned-langc"));
    REQUIRE(InvokedAsNedLangc("/usr/bin/ned-langc"));
    REQUIRE_FALSE(InvokedAsNedLangc("ned"));
    REQUIRE_FALSE(InvokedAsNedLangc("ned-format"));
    REQUIRE_FALSE(InvokedAsNedLangc("/opt/ned-langc/ned"));
}
