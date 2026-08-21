#include <catch2/catch_test_macros.hpp>

#include "Editor/PromptHistory.h"

using ned::editor::PromptHistory;

TEST_CASE("Entries on a key nothing was recorded under is empty", "[PromptHistory]") {
    PromptHistory history;
    REQUIRE(history.Entries("find-file").empty());
}

TEST_CASE("Record pushes newest-first", "[PromptHistory]") {
    PromptHistory history;

    history.Record("find-file", "/tmp/a");
    history.Record("find-file", "/tmp/b");
    history.Record("find-file", "/tmp/c");

    const auto& entries = history.Entries("find-file");
    REQUIRE(entries.size() == 3);
    REQUIRE(entries[0] == "/tmp/c");
    REQUIRE(entries[1] == "/tmp/b");
    REQUIRE(entries[2] == "/tmp/a");
}

TEST_CASE("Record ignores an empty entry", "[PromptHistory]") {
    PromptHistory history;

    history.Record("goto-line", "");

    REQUIRE(history.Entries("goto-line").empty());
}

TEST_CASE("Record dedups a consecutive repeat of the most recent entry", "[PromptHistory]") {
    PromptHistory history;

    history.Record("execute-command", "format-buffer");
    history.Record("execute-command", "format-buffer");
    history.Record("execute-command", "format-buffer");

    REQUIRE(history.Entries("execute-command").size() == 1);

    // Re-submitting an older (non-consecutive) entry still records a fresh
    // copy at the front, matching Emacs' own history behavior.
    history.Record("execute-command", "save-buffer");
    history.Record("execute-command", "format-buffer");

    const auto& entries = history.Entries("execute-command");
    REQUIRE(entries.size() == 3);
    REQUIRE(entries[0] == "format-buffer");
    REQUIRE(entries[1] == "save-buffer");
    REQUIRE(entries[2] == "format-buffer");
}

TEST_CASE("Record evicts the oldest entry once over capacity", "[PromptHistory]") {
    PromptHistory history(/*capacityPerKind=*/2);

    history.Record("goto-line", "1");
    history.Record("goto-line", "2");
    history.Record("goto-line", "3");

    const auto& entries = history.Entries("goto-line");
    REQUIRE(entries.size() == 2);
    REQUIRE(entries[0] == "3");
    REQUIRE(entries[1] == "2");
}

TEST_CASE("Different keys keep independent rings", "[PromptHistory]") {
    PromptHistory history;

    history.Record("find-file", "/tmp/a");
    history.Record("goto-line", "42");

    REQUIRE(history.Entries("find-file") == std::vector<std::string>{"/tmp/a"});
    REQUIRE(history.Entries("goto-line") == std::vector<std::string>{"42"});
}
