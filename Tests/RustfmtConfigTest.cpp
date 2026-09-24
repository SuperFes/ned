#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>

#include "Editor/FileSettings.h"
#include "Editor/Format.h"
#include "Editor/FormatBuiltinStyle.h"
#include "Editor/FormatRules.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
#include "Editor/RustfmtConfig.h"
#include "Text/Buffer.h"

using ned::editor::BracePlacement;
using ned::editor::FormatRuleLayer;
using ned::editor::ParseRustfmtToml;
using ned::editor::RustfmtOptions;

namespace {

struct TempTree {
    std::filesystem::path root;

    explicit TempTree(const std::string& name) : root(std::filesystem::temp_directory_path() / ("ned_rustfmt_" + name + "_" + std::to_string(::getpid()))) {
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
        std::ofstream(path) << content;
        return path;
    }
};

struct RuleLayersGuard {
    ~RuleLayersGuard() {
        ned::editor::ClearFormatRuleLayer(FormatRuleLayer::File);
        ned::editor::ClearFormatRuleLayer(FormatRuleLayer::Builtin);
    }
};

} // namespace

TEST_CASE("ParseRustfmtToml reads the options ned follows and skips the rest", "[Rustfmt]") {
    CHECK(ParseRustfmtToml("# project style\n"
                           "hard_tabs = true\n"
                           "tab_spaces = 2   # narrow\n"
                           "max_width = 80\n"
                           "brace_style = \"AlwaysNextLine\"\n"
                           "control_brace_style = 'ClosingNextLine'\n"
                           "blank_lines_upper_bound = 2\n"
                           "edition = \"2021\"\n"
                           "tab_spaces_typo = 9\n") ==
          RustfmtOptions{.hardTabs             = true,
                         .tabSpaces            = 2,
                         .maxWidth             = 80,
                         .braceStyle           = "AlwaysNextLine",
                         .controlBraceStyle    = "ClosingNextLine",
                         .blankLinesUpperBound = 2});
    // A value of the wrong type, and anything under a table, is ignored.
    CHECK(ParseRustfmtToml("hard_tabs = \"yes\"\n[unstable]\nmax_width = 60\n") == RustfmtOptions{});
}

TEST_CASE("FindRustfmtConfig looks up from a directory for either spelling", "[Rustfmt]") {
    const TempTree tree("find");
    tree.Write(".rustfmt.toml", "max_width = 90\n");
    std::filesystem::create_directories(tree.root / "crate" / "src");
    CHECK(ned::editor::FindRustfmtConfig(tree.root / "crate" / "src") == tree.root / ".rustfmt.toml");
    tree.Write("crate/rustfmt.toml", "max_width = 70\n");
    CHECK(ned::editor::FindRustfmtConfig(tree.root / "crate" / "src") == tree.root / "crate" / "rustfmt.toml");
}

TEST_CASE("A Rust file takes indentation and line length from rustfmt.toml over .editorconfig", "[Rustfmt][FileSettings]") {
    const TempTree tree("settings");
    tree.Write(".editorconfig", "root = true\n[*]\nindent_size = 8\nmax_line_length = 120\n");
    tree.Write("rustfmt.toml", "hard_tabs = true\ntab_spaces = 2\nmax_width = 80\n");
    const std::filesystem::path rust   = tree.Write("src/lib.rs", "fn f() {}\n");
    const std::filesystem::path python = tree.Write("tool.py", "x = 1\n");

    ned::text::Buffer rustBuffer("lib.rs");
    rustBuffer.SetPath(rust);
    ned::editor::ApplyFileSettings(rustBuffer);
    CHECK(rustBuffer.LocalIndent() == ned::editor::IndentOverride{.useTabs = true, .width = 2});
    CHECK(rustBuffer.Conventions().maxLineLength == 80);

    ned::text::Buffer pythonBuffer("tool.py");
    pythonBuffer.SetPath(python);
    ned::editor::ApplyFileSettings(pythonBuffer);
    CHECK(pythonBuffer.LocalIndent().width == 8);
    CHECK(pythonBuffer.Conventions().maxLineLength == 120);
}

TEST_CASE("A project's rustfmt.toml adjusts Rust's bundled style", "[Rustfmt][FormatBuiltinStyle]") {
    const RuleLayersGuard guard;
    const TempTree        tree("rules");
    tree.Write("rustfmt.toml", "brace_style = \"AlwaysNextLine\"\n"
                               "control_brace_style = \"ClosingNextLine\"\n"
                               "blank_lines_upper_bound = 2\n");
    ned::editor::ApplyRustfmtRules(tree.root);
    CHECK(ned::editor::BreakRuleFor("brace.function", "rust").placement == BracePlacement::NextLine);
    CHECK(ned::editor::BreakRuleFor("brace.function.where", "rust").placement == BracePlacement::NextLine);
    CHECK(ned::editor::BreakRuleFor("control.keyword", "rust").before == true);
    CHECK_FALSE(ned::editor::BreakRuleFor("brace.control", "rust").placement.has_value());
    CHECK(ned::editor::BlankRuleFor("def.toplevel", "rust").maxBefore == 2);
    // Nothing leaks to another language.
    CHECK_FALSE(ned::editor::BreakRuleFor("brace.function", "go").placement.has_value());

    ned::editor::LoadBuiltinFormatStyles();
    const std::optional<ned::editor::Mode> mode = ned::editor::ModeByName("rust-mode");
    REQUIRE(mode.has_value());
    ned::text::Buffer buffer("t.rs");
    buffer.InsertAtPoint("fn f(x: bool) {\n    if x {\n        a();\n    } else {\n        b();\n    }\n}\n");
    ned::editor::ApplyNativeFormat(buffer, &*mode);
    CHECK(buffer.Text() == "fn f(x: bool)\n{\n    if x {\n        a();\n    }\n    else {\n        b();\n    }\n}\n");
}
