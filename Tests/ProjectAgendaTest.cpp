#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "Editor/ProjectAgenda.h"

using ned::editor::CollectProjectTodos;
using ned::editor::SearchMatch;

namespace {

bool AnyEntryWithText(const std::vector<SearchMatch>& todos, const std::string& text) {
    return std::any_of(todos.begin(), todos.end(), [&](const SearchMatch& m) { return m.lineText == text; });
}

} // namespace

TEST_CASE("CollectProjectTodos finds active TODOs across multiple .org files, excluding DONE and plain headlines",
          "[ProjectAgenda]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_agenda_test_basic";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    {
        std::ofstream(dir / "a.org") << "* TODO Buy milk\n* DONE Buy eggs\n* Just a headline\n";
    }
    {
        std::ofstream(dir / "b.org") << "* TODO [#A] Fix the roof :urgent:home:\n";
    }
    // A non-.org file with the same shape must be ignored entirely.
    { std::ofstream(dir / "c.txt") << "* TODO not an org file\n"; }

    const std::vector<SearchMatch> todos = CollectProjectTodos(dir);

    REQUIRE(todos.size() == 2);
    REQUIRE(AnyEntryWithText(todos, "TODO Buy milk"));
    REQUIRE(AnyEntryWithText(todos, "TODO [#A] Fix the roof :urgent:home:"));

    const auto found = std::find_if(todos.begin(), todos.end(), [](const SearchMatch& m) { return m.file.filename() == "a.org"; });
    REQUIRE(found != todos.end());
    REQUIRE(found->lineNumber == 1); // 1-indexed, matching SearchMatch's own convention

    std::filesystem::remove_all(dir);
}

TEST_CASE("CollectProjectTodos respects a custom todoKeywords list's own last-is-done convention", "[ProjectAgenda]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_project_agenda_test_custom_keywords";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    {
        std::ofstream(dir / "a.org") << "* TODO Step one\n* IN-PROGRESS Step two\n* COMPLETE Step three\n";
    }

    const std::vector<SearchMatch> todos = CollectProjectTodos(dir, {"TODO", "IN-PROGRESS", "COMPLETE"});

    REQUIRE(todos.size() == 2); // COMPLETE (the last/configured-done keyword) is excluded
    REQUIRE(AnyEntryWithText(todos, "TODO Step one"));
    REQUIRE(AnyEntryWithText(todos, "IN-PROGRESS Step two"));
    REQUIRE_FALSE(AnyEntryWithText(todos, "COMPLETE Step three"));

    std::filesystem::remove_all(dir);
}

TEST_CASE("CollectProjectTodos returns an empty list for a nonexistent root, not a throw", "[ProjectAgenda]") {
    const std::vector<SearchMatch> todos =
        CollectProjectTodos(std::filesystem::temp_directory_path() / "ned_project_agenda_test_does_not_exist");
    REQUIRE(todos.empty());
}
