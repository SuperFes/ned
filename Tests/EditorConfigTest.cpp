#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

#include "Editor/BufferSave.h"
#include "Editor/EditorConfig.h"
#include "Editor/FileSettings.h"
#include "Editor/FillColumn.h"
#include "Editor/FinalNewline.h"
#include "Editor/IndentDetect.h"
#include "Editor/LineEndingPolicy.h"
#include "Editor/RulerSettings.h"
#include "Editor/TrimOnSave.h"
#include "Text/Buffer.h"
#include "Text/BufferList.h"

using ned::editor::EditorConfigConventions;
using ned::editor::EditorConfigGlobMatches;
using ned::editor::EditorConfigIndent;
using ned::editor::EditorConfigPropertiesFor;
using ned::editor::FileIndentOverride;
using ned::editor::IndentOverride;
using ned::editor::ParseEditorConfig;
using ned::text::FileConventions;
using ned::text::LineEnding;

namespace {

// A fresh directory tree per test, removed afterwards.
struct TempTree {
    std::filesystem::path root;

    explicit TempTree(const std::string& name) : root(std::filesystem::temp_directory_path() / ("ned_editorconfig_" + name + "_" + std::to_string(::getpid()))) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
    }

    ~TempTree() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    TempTree(const TempTree&)            = delete;
    TempTree& operator=(const TempTree&) = delete;

    std::filesystem::path Write(const std::string& relative, const std::string& content) const {
        const std::filesystem::path path = root / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << content;
        return path;
    }
};

std::string ReadBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct SaveSettingsGuard {
    ~SaveSettingsGuard() {
        ned::editor::SetEnsureFinalNewline(true);
        ned::editor::SetTrimTrailingWhitespaceOnSave(true);
        ned::editor::SetLineEndingPolicy({});
    }
};

struct TogglesGuard {
    ~TogglesGuard() {
        ned::editor::SetEditorConfigEnabled(true);
        ned::editor::SetIndentDetection(true);
    }
};

} // namespace

TEST_CASE("ParseEditorConfig reads root, sections and properties", "[EditorConfig]") {
    const auto file = ParseEditorConfig("# top\nroot = true\n\n[*]\nIndent_Style = space\n; note\n[*.go]\nindent_style=tab\n");
    CHECK(file.root);
    REQUIRE(file.sections.size() == 2);
    CHECK(file.sections[0].pattern == "*");
    REQUIRE(file.sections[0].properties.size() == 1);
    CHECK(file.sections[0].properties[0].first == "indent_style");
    CHECK(file.sections[0].properties[0].second == "space");
    CHECK(file.sections[1].pattern == "*.go");

    CHECK_FALSE(ParseEditorConfig("root = false\n[*]\n").root);
    CHECK(ParseEditorConfig("[*]\nroot = true\n").sections[0].properties.size() == 1); // only the preamble's root counts
}

TEST_CASE("An .editorconfig glob without a slash matches at any depth", "[EditorConfig]") {
    CHECK(EditorConfigGlobMatches("*", "a.py"));
    CHECK(EditorConfigGlobMatches("*.py", "a.py"));
    CHECK(EditorConfigGlobMatches("*.py", "src/deep/a.py"));
    CHECK_FALSE(EditorConfigGlobMatches("*.py", "a.pyc"));
    CHECK(EditorConfigGlobMatches("Makefile", "sub/Makefile"));
}

TEST_CASE("An .editorconfig glob with a slash is anchored to its directory", "[EditorConfig]") {
    CHECK(EditorConfigGlobMatches("src/*.py", "src/a.py"));
    CHECK_FALSE(EditorConfigGlobMatches("src/*.py", "src/b/a.py")); // * stays in one segment
    CHECK_FALSE(EditorConfigGlobMatches("src/*.py", "x/src/a.py"));
    CHECK(EditorConfigGlobMatches("src/**.py", "src/b/a.py"));
    CHECK(EditorConfigGlobMatches("/Makefile", "Makefile"));
    CHECK_FALSE(EditorConfigGlobMatches("/Makefile", "sub/Makefile"));
}

