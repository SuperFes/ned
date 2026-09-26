#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Editor/EditorConfig.h"
#include "Editor/Format.h"
#include "Editor/FormatBuiltinStyle.h"
#include "Editor/FormatRules.h"
#include "Editor/Mode.h"
#include "Editor/ModeOverrides.h"
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

std::string NativeFormatted(const std::string& source, const std::string& language = "php") {
    const std::optional<Mode> mode = ned::editor::ModeByName(language + "-mode");
    REQUIRE(mode.has_value());
    Buffer buffer("t");
    buffer.InsertAtPoint(source);
    ApplyNativeFormat(buffer, &*mode);
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

TEST_CASE("Rust's bundled style formats to rustfmt's defaults", "[FormatBuiltinStyle]") {
    const BuiltinStyleGuard guard;
    LoadBuiltinFormatStyles();

    // brace_style = SameLineWhere, control_brace_style = AlwaysSameLine,
    // blank_lines_upper_bound = 1.
    CHECK(NativeFormatted("fn add(a: i32) -> i32\n"
                          "{\n"
                          "    a\n"
                          "}\n"
                          "\n"
                          "\n"
                          "fn show<T>(x: T) where T: Debug {\n"
                          "    if x\n"
                          "    {\n"
                          "        a();\n"
                          "    }\n"
                          "    else\n"
                          "    {\n"
                          "        b();\n"
                          "    }\n"
                          "}\n"
                          "struct S\n"
                          "{\n"
                          "    a: i32,\n"
                          "}\n"
                          "impl S\n"
                          "{\n"
                          "}\n"
                          "trait Tr where Self: Sized {\n"
                          "}\n",
                          "rust") ==
          "fn add(a: i32) -> i32 {\n"
          "    a\n"
          "}\n"
          "\n"
          "fn show<T>(x: T) where T: Debug\n"
          "{\n"
          "    if x {\n"
          "        a();\n"
          "    } else {\n"
          "        b();\n"
          "    }\n"
          "}\n"
          "struct S {\n"
          "    a: i32,\n"
          "}\n"
          "impl S {\n"
          "}\n"
          "trait Tr where Self: Sized\n"
          "{\n"
          "}\n");
}

TEST_CASE("Go's bundled style formats as gofmt does", "[FormatBuiltinStyle]") {
    const BuiltinStyleGuard guard;
    LoadBuiltinFormatStyles();

    CHECK(NativeFormatted("package main\n"
                          "\n"
                          "\n"
                          "\n"
                          "func add(a int) int {\n"
                          "\tif ( a > 0 ) {\n"
                          "\t\treturn a\n"
                          "\t}\n"
                          "\treturn 0\n"
                          "}\n"
                          "\n"
                          "\n"
                          "type S struct {\n"
                          "\tA int\n"
                          "}\n",
                          "go") ==
          "package main\n"
          "\n"
          "func add(a int) int {\n"
          "\tif (a > 0) {\n"
          "\t\treturn a\n"
          "\t}\n"
          "\treturn 0\n"
          "}\n"
          "\n"
          "type S struct {\n"
          "\tA int\n"
          "}\n");
}

TEST_CASE("Kotlin's bundled style follows the official conventions", "[FormatBuiltinStyle]") {
    const BuiltinStyleGuard guard;
    LoadBuiltinFormatStyles();

    CHECK(NativeFormatted("class Box\n"
                          "{\n"
                          "    fun size(n: Int): Int\n"
                          "    {\n"
                          "        if( n > 0 )\n"
                          "        {\n"
                          "            return n\n"
                          "        }\n"
                          "        else\n"
                          "        {\n"
                          "            return 0\n"
                          "        }\n"
                          "    }\n"
                          "}\n",
                          "kotlin") ==
          "class Box {\n"
          "    fun size(n: Int): Int {\n"
          "        if (n > 0) {\n"
          "            return n\n"
          "        } else {\n"
          "            return 0\n"
          "        }\n"
          "    }\n"
          "}\n");
}

TEST_CASE("C#'s bundled style puts every brace and else on its own line", "[FormatBuiltinStyle]") {
    const BuiltinStyleGuard guard;
    LoadBuiltinFormatStyles();

    CHECK(NativeFormatted("namespace App {\n"
                          "    class Box {\n"
                          "        int Size(int n) {\n"
                          "            if(n > 0) {\n"
                          "                return n;\n"
                          "            } else {\n"
                          "                return 0;\n"
                          "            }\n"
                          "        }\n"
                          "    }\n"
                          "}\n",
                          "csharp") ==
          "namespace App\n"
          "{\n"
          "    class Box\n"
          "    {\n"
          "        int Size(int n)\n"
          "        {\n"
          "            if (n > 0)\n"
          "            {\n"
          "                return n;\n"
          "            }\n"
          "            else\n"
          "            {\n"
          "                return 0;\n"
          "            }\n"
          "        }\n"
          "    }\n"
          "}\n");
}

TEST_CASE("JavaScript and TypeScript's bundled style follows Prettier's braces and blank lines", "[FormatBuiltinStyle]") {
    const BuiltinStyleGuard guard;
    LoadBuiltinFormatStyles();

    const std::string source   = "function size(n)\n"
                                 "{\n"
                                 "  if ( n > 0 )\n"
                                 "  {\n"
                                 "    return 'n';\n"
                                 "  }\n"
                                 "  else\n"
                                 "  {\n"
                                 "    return 0;\n"
                                 "  }\n"
                                 "}\n"
                                 "\n"
                                 "\n"
                                 "\n"
                                 "class Box\n"
                                 "{\n"
                                 "}\n";
    const std::string expected = "function size(n) {\n"
                                 "  if (n > 0) {\n"
                                 "    return 'n';\n"
                                 "  } else {\n"
                                 "    return 0;\n"
                                 "  }\n"
                                 "}\n"
                                 "\n"
                                 "class Box {\n"
                                 "}\n";
    CHECK(NativeFormatted(source, "javascript") == expected);
    CHECK(NativeFormatted(source, "typescript") == expected);
    CHECK(NativeFormatted(source, "tsx") == expected);
}

TEST_CASE("A project's .editorconfig C# keys adjust C#'s bundled style", "[FormatBuiltinStyle][EditorConfig]") {
    const BuiltinStyleGuard     guard;
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "ned_builtin_style_csharp_editorconfig";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::ofstream(root / ".editorconfig") << "root = true\n"
                                             "[*.{cs,vb}]\n"
                                             "csharp_new_line_before_open_brace = types, methods:warning\n"
                                             "csharp_new_line_before_else = false\n"
                                             "csharp_new_line_before_catch = false\n"
                                             "csharp_new_line_before_finally = false\n";
    LoadBuiltinFormatStyles();
    ned::editor::ApplyEditorConfigFormatRules(root);

    CHECK(NativeFormatted("class Box\n"
                          "{\n"
                          "    int Size(int n)\n"
                          "    {\n"
                          "        if (n > 0)\n"
                          "        {\n"
                          "            return n;\n"
                          "        }\n"
                          "        else\n"
                          "        {\n"
                          "            return 0;\n"
                          "        }\n"
                          "    }\n"
                          "}\n",
                          "csharp") ==
          "class Box\n"
          "{\n"
          "    int Size(int n)\n"
          "    {\n"
          "        if (n > 0) {\n"
          "            return n;\n"
          "        } else {\n"
          "            return 0;\n"
          "        }\n"
          "    }\n"
          "}\n");

    ClearFormatRuleLayer(FormatRuleLayer::File);
    std::filesystem::remove_all(root);
}

TEST_CASE("Dart, Scala and Solidity's bundled styles keep braces on the header's line", "[FormatBuiltinStyle]") {
    const BuiltinStyleGuard guard;
    LoadBuiltinFormatStyles();

    CHECK(NativeFormatted("class C\n{\n  int m()\n  {\n    if(x > 0)\n    {\n      return 1;\n    }\n    else {\n      return 2;\n"
                          "    }\n  }\n}\n",
                          "dart") ==
          "class C {\n  int m() {\n    if (x > 0) {\n      return 1;\n    } else {\n      return 2;\n    }\n  }\n}\n");

    CHECK(NativeFormatted("class C\n{\n  def m(): Int =\n  {\n    1\n  }\n}\n", "scala") ==
          "class C {\n  def m(): Int = {\n    1\n  }\n}\n");

    CHECK(NativeFormatted("contract C\n{\n    function f() public\n    {\n        if(x > 0)\n        {\n            y = 1;\n"
                          "        }\n        else\n        {\n            y = 2;\n        }\n    }\n}\n",
                          "solidity") ==
          "contract C {\n    function f() public {\n        if (x > 0) {\n            y = 1;\n        } else {\n"
          "            y = 2;\n        }\n    }\n}\n");
}

TEST_CASE("D's bundled style is dfmt's Allman braces", "[FormatBuiltinStyle]") {
    const BuiltinStyleGuard guard;
    LoadBuiltinFormatStyles();

    CHECK(NativeFormatted("class C {\n    int m() {\n        if(x) {\n            return 1;\n        } else {\n"
                          "            return 2;\n        }\n    }\n}\n",
                          "d") ==
          "class C\n{\n    int m()\n    {\n        if (x)\n        {\n            return 1;\n        }\n        else\n"
          "        {\n            return 2;\n        }\n    }\n}\n");
}

TEST_CASE("Each bundled style formats its off-style sample to the language's own formatter or guide",
          "[FormatBuiltinStyle]") {
    const BuiltinStyleGuard guard;
    LoadBuiltinFormatStyles();

    // Tests/Format/style/input/<file>, formatted, is expected/<file>. Each
    // expected file was checked against the formatter or guide its
    // style.janet cites.
    const std::filesystem::path root = std::filesystem::path(NED_REPO_ROOT) / "Tests" / "Format" / "style";
    const auto read = [](const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in);
        std::ostringstream content;
        content << in.rdbuf();
        return content.str();
    };
    for (const auto& [file, language] : std::vector<std::pair<std::string, std::string>>{
             {"style.awk", "awk"},       {"style.clj", "clojure"},   {"style.cmake", "cmake"},
             {"style.cr", "crystal"},    {"style.css", "css"},       {"style.elm", "elm"},
             {"style.ex", "elixir"},     {"style.fish", "fish"},     {"style.gd", "gdscript"},
             {"style.gleam", "gleam"},   {"style.groovy", "groovy"}, {"style.hs", "haskell"},
             {"style.jank", "jank"},     {"style.lisp", "commonlisp"}, {"style.lua", "lua"},
             {"style.pas", "pascal"},    {"style.pl", "perl"},       {"style.proto", "proto"},
             {"style.ps1", "powershell"}, {"style.py", "python"},    {"style.R", "r"},
             {"style.rb", "ruby"},       {"style.res", "rescript"},  {"style.scss", "scss"},
             {"style.sh", "bash"},       {"style.swift", "swift"},   {"style.tf", "hcl"},
             {"style.vala", "vala"},     {"style.Caddyfile", "caddy"}, {"style.erl", "erlang"},
             {"style.fnl", "fennel"},    {"style.janet", "janet"},   {"style.jsonnet", "jsonnet"},
             {"style.just", "just"},     {"style.ml", "ocaml"},      {"style.mli", "ocaml-interface"},
             {"style.pkl", "pkl"},       {"style.purs", "purescript"}, {"style.rkt", "racket"},
             {"style.bzl", "starlark"},
         }) {
        INFO("sample: " << file);
        CHECK(NativeFormatted(read(root / "input" / file), language) == read(root / "expected" / file));
    }
}
