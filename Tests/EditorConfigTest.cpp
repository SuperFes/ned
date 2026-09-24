#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "Editor/EditorConfig.h"
#include "Editor/FileIndent.h"
#include "Editor/IndentDetect.h"
#include "Text/Buffer.h"

using ned::editor::EditorConfigGlobMatches;
using ned::editor::EditorConfigIndent;
using ned::editor::EditorConfigPropertiesFor;
using ned::editor::FileIndentOverride;
using ned::editor::IndentOverride;
using ned::editor::ParseEditorConfig;

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

TEST_CASE("A file's modeline outranks its .editorconfig, which outranks its content", "[EditorConfig][FileIndent]") {
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

TEST_CASE("ApplyFileIndent reads the file on disk, and .editorconfig for a new one", "[EditorConfig][FileIndent]") {
    const TogglesGuard guard;
    const TempTree     tree("apply");
    tree.Write(".editorconfig", "root = true\n[*.py]\nindent_size = 2\n");
    const std::filesystem::path existing = tree.Write("tabs.txt", "a\n\tb\n\tc\n");

    ned::text::Buffer buffer("tabs.txt");
    buffer.SetPath(existing);
    ned::editor::ApplyFileIndent(buffer);
    CHECK(buffer.LocalIndent() == IndentOverride{.useTabs = true});

    ned::text::Buffer fresh("new.py");
    fresh.SetPath(tree.root / "new.py");
    ned::editor::ApplyFileIndent(fresh);
    CHECK(fresh.LocalIndent() == IndentOverride{.width = 2});

    ned::text::Buffer scratch("scratch");
    ned::editor::ApplyFileIndent(scratch);
    CHECK(scratch.LocalIndent().Empty());
}
