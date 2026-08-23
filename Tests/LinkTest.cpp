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
using ned::editor::link::StripDelimiters;
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

TEST_CASE("DetectLinkAtPoint strips quotes off a #include-style target", "[Link]") {
    const std::string text  = "#include \"Editor/Acp/AcpManager.h\"\n";
    const std::size_t point = text.find("AcpManager");

    const auto detected = DetectLinkAtPoint(text, point);
    REQUIRE(detected.has_value());
    CHECK(detected->kind == LinkKind::File);
    CHECK(detected->target == "Editor/Acp/AcpManager.h");
}

TEST_CASE("DetectLinkAtPoint strips angle brackets off a #include-style target", "[Link]") {
    const std::string text  = "#include <vector>\n";
    const std::size_t point = text.find("vector");

    const auto detected = DetectLinkAtPoint(text, point);
    REQUIRE(detected.has_value());
    CHECK(detected->kind == LinkKind::File);
    CHECK(detected->target == "vector");
}

TEST_CASE("DetectLinkAtPoint finds a #include target with point anywhere on the line, not just on the target itself",
          "[Link]") {
    const std::string text = "#include <vector>\n";

    for (const std::string_view needle : {"#", "include", "clude"}) {
        const std::size_t point = text.find(needle);
        REQUIRE(point != std::string::npos);
        const auto detected = DetectLinkAtPoint(text, point);
        REQUIRE(detected.has_value());
        CHECK(detected->kind == LinkKind::File);
        CHECK(detected->target == "vector");
    }
}

TEST_CASE("DetectLinkAtPoint finds a quoted #include target with point anywhere on the line", "[Link]") {
    const std::string text  = "#include \"Editor/Acp/AcpManager.h\"\n";
    const std::size_t point = text.find("#include");

    const auto detected = DetectLinkAtPoint(text, point);
    REQUIRE(detected.has_value());
    CHECK(detected->kind == LinkKind::File);
    CHECK(detected->target == "Editor/Acp/AcpManager.h");
}

TEST_CASE("DetectLinkAtPoint does not treat template angle brackets as a file target on a non-#include line", "[Link]") {
    const std::string text  = "std::vector<int> values;\n";
    const std::size_t point = text.find("values");

    // Point is on the bare word "values" -- not path-shaped, not
    // delimited -- and the line doesn't start with #include, so the
    // <int> angle-bracket run must never be picked up as a guess.
    REQUIRE_FALSE(DetectLinkAtPoint(text, point).has_value());
}

TEST_CASE("DetectLinkAtPoint prefers an exact match under point over a delimited target elsewhere in the statement",
          "[Link]") {
    const std::string text  = "see src/main.cpp or \"notes.txt\" for details\n";
    const std::size_t point = text.find("main.cpp");

    const auto detected = DetectLinkAtPoint(text, point);
    REQUIRE(detected.has_value());
    CHECK(detected->target == "src/main.cpp"); // not "notes.txt", despite being on the same (comma/semicolon-free) line
}

TEST_CASE("DetectLinkAtPoint's statement scope stops at a semicolon, not the whole line", "[Link]") {
    const std::string text  = "foo(); #include <vector>\n";
    const std::size_t point = text.find("foo");

    // Point's own statement ("foo();") has no delimited target at all --
    // the <vector> on the other side of the ';' must not leak across.
    REQUIRE_FALSE(DetectLinkAtPoint(text, point).has_value());
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

TEST_CASE("ResolveFileLink falls back to includePaths for a target not found under baseDirectory/root", "[Link]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_link_test_include_paths";
    std::filesystem::remove_all(dir);
    const std::filesystem::path includeDir = dir / "vendored" / "include";
    std::filesystem::create_directories(includeDir);
    const std::filesystem::path header = includeDir / "vector";
    {
        std::ofstream(header) << "// stand-in for a system header";
    }
    const std::filesystem::path emptyBase = dir / "empty_base";
    std::filesystem::create_directories(emptyBase);

    REQUIRE_FALSE(ResolveFileLink("vector", emptyBase).has_value());

    const auto resolved = ResolveFileLink("vector", emptyBase, {includeDir});
    REQUIRE(resolved.has_value());
    CHECK(std::filesystem::equivalent(*resolved, header));

    std::filesystem::remove_all(dir);
}

TEST_CASE("StripDelimiters strips one matching layer of quotes/angle-brackets", "[Link]") {
    CHECK(StripDelimiters("\"foo.h\"") == "foo.h");
    CHECK(StripDelimiters("'foo.h'") == "foo.h");
    CHECK(StripDelimiters("<vector>") == "vector");
    CHECK(StripDelimiters("foo.h") == "foo.h"); // no delimiters -- unchanged
    CHECK(StripDelimiters("\"unbalanced") == "\"unbalanced");
}

TEST_CASE("ResolveFileLink with empty extension/index lists behaves exactly as before", "[Link]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_link_test_no_widening";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path file = dir / "note.txt";
    { std::ofstream(file) << "hi"; }

    REQUIRE(ResolveFileLink("note.txt", dir).has_value());
    REQUIRE_FALSE(ResolveFileLink("note", dir).has_value()); // no extension inference requested

    std::filesystem::remove_all(dir);
}

TEST_CASE("ResolveFileLink appends a candidate extension to find a relative import written without one", "[Link]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_link_test_extension";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directory(dir);
    const std::filesystem::path file = dir / "foo.ts";
    { std::ofstream(file) << "export {};"; }

    const auto resolved = ResolveFileLink("foo", dir, {}, {"ts", "js"});
    REQUIRE(resolved.has_value());
    CHECK(std::filesystem::equivalent(*resolved, file));

    std::filesystem::remove_all(dir);
}

TEST_CASE("ResolveFileLink resolves a directory/package import via an index-file basename", "[Link]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_link_test_index";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "foo");
    const std::filesystem::path indexFile = dir / "foo" / "index.js";
    { std::ofstream(indexFile) << "module.exports = {};"; }

    const auto resolved = ResolveFileLink("foo", dir, {}, {"js"}, {"index"});
    REQUIRE(resolved.has_value());
    CHECK(std::filesystem::equivalent(*resolved, indexFile));

    std::filesystem::remove_all(dir);
}

TEST_CASE("ResolveFileLink resolves a Python package's __init__.py after dot-to-slash conversion", "[Link]") {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ned_link_test_python_package";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "foo" / "bar");
    const std::filesystem::path initFile = dir / "foo" / "bar" / "__init__.py";
    { std::ofstream(initFile) << "# package"; }

    const auto resolved = ResolveFileLink("foo/bar", dir, {}, {"py"}, {"__init__"});
    REQUIRE(resolved.has_value());
    CHECK(std::filesystem::equivalent(*resolved, initFile));

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
