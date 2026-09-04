#include <catch2/catch_test_macros.hpp>

#include "Editor/Mode.h"

using ned::editor::BashMode;
using ned::editor::ClojureMode;
using ned::editor::CppMode;
using ned::editor::CssMode;
using ned::editor::JanetMode;
using ned::editor::JankMode;
using ned::editor::JavaScriptMode;
using ned::editor::PhpMode;
using ned::editor::PythonMode;
using ned::editor::RustMode;
using ned::editor::TsxMode;
using ned::editor::TypeScriptMode;

TEST_CASE("Modes with no import query configured have an empty importTarget", "[ImportTarget]") {
    CHECK_FALSE(static_cast<bool>(ned::editor::FundamentalMode().importTarget));
    CHECK_FALSE(static_cast<bool>(ned::editor::JsonMode().importTarget));
    CHECK_FALSE(static_cast<bool>(ned::editor::HtmlMode().importTarget));
    CHECK_FALSE(static_cast<bool>(ned::editor::YamlMode().importTarget));
    CHECK_FALSE(static_cast<bool>(ned::editor::TomlMode().importTarget));
    CHECK_FALSE(static_cast<bool>(ned::editor::MarkdownMode().importTarget));
}

TEST_CASE("CppMode importTarget resolves a quoted #include and strips the quotes", "[ImportTarget]") {
    const auto  mode  = CppMode();
    REQUIRE(static_cast<bool>(mode.importTarget));
    const std::string text  = "#include \"foo/bar.h\"\n";
    const auto         found = mode.importTarget(text, text.find("bar.h"));
    REQUIRE(found.has_value());
    CHECK(found->target == "foo/bar.h");
    CHECK_FALSE(found->isModulePath);
    CHECK(found->startByte == 0);
    // preproc_include's own node span includes its terminating newline in
    // this grammar -- endByte lands one past text.find('\n'), not on it.
    CHECK(found->endByte > text.find("foo/bar.h"));
    CHECK(found->endByte <= text.size());
}

TEST_CASE("CppMode importTarget resolves an angle-form #include and strips the brackets", "[ImportTarget]") {
    const auto  mode  = CppMode();
    const std::string text  = "#include <vector>\n";
    const auto         found = mode.importTarget(text, text.find("vector"));
    REQUIRE(found.has_value());
    CHECK(found->target == "vector");
    CHECK_FALSE(found->isModulePath);
}

TEST_CASE("CppMode importTarget resolves anywhere on the #include statement, not just the target", "[ImportTarget]") {
    const auto  mode  = CppMode();
    const std::string text  = "#include <vector>\n";
    const auto         found = mode.importTarget(text, text.find("#include"));
    REQUIRE(found.has_value());
    CHECK(found->target == "vector");
}

TEST_CASE("CppMode importTarget finds nothing on a line with no #include", "[ImportTarget]") {
    const auto  mode  = CppMode();
    const std::string text  = "int x = 1;\n";
    CHECK_FALSE(mode.importTarget(text, text.find("x")).has_value());
}

TEST_CASE("PythonMode importTarget resolves a dotted \"import a.b\" as a module path", "[ImportTarget]") {
    const auto  mode  = PythonMode();
    const std::string text  = "import foo.bar\n";
    const auto         found = mode.importTarget(text, text.find("foo.bar"));
    REQUIRE(found.has_value());
    CHECK(found->target == "foo.bar");
    CHECK(found->isModulePath);
}

TEST_CASE("PythonMode importTarget resolves \"from a.b import c, d\" regardless of which name point is on", "[ImportTarget]") {
    const auto  mode  = PythonMode();
    const std::string text  = "from foo.bar import baz, qux\n";
    const auto         onModule = mode.importTarget(text, text.find("foo.bar"));
    REQUIRE(onModule.has_value());
    CHECK(onModule->target == "foo.bar");

    const auto onImportedName = mode.importTarget(text, text.find("qux"));
    REQUIRE(onImportedName.has_value());
    CHECK(onImportedName->target == "foo.bar");
}

TEST_CASE("PythonMode importTarget resolves each comma-separated \"import a.b, c.d\" name independently", "[ImportTarget]") {
    const auto  mode  = PythonMode();
    const std::string text  = "import foo.bar, baz.qux\n";
    const auto         onSecond = mode.importTarget(text, text.find("baz.qux"));
    REQUIRE(onSecond.has_value());
    CHECK(onSecond->target == "baz.qux");
}