TEST_CASE("An .editorconfig glob handles ?, classes, braces and ranges", "[EditorConfig]") {
    CHECK(EditorConfigGlobMatches("?.c", "a.c"));
    CHECK_FALSE(EditorConfigGlobMatches("?.c", "ab.c"));
    CHECK(EditorConfigGlobMatches("[abc].c", "b.c"));
    CHECK_FALSE(EditorConfigGlobMatches("[!abc].c", "b.c"));
    CHECK(EditorConfigGlobMatches("[a-c].c", "c.c"));
    CHECK(EditorConfigGlobMatches("*.{js,ts}", "x.ts"));
    CHECK_FALSE(EditorConfigGlobMatches("*.{js,ts}", "x.rs"));
    CHECK(EditorConfigGlobMatches("{a,{b,c}}.txt", "c.txt"));
    CHECK(EditorConfigGlobMatches("file{1..3}.txt", "file2.txt"));
    CHECK_FALSE(EditorConfigGlobMatches("file{1..3}.txt", "file4.txt"));
    CHECK_FALSE(EditorConfigGlobMatches("file{1..3}.txt", "file10.txt"));
    CHECK(EditorConfigGlobMatches("v{-2..2}", "v-1"));
    CHECK(EditorConfigGlobMatches("{single}", "{single}")); // one alternative is literal text
    CHECK(EditorConfigGlobMatches("a\\*b", "a*b"));
    CHECK_FALSE(EditorConfigGlobMatches("a\\*b", "axb"));
}

TEST_CASE("EditorConfigIndent reads indent_style, indent_size and tab_width", "[EditorConfig]") {
    CHECK(EditorConfigIndent({{"indent_style", "space"}, {"indent_size", "2"}}) ==
          IndentOverride{.useTabs = false, .width = 2});
    CHECK(EditorConfigIndent({{"indent_style", "tab"}, {"tab_width", "8"}}) == IndentOverride{.useTabs = true, .width = 8});
    CHECK(EditorConfigIndent({{"indent_size", "tab"}, {"tab_width", "3"}}) == IndentOverride{.width = 3});
    CHECK(EditorConfigIndent({{"indent_style", "space"}, {"tab_width", "8"}}) == IndentOverride{.useTabs = false});
    CHECK(EditorConfigIndent({{"indent_size", "zero"}}).Empty());
    CHECK(EditorConfigIndent({}).Empty());
}

TEST_CASE("EditorConfigPropertiesFor layers files nearest-last and stops at root", "[EditorConfig]") {
    const TempTree tree("layers");
    tree.Write(".editorconfig", "[*]\nindent_size = 7\n"); // above the root file: never read
    tree.Write("project/.editorconfig", "root = true\n[*]\nindent_style = space\nindent_size = 4\n[Makefile]\nindent_style = tab\n");
    tree.Write("project/go/.editorconfig", "[*.go]\nindent_style = TAB\nindent_size = unset\n");

    const auto top = EditorConfigPropertiesFor(tree.root / "project/main.c");
    CHECK(top.at("indent_style") == "space");
    CHECK(top.at("indent_size") == "4");

    CHECK(EditorConfigPropertiesFor(tree.root / "project/Makefile").at("indent_style") == "tab");

    const auto go = EditorConfigPropertiesFor(tree.root / "project/go/main.go");
    CHECK(go.at("indent_style") == "tab"); // values are lowercased
    CHECK_FALSE(go.contains("indent_size"));

    CHECK(EditorConfigPropertiesFor(tree.root / "project/go/other.c").at("indent_size") == "4");
}

TEST_CASE("A file's modeline outranks its .editorconfig, which outranks its content", "[EditorConfig][FileSettings]") {
    const TogglesGuard guard;
    const TempTree     tree("precedence");
    tree.Write(".editorconfig", "root = true\n[*.c]\nindent_style = space\n");

    const std::filesystem::path file   = tree.root / "a.c";
    const std::string           tabbed = "int f(void) {\n\treturn 0;\n}\n";
    // Content says tabs, .editorconfig says spaces; the width it doesn't state stays open.
    CHECK(FileIndentOverride(file, tabbed) == IndentOverride{.useTabs = false});

    const std::string modeline = tabbed + "/* vim: set noexpandtab sw=8: */\n";
    CHECK(FileIndentOverride(file, modeline) == IndentOverride{.useTabs = true, .width = 8});

    ned::editor::SetEditorConfigEnabled(false);
    CHECK(FileIndentOverride(file, tabbed) == IndentOverride{.useTabs = true});

    ned::editor::SetIndentDetection(false);
    CHECK(FileIndentOverride(file, tabbed).Empty());
}

