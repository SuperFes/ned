#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "Editor/Link.h"

using ned::editor::link::ClassifyTarget;
using ned::editor::link::DetectLinkAtPoint;
using ned::editor::link::LinkKind;
using ned::editor::link::OpenUrl;
using ned::editor::link::ResolveFileLink;
using ned::editor::link::SetUrlOpenCommand;
using ned::editor::link::UrlOpenCommand;

TEST_CASE("DetectLinkAtPoint finds a bare URL under point", "[Link]") {
    const std::string text  = "see https://example.com/path for details\n";
    const std::size_t point = text.find("example");

    const auto detected = DetectLinkAtPoint(text, point);
    REQUIRE(detected.has_value());
    CHECK(detected->kind == LinkKind::Url);
    CHECK(detected->target == "https://example.com/path");
}

TEST_CASE("DetectLinkAtPoint trims one trailing punctuation character off a URL", "[Link]") {
    const std::string text  = "see https://example.com/path.\n";
    const std::size_t point = text.find("example");

    const auto detected = DetectLinkAtPoint(text, point);
    REQUIRE(detected.has_value());
    CHECK(detected->target == "https://example.com/path");
}

TEST_CASE("DetectLinkAtPoint classifies a slash-containing token as a File candidate", "[Link]") {
    const std::string text  = "open src/main.cpp now\n";
    const std::size_t point = text.find("main.cpp");

    const auto detected = DetectLinkAtPoint(text, point);
    REQUIRE(detected.has_value());
    CHECK(detected->kind == LinkKind::File);
    CHECK(detected->target == "src/main.cpp");
}

TEST_CASE("DetectLinkAtPoint classifies a dotted-extension token as a File candidate", "[Link]") {
    const std::string text  = "see notes.txt for details\n";
    const std::size_t point = text.find("notes.txt");

    const auto detected = DetectLinkAtPoint(text, point);
    REQUIRE(detected.has_value());
    CHECK(detected->kind == LinkKind::File);
    CHECK(detected->target == "notes.txt");
}

TEST_CASE("DetectLinkAtPoint never classifies a bare word as a File candidate", "[Link]") {
    const std::string text  = "TODO buy milk\n";
    const std::size_t point = text.find("TODO");

    REQUIRE_FALSE(DetectLinkAtPoint(text, point).has_value());
}

TEST_CASE("DetectLinkAtPoint returns nullopt on a blank line", "[Link]") {
    REQUIRE_FALSE(DetectLinkAtPoint("\n\n", 1).has_value());
}

TEST_CASE("ClassifyTarget recognizes http/https as Url, everything else as File", "[Link]") {
    CHECK(ClassifyTarget("https://example.com") == LinkKind::Url);
    CHECK(ClassifyTarget("http://example.com") == LinkKind::Url);
    CHECK(ClassifyTarget("src/main.cpp") == LinkKind::File);
    CHECK(ClassifyTarget("Some Headline") == LinkKind::File);
}

TEST_CASE("ResolveFileLink resolves an absolute path that exists", "[Link]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_link_test_absolute";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path file = dir / "note.txt";
    {
        std::ofstream(file) << "hi";
    }

    const auto resolved = ResolveFileLink(file.string(), dir);
    REQUIRE(resolved.has_value());
    CHECK(std::filesystem::equivalent(*resolved, file));

    std::filesystem::remove_all(dir);
}

TEST_CASE("ResolveFileLink resolves a relative path against baseDirectory", "[Link]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_link_test_relative";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path file = dir / "note.txt";
    {
        std::ofstream(file) << "hi";
    }

    const auto resolved = ResolveFileLink("note.txt", dir);
    REQUIRE(resolved.has_value());
    CHECK(std::filesystem::equivalent(*resolved, file));

    std::filesystem::remove_all(dir);
}

TEST_CASE("ResolveFileLink returns nullopt when nothing exists anywhere it looks", "[Link]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_link_test_missing";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);

    REQUIRE_FALSE(ResolveFileLink("does-not-exist.txt", dir).has_value());

    std::filesystem::remove_all(dir);
}

TEST_CASE("OpenUrl returns false without launching anything when no command is configured", "[Link]") {
    const auto previous = UrlOpenCommand();
    SetUrlOpenCommand(std::nullopt);

    REQUIRE_FALSE(OpenUrl("https://example.com"));

    SetUrlOpenCommand(previous); // restore -- this state is process-wide, other tests may depend on the default
}

TEST_CASE("SetUrlOpenCommand/UrlOpenCommand round-trip, defaulting to xdg-open", "[Link]") {
    const auto previous = UrlOpenCommand();

    CHECK(UrlOpenCommand() == "xdg-open"); // the documented default before anything overrides it

    SetUrlOpenCommand("my-browser");
    CHECK(UrlOpenCommand() == "my-browser");

    SetUrlOpenCommand(previous);
}
