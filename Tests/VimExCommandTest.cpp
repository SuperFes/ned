#include <catch2/catch_test_macros.hpp>

#include "Editor/Vim/VimExCommand.h"

using ned::editor::vim::ExRange;
using ned::editor::vim::ParseExCommand;
using ned::editor::vim::ParseSubstituteArgs;

TEST_CASE("A plain command with no range", "[VimExCommand]") {
    const auto cmd = ParseExCommand("w", 5, 10, std::nullopt);
    REQUIRE(cmd.has_value());
    REQUIRE_FALSE(cmd->range.present);
    REQUIRE(cmd->name == "w");
    REQUIRE_FALSE(cmd->bang);
}

TEST_CASE("wq and q! parse name and bang", "[VimExCommand]") {
    const auto wq = ParseExCommand("wq", 0, 0, std::nullopt);
    REQUIRE(wq->name == "wq");

    const auto qbang = ParseExCommand("q!", 0, 0, std::nullopt);
    REQUIRE(qbang->name == "q");
    REQUIRE(qbang->bang);
}

TEST_CASE("A bare numeric address is a range-only command (goto line)", "[VimExCommand]") {
    const auto cmd = ParseExCommand("42", 0, 100, std::nullopt);
    REQUIRE(cmd->range.present);
    REQUIRE(cmd->range.startLine == 41); // 1-based -> 0-based
    REQUIRE(cmd->range.endLine == 41);
    REQUIRE(cmd->name.empty());
}

TEST_CASE("% expands to the whole file", "[VimExCommand]") {
    const auto cmd = ParseExCommand("%d", 3, 20, std::nullopt);
    REQUIRE(cmd->range.present);
    REQUIRE(cmd->range.startLine == 0);
    REQUIRE(cmd->range.endLine == 20);
    REQUIRE(cmd->name == "d");
}

TEST_CASE(". and $ resolve against the caller's current/last line", "[VimExCommand]") {
    const auto cmd = ParseExCommand(".,$d", 7, 20, std::nullopt);
    REQUIRE(cmd->range.startLine == 7);
    REQUIRE(cmd->range.endLine == 20);
}

TEST_CASE("'< and '> resolve against the supplied visual range", "[VimExCommand]") {
    const auto cmd = ParseExCommand("'<,'>s/foo/bar/g", 0, 100, ExRange{true, 3, 8});
    REQUIRE(cmd->range.startLine == 3);
    REQUIRE(cmd->range.endLine == 8);
    REQUIRE(cmd->name == "s");
    REQUIRE(cmd->rest == "/foo/bar/g");
}

TEST_CASE("A reversed explicit range is normalized low-to-high", "[VimExCommand]") {
    const auto cmd = ParseExCommand("10,5d", 0, 100, std::nullopt);
    REQUIRE(cmd->range.startLine == 4);
    REQUIRE(cmd->range.endLine == 9);
}

TEST_CASE(":normal keeps its raw argument, stripping exactly one leading space", "[VimExCommand]") {
    const auto cmd = ParseExCommand("normal  ddp", 0, 0, std::nullopt);
    REQUIRE(cmd->name == "normal");
    REQUIRE(cmd->rest == " ddp"); // only one of the two spaces is the separator
}

TEST_CASE("Blank input is not a command", "[VimExCommand]") {
    REQUIRE_FALSE(ParseExCommand("", 0, 0, std::nullopt).has_value());
    REQUIRE_FALSE(ParseExCommand("   ", 0, 0, std::nullopt).has_value());
}

TEST_CASE("ParseSubstituteArgs splits pattern/replacement/flags on the chosen delimiter", "[VimExCommand]") {
    const auto args = ParseSubstituteArgs("/foo/bar/g");
    REQUIRE(args.has_value());
    REQUIRE(args->pattern == "foo");
    REQUIRE(args->replacement == "bar");
    REQUIRE(args->flags == "g");
}

TEST_CASE("ParseSubstituteArgs accepts a non-slash delimiter and missing flags", "[VimExCommand]") {
    const auto args = ParseSubstituteArgs("#/usr#/opt#");
    REQUIRE(args.has_value());
    REQUIRE(args->pattern == "/usr");
    REQUIRE(args->replacement == "/opt");
    REQUIRE(args->flags.empty());
}

TEST_CASE("ParseSubstituteArgs tolerates a missing trailing delimiter", "[VimExCommand]") {
    const auto args = ParseSubstituteArgs("/foo/bar");
    REQUIRE(args->pattern == "foo");
    REQUIRE(args->replacement == "bar");
    REQUIRE(args->flags.empty());
}