TEST_CASE("PythonMode importTarget resolves \"from . import foo\" as a level-1 relative import", "[ImportTarget]") {
    const auto  mode  = PythonMode();
    const std::string text  = "from . import foo\n";
    const auto        found = mode.importTarget(text, text.find("foo"));
    REQUIRE(found.has_value());
    CHECK(found->target.empty());
    CHECK(found->isModulePath);
    CHECK(found->relativeLevel == 1);
}

TEST_CASE("PythonMode importTarget resolves \"from .foo import bar\" as a level-1 relative import with a module suffix",
          "[ImportTarget]") {
    const auto        mode  = PythonMode();
    const std::string text  = "from .foo import bar\n";
    const auto        found = mode.importTarget(text, text.find("bar"));
    REQUIRE(found.has_value());
    CHECK(found->target == "foo");
    CHECK(found->isModulePath);
    CHECK(found->relativeLevel == 1);
}

TEST_CASE("PythonMode importTarget resolves \"from ..foo.bar import baz\" as a level-2 relative import", "[ImportTarget]") {
    const auto        mode  = PythonMode();
    const std::string text  = "from ..foo.bar import baz\n";
    const auto        found = mode.importTarget(text, text.find("baz"));
    REQUIRE(found.has_value());
    CHECK(found->target == "foo.bar");
    CHECK(found->relativeLevel == 2);
}

TEST_CASE("PythonMode importTarget leaves relativeLevel at 0 for an ordinary absolute import", "[ImportTarget]") {
    const auto        mode  = PythonMode();
    const std::string text  = "import foo.bar\n";
    const auto        found = mode.importTarget(text, text.find("foo.bar"));
    REQUIRE(found.has_value());
    CHECK(found->relativeLevel == 0);
}

TEST_CASE("JavaScriptMode importTarget resolves a quoted import source without quotes", "[ImportTarget]") {
    const auto  mode  = JavaScriptMode();
    const std::string text  = "import x from './foo';\n";
    const auto         found = mode.importTarget(text, text.find("./foo"));
    REQUIRE(found.has_value());
    CHECK(found->target == "./foo");
    CHECK_FALSE(found->isModulePath);
}

TEST_CASE("JavaScriptMode importTarget resolves require(...) via the callee predicate", "[ImportTarget]") {
    const auto  mode  = JavaScriptMode();
    const std::string text  = "const x = require('lodash');\n";
    const auto         found = mode.importTarget(text, text.find("lodash"));
    REQUIRE(found.has_value());
    CHECK(found->target == "lodash");
}

TEST_CASE("JavaScriptMode importTarget does not match an unrelated call", "[ImportTarget]") {
    const auto  mode  = JavaScriptMode();
    const std::string text  = "const x = notrequire('lodash');\n";
    CHECK_FALSE(mode.importTarget(text, text.find("lodash")).has_value());
}

TEST_CASE("JavaScriptMode importTarget resolves a dynamic import(...)", "[ImportTarget]") {
    const auto        mode  = JavaScriptMode();
    const std::string text  = "const x = import('./foo');\n";
    const auto        found = mode.importTarget(text, text.find("./foo"));
    REQUIRE(found.has_value());
    CHECK(found->target == "./foo");
    CHECK_FALSE(found->isModulePath);
}

TEST_CASE("TypeScriptMode importTarget resolves \"import x = require(...)\"", "[ImportTarget]") {
    const auto  mode  = TypeScriptMode();
    const std::string text  = "import x = require(\"./foo\");\n";
    const auto         found = mode.importTarget(text, text.find("./foo"));
    REQUIRE(found.has_value());
    CHECK(found->target == "./foo");
}

TEST_CASE("TsxMode importTarget resolves an ordinary import source", "[ImportTarget]") {
    const auto  mode  = TsxMode();
    const std::string text  = "import { Foo } from './foo';\n";
    const auto         found = mode.importTarget(text, text.find("./foo"));
    REQUIRE(found.has_value());
    CHECK(found->target == "./foo");
}

TEST_CASE("TypeScriptMode importTarget resolves a dynamic import(...)", "[ImportTarget]") {
    const auto        mode  = TypeScriptMode();
    const std::string text  = "const x = import('./foo');\n";
    const auto        found = mode.importTarget(text, text.find("./foo"));
    REQUIRE(found.has_value());
    CHECK(found->target == "./foo");
}

TEST_CASE("PhpMode importTarget resolves require_once with a single-quoted string", "[ImportTarget]") {
    const auto  mode  = PhpMode();
    const std::string text  = "<?php\nrequire_once 'foo.php';\n";
    const auto         found = mode.importTarget(text, text.find("foo.php"));
    REQUIRE(found.has_value());
    CHECK(found->target == "foo.php");
}

