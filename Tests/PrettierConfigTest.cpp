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
#include "Editor/PrettierConfig.h"
#include "Text/Buffer.h"

using ned::editor::FormatRuleLayer;
using ned::editor::ParsePrettierConfig;
using ned::editor::PrettierOptions;
using ned::editor::QuoteStyle;

namespace {

struct TempTree {
    std::filesystem::path root;

    explicit TempTree(const std::string& name) : root(std::filesystem::temp_directory_path() / ("ned_prettier_" + name + "_" + std::to_string(::getpid()))) {
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

TEST_CASE("ParsePrettierConfig reads JSON, YAML and TOML, and only the options ned follows", "[Prettier]") {
    const PrettierOptions expected{.useTabs = false, .tabWidth = 4, .printWidth = 100, .singleQuote = true};
    CHECK(ParsePrettierConfig("{\n  // comment\n  \"useTabs\": false, \"tabWidth\": 4, \"printWidth\": 100,\n"
                              "  \"singleQuote\": true, \"semi\": false }") == expected);
    CHECK(ParsePrettierConfig("# comment\nuseTabs: false\ntabWidth: 4\nprintWidth: 100\nsingleQuote: true\n"
                              "overrides:\n  - files: '*.md'\n    options:\n      tabWidth: 8\n") == expected);
    CHECK(ParsePrettierConfig("useTabs = false\ntabWidth = 4\nprintWidth = 100\nsingleQuote = true\n") == expected);
    CHECK(ParsePrettierConfig("{\"tabWidth\": \"four\"}") == PrettierOptions{});
    CHECK_FALSE(ParsePrettierConfig("{ tabWidth: 4 }").has_value()); // JSON5 keys: unreadable, not empty
}

TEST_CASE("The nearest Prettier config wins, and one written as JavaScript counts as none", "[Prettier]") {
    const TempTree tree("find");
    tree.Write("package.json", "{\"name\": \"app\", \"prettier\": {\"singleQuote\": true}}");
    tree.Write("pkg/.prettierrc.json", "{\"tabWidth\": 4}");
    tree.Write("scripted/prettier.config.js", "module.exports = {tabWidth: 8};\n");
    tree.Write("shared/package.json", "{\"prettier\": \"@company/prettier-config\"}");
    tree.Write("plain/package.json", "{\"name\": \"x\"}");

    CHECK(ned::editor::PrettierOptionsIn(tree.root) == PrettierOptions{.singleQuote = true});
    CHECK(ned::editor::PrettierOptionsIn(tree.root / "pkg") == PrettierOptions{.tabWidth = 4});
    CHECK_FALSE(ned::editor::PrettierOptionsIn(tree.root / "scripted").has_value());
    CHECK_FALSE(ned::editor::PrettierOptionsIn(tree.root / "shared").has_value());
    // A package.json without the key doesn't stop the search.
    CHECK(ned::editor::PrettierOptionsIn(tree.root / "plain") == PrettierOptions{.singleQuote = true});
}

TEST_CASE("A file Prettier formats takes indentation and line length from its config", "[Prettier][FileSettings]") {
    const TempTree tree("settings");
    tree.Write(".editorconfig", "root = true\n[*]\nindent_size = 8\nmax_line_length = 120\n");
    tree.Write(".prettierrc", "useTabs: true\ntabWidth: 3\nprintWidth: 90\n");
    const std::filesystem::path script = tree.Write("src/app.ts", "let x = 1;\n");
    const std::filesystem::path python = tree.Write("tool.py", "x = 1\n");

    ned::text::Buffer scriptBuffer("app.ts");
    scriptBuffer.SetPath(script);
    ned::editor::ApplyFileSettings(scriptBuffer);
    CHECK(scriptBuffer.LocalIndent() == ned::editor::IndentOverride{.useTabs = true, .width = 3});
    CHECK(scriptBuffer.Conventions().maxLineLength == 90);

    ned::text::Buffer pythonBuffer("tool.py");
    pythonBuffer.SetPath(python);
    ned::editor::ApplyFileSettings(pythonBuffer);
    CHECK(pythonBuffer.LocalIndent().width == 8);
    CHECK(pythonBuffer.Conventions().maxLineLength == 120);
}

TEST_CASE("A project's Prettier config sets the JavaScript and TypeScript quote style", "[Prettier][FormatBuiltinStyle]") {
    const RuleLayersGuard guard;
    const TempTree        tree("rules");

    // No config: quotes are left as written.
    ned::editor::ApplyPrettierRules(tree.root);
    CHECK_FALSE(ned::editor::RewriteRuleFor("rewrite.quote", "javascript").quoteStyle.has_value());

    tree.Write(".prettierrc.json", "{\"semi\": true}");
    ned::editor::ApplyPrettierRules(tree.root);
    CHECK(ned::editor::RewriteRuleFor("rewrite.quote", "typescript").quoteStyle == QuoteStyle::Double);

    tree.Write(".prettierrc.json", "{\"singleQuote\": true}");
    ned::editor::ApplyPrettierRules(tree.root);
    CHECK(ned::editor::RewriteRuleFor("rewrite.quote", "tsx").quoteStyle == QuoteStyle::Single);
    CHECK_FALSE(ned::editor::RewriteRuleFor("rewrite.quote", "python").quoteStyle.has_value());

    ned::editor::LoadBuiltinFormatStyles();
    const std::optional<ned::editor::Mode> mode = ned::editor::ModeByName("javascript-mode");
    REQUIRE(mode.has_value());
    ned::text::Buffer buffer("t.js");
    buffer.InsertAtPoint("const a = \"x\";\nconst b = \"it's\";\n");
    ned::editor::ApplyNativeFormat(buffer, &*mode);
    CHECK(buffer.Text() == "const a = 'x';\nconst b = \"it's\";\n");
}