TEST_CASE("ApplyFileSettings reads the file on disk, and .editorconfig for a new one", "[EditorConfig][FileSettings]") {
    const TogglesGuard guard;
    const TempTree     tree("apply");
    tree.Write(".editorconfig", "root = true\n[*.py]\nindent_size = 2\n");
    const std::filesystem::path existing = tree.Write("tabs.txt", "a\n\tb\n\tc\n");

    ned::text::Buffer buffer("tabs.txt");
    buffer.SetPath(existing);
    ned::editor::ApplyFileSettings(buffer);
    CHECK(buffer.LocalIndent() == IndentOverride{.useTabs = true});

    ned::text::Buffer fresh("new.py");
    fresh.SetPath(tree.root / "new.py");
    ned::editor::ApplyFileSettings(fresh);
    CHECK(fresh.LocalIndent() == IndentOverride{.width = 2});

    ned::text::Buffer scratch("scratch");
    ned::editor::ApplyFileSettings(scratch);
    CHECK(scratch.LocalIndent().Empty());
}

TEST_CASE("RefreshFileSettings re-reads a file renamed or reverted under its buffer", "[EditorConfig][FileSettings]") {
    const TogglesGuard guard;
    const TempTree     tree("refresh");
    tree.Write(".editorconfig", "root = true\n[*]\nend_of_line = crlf\n");
    tree.Write("lf/.editorconfig", "root = true\n[*]\nend_of_line = lf\n");
    const std::filesystem::path original = tree.Write("a.txt", "a\n    b\n");

    ned::text::BufferList bufferList;
    ned::text::Buffer&    buffer = bufferList.OpenFile(original);
    ned::editor::ApplyFileSettings(buffer);
    REQUIRE(buffer.Conventions().lineEnding == LineEnding::CRLF);
    REQUIRE(buffer.LocalIndent() == IndentOverride{.useTabs = false, .width = 4});

    // Nothing moved: nothing re-read.
    buffer.SetConventions({});
    ned::editor::RefreshFileSettings(bufferList);
    CHECK_FALSE(buffer.Conventions().lineEnding.has_value());

    // Renamed into a directory with its own .editorconfig.
    const std::filesystem::path moved = tree.root / "lf" / "a.txt";
    std::filesystem::rename(original, moved);
    buffer.SetPath(moved);
    ned::editor::RefreshFileSettings(bufferList);
    CHECK(buffer.Conventions().lineEnding == LineEnding::LF);

    // Reformatted on disk and reverted.
    tree.Write("lf/a.txt", "a\n\tb\n");
    buffer.Revert();
    ned::editor::RefreshFileSettings(bufferList);
    CHECK(buffer.LocalIndent() == IndentOverride{.useTabs = true});
}

TEST_CASE("EditorConfigConventions reads the save and layout properties", "[EditorConfig]") {
    CHECK(EditorConfigConventions({{"end_of_line", "crlf"},
                                   {"insert_final_newline", "false"},
                                   {"trim_trailing_whitespace", "true"},
                                   {"charset", "utf-8-bom"},
                                   {"max_line_length", "100"}}) ==
          FileConventions{.ensureFinalNewline     = false,
                          .trimTrailingWhitespace = true,
                          .lineEnding             = LineEnding::CRLF,
                          .utf8Bom                = true,
                          .maxLineLength          = 100});
    CHECK(EditorConfigConventions({{"charset", "utf-8"}, {"max_line_length", "off"}}) ==
          FileConventions{.utf8Bom = false, .maxLineLength = 0});
    // Encodings ned can't write, and values it doesn't know, state nothing.
    CHECK(EditorConfigConventions({{"charset", "latin1"}, {"end_of_line", "nel"}, {"insert_final_newline", "maybe"}}) ==
          FileConventions{});
}

