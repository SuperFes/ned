#include <catch2/catch_test_macros.hpp>

#include "Editor/SearchEverywherePreview.h"

using ned::editor::FormatSearchEverywherePreview;

TEST_CASE("a preview window with no target line spends no column on a marker", "[SearchEverywherePreview]") {
    const std::vector<std::string> raw = {"#include <cstddef>", "", "int main() {"};

    const std::vector<std::string> lines = FormatSearchEverywherePreview(raw, std::nullopt, 4);

    REQUIRE(lines == std::vector<std::string>{"#include <cstddef>", "", "int main() {"});
}

TEST_CASE("the target line is marked and the others are padded to match", "[SearchEverywherePreview]") {
    const std::vector<std::string> raw = {"int a = 1;", "int b = 2;", "int c = 3;"};

    const std::vector<std::string> lines = FormatSearchEverywherePreview(raw, 1, 4);

    REQUIRE(lines == std::vector<std::string>{"  int a = 1;", "▸ int b = 2;", "  int c = 3;"});
}

TEST_CASE("indentation common to the whole window is stripped, relative indentation kept",
          "[SearchEverywherePreview]") {
    const std::vector<std::string> raw = {"            if (ready) {", "                Run();", "            }"};

    const std::vector<std::string> lines = FormatSearchEverywherePreview(raw, std::nullopt, 4);

    REQUIRE(lines == std::vector<std::string>{"if (ready) {", "    Run();", "}"});
}

TEST_CASE("a blank line neither constrains the common indent nor gets stripped", "[SearchEverywherePreview]") {
    // The blank line's zero indentation would flatten the strip to nothing
    // if it counted, which is the whole reason blanks are excluded.
    const std::vector<std::string> raw = {"        Setup();", "", "        Run();"};

    const std::vector<std::string> lines = FormatSearchEverywherePreview(raw, std::nullopt, 4);

    REQUIRE(lines == std::vector<std::string>{"Setup();", "", "Run();"});
}

TEST_CASE("tabs expand to the next tab stop rather than a fixed run", "[SearchEverywherePreview]") {
    // "ab\tc": the tab advances from column 2 to column 4, so two spaces,
    // not a full tabWidth of them. Nothing is stripped -- the first line
    // starts at column 0, so the window has no indentation in common.
    const std::vector<std::string> raw = {"ab\tc", "\tx"};

    const std::vector<std::string> lines = FormatSearchEverywherePreview(raw, std::nullopt, 4);

    REQUIRE(lines == std::vector<std::string>{"ab  c", "    x"});
}

TEST_CASE("trailing whitespace and a CRLF carriage return are trimmed", "[SearchEverywherePreview]") {
    const std::vector<std::string> raw = {"value = 1;   ", "other = 2;\r"};

    const std::vector<std::string> lines = FormatSearchEverywherePreview(raw, std::nullopt, 4);

    REQUIRE(lines == std::vector<std::string>{"value = 1;", "other = 2;"});
}

TEST_CASE("a blank line inside a marked window stays bare rather than padded", "[SearchEverywherePreview]") {
    const std::vector<std::string> raw = {"setup();", "", "run();"};

    const std::vector<std::string> lines = FormatSearchEverywherePreview(raw, 2, 4);

    REQUIRE(lines == std::vector<std::string>{"  setup();", "", "\u25b8 run();"});
}

TEST_CASE("an entirely blank window previews nothing at all", "[SearchEverywherePreview]") {
    const std::vector<std::string> raw = {"", "   ", "\t"};

    REQUIRE(FormatSearchEverywherePreview(raw, 1, 4).empty());
}

TEST_CASE("a very long line is cut on a codepoint boundary", "[SearchEverywherePreview]") {
    // Multi-byte glyphs straddling the cut: a byte-wise truncation would
    // leave a split codepoint the popup paints as garbage.
    std::string wide;
    for (std::size_t i = 0; i < ned::editor::kSearchEverywherePreviewMaxColumns + 50; ++i) {
        wide += "é";
    }

    const std::vector<std::string> lines = FormatSearchEverywherePreview({wide}, std::nullopt, 4);

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0].size() == ned::editor::kSearchEverywherePreviewMaxColumns * 2); // 2 bytes per é
}

TEST_CASE("the marker column comes out of the line budget, not on top of it", "[SearchEverywherePreview]") {
    const std::string              wide(ned::editor::kSearchEverywherePreviewMaxColumns + 50, 'x');
    const std::vector<std::string> lines = FormatSearchEverywherePreview({wide}, 0, 4);

    REQUIRE(lines.size() == 1);
    REQUIRE(lines[0] == "▸ " + std::string(ned::editor::kSearchEverywherePreviewMaxColumns - 2, 'x'));
}
