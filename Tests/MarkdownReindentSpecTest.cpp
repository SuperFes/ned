// Every GFM spec example, reindented. Markdown's leading whitespace is mostly
// meaning, so each result here was checked against cmark's rendering of the
// original (Tools/markdown-oracle.py) -- rerun that before reblessing:
//
//     NED_BLESS_MARKDOWN_REINDENT=1 ./build/Tests/ned_tests "[MarkdownReindentSpec]"
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "Editor/Indent.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

namespace fs = std::filesystem;

namespace {

std::string ReadFile(const fs::path& path) {
    std::ifstream     in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

TEST_CASE("Reindenting the GFM spec examples gives the checked result", "[MarkdownReindentSpec]") {
    const std::string spec = ReadFile(fs::path(NED_REPO_ROOT) / "Source" / "Languages" / "markdown" / "corpus" / "spec.txt");
    // Separators are 80 characters wide; examples themselves contain `---`/`===`.
    const std::regex example(R"(={20,}\n(Example \d+)[^\n]*\n(?::[^\n]*\n)*={20,}\n([\s\S]*?)\n-{20,}[ \t]*\n)");

    const ned::editor::Mode mode = ned::editor::MarkdownMode();
    std::string             rendered;
    std::size_t             count = 0;
    for (auto it = std::sregex_iterator(spec.begin(), spec.end(), example); it != std::sregex_iterator(); ++it) {
        ned::text::Buffer buffer("example.md");
        buffer.InsertAtPoint((*it)[2].str() + "\n");
        (void)ned::editor::IndentBuffer(buffer, mode);
        rendered += "### " + (*it)[1].str() + "\n" + buffer.Text();
        ++count;
    }
    REQUIRE(count > 250);

    const fs::path golden = fs::path(NED_REPO_ROOT) / "Tests" / "Format" / "markdown-spec-reindent.golden";
    if (std::getenv("NED_BLESS_MARKDOWN_REINDENT") != nullptr) {
        std::ofstream(golden, std::ios::binary) << rendered;
    }
    REQUIRE(fs::exists(golden));
    CHECK(ReadFile(golden) == rendered);
}