TEST_CASE("A save follows the buffer's conventions over the global settings", "[EditorConfig][FileSettings]") {
    const SaveSettingsGuard guard;
    const TempTree          tree("save");
    ned::editor::SetLineEndingPolicy({.mode = ned::editor::LineEndingPolicyMode::Force, .forcedEnding = LineEnding::LF});

    ned::text::Buffer buffer("a.txt");
    buffer.SetPath(tree.root / "a.txt");
    buffer.InsertAtPoint("a  \nb");
    buffer.SetConventions({.ensureFinalNewline     = false,
                           .trimTrailingWhitespace = false,
                           .lineEnding             = LineEnding::CRLF,
                           .utf8Bom                = true});
    ned::editor::WriteBufferToDisk(buffer);
    CHECK(ReadBytes(tree.root / "a.txt") == "\xEF\xBB\xBF"
                                            "a  \r\nb");

    ned::editor::SetEnsureFinalNewline(false);
    ned::editor::SetTrimTrailingWhitespaceOnSave(false);
    buffer.SetConventions({.ensureFinalNewline = true, .trimTrailingWhitespace = true});
    buffer.InsertAtPoint(" ");
    ned::editor::WriteBufferToDisk(buffer);
    // The forced LF policy again, with no stated ending; the BOM the file now has is kept.
    CHECK(ReadBytes(tree.root / "a.txt") == "\xEF\xBB\xBF"
                                            "a\nb\n");
}

TEST_CASE("A buffer's max line length moves its ruler and fill column", "[EditorConfig][FileSettings]") {
    ned::text::Buffer buffer("a.txt");
    CHECK(ned::editor::RulerColumn(buffer) == ned::editor::RulerColumn());
    CHECK(ned::editor::FillColumn(buffer) == ned::editor::FillColumn());

    buffer.SetConventions({.maxLineLength = 100});
    CHECK(ned::editor::RulerColumn(buffer) == 100);
    CHECK(ned::editor::FillColumn(buffer) == 100);

    buffer.SetConventions({.maxLineLength = 0}); // "off"
    CHECK_FALSE(ned::editor::RulerColumn(buffer).has_value());
    CHECK(ned::editor::FillColumn(buffer) == ned::editor::FillColumn());
}

TEST_CASE("ApplyFileSettings takes a file's conventions from its .editorconfig", "[EditorConfig][FileSettings]") {
    const TogglesGuard guard;
    const TempTree     tree("conventions");
    tree.Write(".editorconfig", "root = true\n[*]\nend_of_line = crlf\nmax_line_length = 72\n");

    ned::text::Buffer buffer("a.txt");
    buffer.SetPath(tree.root / "a.txt");
    ned::editor::ApplyFileSettings(buffer);
    CHECK(buffer.Conventions() == FileConventions{.lineEnding = LineEnding::CRLF, .maxLineLength = 72});

    ned::editor::SetEditorConfigEnabled(false);
    ned::editor::ApplyFileSettings(buffer);
    CHECK(buffer.Conventions() == FileConventions{});
}

TEST_CASE("charset = utf-8 drops a file's own byte-order mark on save", "[EditorConfig][FileSettings]") {
    const TempTree              tree("charset");
    const std::filesystem::path path = tree.Write("bom.txt", "\xEF\xBB\xBFx\n");

    ned::text::Buffer buffer = ned::text::Buffer::FromFile(path);
    REQUIRE(buffer.Utf8Bom());
    buffer.SetConventions({.utf8Bom = false});
    buffer.InsertAt(0, "y");
    ned::editor::WriteBufferToDisk(buffer);
    CHECK(ReadBytes(path) == "yx\n");
    CHECK_FALSE(buffer.Utf8Bom()); // what was written

    buffer.SetConventions({});
    buffer.InsertAt(0, "z");
    ned::editor::WriteBufferToDisk(buffer);
    CHECK(ReadBytes(path) == "zyx\n");
}
