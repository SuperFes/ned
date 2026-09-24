// Markdown's trailing whitespace that is content (Mode::keptTrailingWhitespace):
// the hook itself, and a real save that honours it.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "Editor/BufferSave.h"
#include "Editor/Mode.h"
#include "Editor/TrimOnSave.h"
#include "Text/Buffer.h"

using ned::editor::MarkdownMode;

TEST_CASE("Markdown keeps hard line breaks and code-block text", "[Markdown][Whitespace]") {
    const std::string text = "para one  \n"        // 0: hard break
                             "para end  \n"        // 1: the paragraph's last line -- not a break
                             "\n"                  // 2
                             "> quoted  \n"        // 3: hard break inside a quote
                             "> more\n"            // 4
                             "\n"                  // 5
                             "```\n"               // 6
                             "code  \n"            // 7
                             "   \n"               // 8
                             "```\n"               // 9
                             "\n"                  // 10
                             "    indented  \n"    // 11
                             "\n"                  // 12
                             "single \n"           // 13: one space is not a break
                             "tail\n";             // 14
    CHECK(MarkdownMode().keptTrailingWhitespace(text) == std::vector<std::size_t>{0, 3, 7, 8, 11});
}

TEST_CASE("Saving a Markdown file keeps its hard line breaks", "[Markdown][Whitespace]") {
    ned::editor::SetTrimTrailingWhitespaceOnSave(true);
    const std::filesystem::path path = std::filesystem::temp_directory_path() / ("ned_md_trim_" + std::to_string(::getpid()) + ".md");
    {
        std::ofstream seed(path, std::ios::binary);
    }
    ned::text::Buffer buffer = ned::text::Buffer::FromFile(path);
    buffer.InsertAt(0, "line one  \nline two   \n\nnoise  \n");
    ned::editor::WriteBufferToDisk(buffer);
    std::ifstream     file(path, std::ios::binary);
    const std::string saved{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::filesystem::remove(path);
    CHECK(saved == "line one  \nline two\n\nnoise\n");
}