TEST_CASE("PhpMode importTarget resolves a \"use\" namespace as a namespace path", "[ImportTarget]") {
    const auto        mode  = PhpMode();
    const std::string text  = "<?php\nuse App\\Models\\User;\n";
    const auto        found = mode.importTarget(text, text.find("User"));
    REQUIRE(found.has_value());
    CHECK(found->target == "App\\Models\\User");
    CHECK(found->isNamespacePath);
    CHECK_FALSE(found->isModulePath);
}

TEST_CASE("BashMode importTarget resolves \"source ./foo.sh\"", "[ImportTarget]") {
    const auto  mode  = BashMode();
    const std::string text  = "source ./foo.sh\n";
    const auto         found = mode.importTarget(text, text.find("./foo.sh"));
    REQUIRE(found.has_value());
    CHECK(found->target == "./foo.sh");
}

TEST_CASE("BashMode importTarget resolves \". ./foo.sh\"", "[ImportTarget]") {
    const auto  mode  = BashMode();
    const std::string text  = ". ./foo.sh\n";
    const auto         found = mode.importTarget(text, text.find("./foo.sh"));
    REQUIRE(found.has_value());
    CHECK(found->target == "./foo.sh");
}

TEST_CASE("BashMode importTarget does not match an unrelated command", "[ImportTarget]") {
    const auto  mode  = BashMode();
    const std::string text  = "echo ./foo.sh\n";
    CHECK_FALSE(mode.importTarget(text, text.find("./foo.sh")).has_value());
}

TEST_CASE("CssMode importTarget resolves a quoted @import", "[ImportTarget]") {
    const auto  mode  = CssMode();
    const std::string text  = "@import \"foo.css\";\n";
    const auto         found = mode.importTarget(text, text.find("foo.css"));
    REQUIRE(found.has_value());
    CHECK(found->target == "foo.css");
}

TEST_CASE("ClojureMode importTarget resolves a quoted-symbol require as a module path", "[ImportTarget]") {
    const auto  mode  = ClojureMode();
    const std::string text  = "(require 'foo.bar)\n";
    const auto         found = mode.importTarget(text, text.find("foo.bar"));
    REQUIRE(found.has_value());
    CHECK(found->target == "foo.bar");
    CHECK(found->isModulePath);
}

TEST_CASE("JankMode importTarget resolves the same require shape as ClojureMode", "[ImportTarget]") {
    const auto  mode  = JankMode();
    const std::string text  = "(require 'foo.bar)\n";
    const auto         found = mode.importTarget(text, text.find("foo.bar"));
    REQUIRE(found.has_value());
    CHECK(found->target == "foo.bar");
}

TEST_CASE("JanetMode importTarget resolves (import foo)", "[ImportTarget]") {
    const auto  mode  = JanetMode();
    const std::string text  = "(import foo/bar)\n";
    const auto         found = mode.importTarget(text, text.find("foo/bar"));
    REQUIRE(found.has_value());
    CHECK(found->target == "foo/bar");
    CHECK_FALSE(found->isModulePath);
}

TEST_CASE("JanetMode importTarget resolves (require \"foo\")", "[ImportTarget]") {
    const auto  mode  = JanetMode();
    const std::string text  = "(require \"foo\")\n";
    const auto         found = mode.importTarget(text, text.find("foo"));
    REQUIRE(found.has_value());
    CHECK(found->target == "foo");
}

TEST_CASE("RustMode importTarget resolves a bodyless \"mod foo;\" declaration", "[ImportTarget]") {
    const auto        mode  = RustMode();
    const std::string text  = "mod foo;\n";
    const auto        found = mode.importTarget(text, text.find("foo"));
    REQUIRE(found.has_value());
    CHECK(found->target == "foo");
    CHECK(found->isModDeclaration);
    CHECK_FALSE(found->isModulePath);
    CHECK_FALSE(found->isNamespacePath);
}

TEST_CASE("RustMode importTarget does not match an inline \"mod foo { ... }\" definition", "[ImportTarget]") {
    const auto        mode = RustMode();
    const std::string text = "mod foo {\n    fn bar() {}\n}\n";
    CHECK_FALSE(mode.importTarget(text, text.find("foo")).has_value());
}

TEST_CASE("RustMode importTarget does not match a \"use\" path", "[ImportTarget]") {
    const auto        mode = RustMode();
    const std::string text = "use foo::bar::Baz;\n";
    CHECK_FALSE(mode.importTarget(text, text.find("Baz")).has_value());
}
