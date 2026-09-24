#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "Editor/Format.h"
#include "Editor/FormatBuiltinStyle.h"
#include "Editor/FormatRules.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

using ned::editor::ApplyNativeFormat;
using ned::editor::BracePlacement;
using ned::editor::BreakRuleFor;
using ned::editor::ClearFormatRuleLayer;
using ned::editor::FormatRuleLayer;
using ned::editor::LoadBuiltinFormatStyles;
using ned::editor::Mode;
using ned::editor::PhpMode;
using ned::editor::SetBreakBefore;
using ned::editor::SetBuiltinFormatStyleEnabled;
using ned::text::Buffer;

namespace {

struct BuiltinStyleGuard {
    ~BuiltinStyleGuard() {
        ClearFormatRuleLayer(FormatRuleLayer::Builtin);
        SetBuiltinFormatStyleEnabled(true);
        SetBreakBefore("control.keyword", std::nullopt);
    }
};

// A scratch languages/ tree holding one <lang>/style.janet.
struct StyleTree {
    std::filesystem::path root;

    StyleTree(const std::string& name, const std::string& language, const std::string& content)
        : root(std::filesystem::temp_directory_path() / name) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / language);
        std::ofstream(root / language / "style.janet") << content;
    }

    ~StyleTree() {
        std::filesystem::remove_all(root);
    }
};

std::string NativeFormatted(const std::string& source) {
    const Mode mode = PhpMode();
    Buffer     buffer("t.php");
    buffer.InsertAtPoint(source);
    ApplyNativeFormat(buffer, &mode);
    return buffer.Text();
}

} // namespace

TEST_CASE("A language's style.janet loads into the builtin layer scoped to that language", "[FormatBuiltinStyle]") {
    const BuiltinStyleGuard guard;
    const StyleTree         tree("ned_builtin_style_test_scoped", "fakelang",
                                 "{:break {\"builtin-style-test.capture\" {:placement :next-line}}}");

    LoadBuiltinFormatStyles(tree.root);
    CHECK(BreakRuleFor("builtin-style-test.capture", "fakelang").placement == BracePlacement::NextLine);
    CHECK_FALSE(BreakRuleFor("builtin-style-test.capture", "otherlang").placement.has_value());
    CHECK_FALSE(BreakRuleFor("builtin-style-test.capture").placement.has_value());
}

TEST_CASE("style.janet rejects settings that are not per-capture rules", "[FormatBuiltinStyle]") {
    const BuiltinStyleGuard guard;
    const StyleTree         tree("ned_builtin_style_test_indent", "fakelang", "{:indent {:fakelang {:width 2}}}");
    CHECK_THROWS_AS(LoadBuiltinFormatStyles(tree.root), std::runtime_error);
}

TEST_CASE("style.janet rejects a key that is already language-scoped", "[FormatBuiltinStyle]") {
    const BuiltinStyleGuard guard;
    const StyleTree         tree("ned_builtin_style_test_prefixed", "fakelang",
                                 "{:break {\"cpp/builtin-style-test.capture\" {:before true}}}");
    CHECK_THROWS_AS(LoadBuiltinFormatStyles(tree.root), std::runtime_error);
}

TEST_CASE("Every bundled style.janet loads", "[FormatBuiltinStyle]") {
    const BuiltinStyleGuard guard;
    REQUIRE_NOTHROW(LoadBuiltinFormatStyles());
    CHECK(BreakRuleFor("control.keyword", "php").before == false);
}

TEST_CASE("PHP's bundled style formats to PSR-12", "[FormatBuiltinStyle]") {
    const BuiltinStyleGuard guard;
    LoadBuiltinFormatStyles();

    CHECK(NativeFormatted("<?php\n"
                          "class Foo {\n"
                          "    public function bar($x) {\n"
                          "        if($x){\n"
                          "            a();\n"
                          "        }\n"
                          "        else {\n"
                          "            b();\n"
                          "        }\n"
                          "        try\n"
                          "        {\n"
                          "            c();\n"
                          "        }\n"
                          "        catch (E $e) {\n"
                          "        }\n"
                          "        do {\n"
                          "        }\n"
                          "        while ($x);\n"
                          "        $f = function ()\n"
                          "        {\n"
                          "            return 1;\n"
                          "        };\n"
                          "        $o = new class\n"
                          "        {\n"
                          "        };\n"
                          "        for ( $i = 0; $i < 3; $i++ ) {\n"
                          "        }\n"
                          "    }\n"
                          "}\n") ==
          "<?php\n"
          "class Foo\n"
          "{\n"
          "    public function bar($x)\n"
          "    {\n"
          "        if ($x) {\n"
          "            a();\n"
          "        } else {\n"
          "            b();\n"
          "        }\n"
          "        try {\n"
          "            c();\n"
          "        } catch (E $e) {\n"
          "        }\n"
          "        do {\n"
          "        } while ($x);\n"
          "        $f = function () {\n"
          "            return 1;\n"
          "        };\n"
          "        $o = new class {\n"
          "        };\n"
          "        for ($i = 0; $i < 3; $i++) {\n"
          "        }\n"
          "    }\n"
          "}\n");
}

TEST_CASE("PHP's bundled style never joins onto a comment", "[FormatBuiltinStyle]") {
    const BuiltinStyleGuard guard;
    LoadBuiltinFormatStyles();

    const std::string braceAfterComment = "<?php\nif ($x) // note\n{\n    f();\n}\n";
    CHECK(NativeFormatted(braceAfterComment) == braceAfterComment);

    const std::string keywordAfterComment = "<?php\nif ($x) {\n} // done\nelse {\n}\n";
    CHECK(NativeFormatted(keywordAfterComment) == keywordAfterComment);

    const std::string closerInComment = "<?php\nif ($x) {\n} // tail }\nelse {\n}\n";
    CHECK(NativeFormatted(closerInComment) == closerInComment);
}

TEST_CASE("A user rule overrides PHP's bundled style, and the toggle turns it off", "[FormatBuiltinStyle]") {
    const BuiltinStyleGuard guard;
    LoadBuiltinFormatStyles();

    SetBreakBefore("control.keyword", true);
    CHECK(NativeFormatted("<?php\nif ($x) {\n} else {\n}\n") == "<?php\nif ($x) {\n}\nelse {\n}\n");
    SetBreakBefore("control.keyword", std::nullopt);

    SetBuiltinFormatStyleEnabled(false);
    const std::string allman = "<?php\nif ($x)\n{\n}\n";
    CHECK(NativeFormatted(allman) == allman);
}
