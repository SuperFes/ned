//
// Every bundled imports query, one line of real source each: what
// go-to-file reads at point, and that a line naming no one file reads as
// nothing. Then the per-language resolution knobs (source roots, module
// separators, substitutions, index files, partials, root prefixes, Dart
// packages, Go modules) against real files, both for go-to-file and a rename's
// fixups.
//

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "Editor/ImportFixup.h"
#include "Editor/ImportResolve.h"
#include "Editor/ModeOverrides.h"
#include "Editor/Project/Root.h"
#include "Editor/ToolchainIncludePaths.h"

namespace {

struct ImportCase {
    const char* language;
    const char* source;
    const char* at;
    const char* target;
    bool        isModule;
};

const ImportCase kImportCases[] = {
    {"ruby", "require_relative \"foo/bar\"\n", "foo/bar", "foo/bar", false},
    {"ruby", "require \"json\"\n", "require", "json", false},
    {"crystal", "require \"./foo\"\n", "foo", "./foo", false},
    {"julia", "include(\"x.jl\")\n", "x.jl", "x.jl", false},
    {"r", "source(\"x.R\")\n", "source", "x.R", false},
    {"nix", "import ./foo.nix { }\n", "foo", "./foo.nix", false},
    {"latex", "\\input{chapters/one}\n", "one", "chapters/one", false},
    {"latex", "\\bibliography{refs}\n", "refs", "refs", false},
    {"typst", "#import \"x.typ\": a\n", "x.typ", "x.typ", false},
    {"typst", "#include \"y.typ\"\n", "y.typ", "y.typ", false},
    {"proto", "import \"a/b.proto\";\n", "b.proto", "a/b.proto", false},
    {"thrift", "include \"shared.thrift\"\n", "shared", "shared.thrift", false},
    {"jsonnet", "local a = import \"a.libsonnet\";\na\n", "a.libsonnet", "a.libsonnet", false},
    {"just", "import 'x.just'\n", "x.just", "x.just", false},
    {"just", "mod foo 'bar/baz.just'\n", "baz", "bar/baz.just", false},
    {"cmake", "include(cmake/bar.cmake)\n", "bar", "cmake/bar.cmake", false},
    {"cmake", "add_subdirectory(\"lib\")\n", "lib", "lib", false},
    {"make", "include a.mk b.mk\n", "b.mk", "b.mk", false},
    {"fish", "source ./x.fish\n", "x.fish", "./x.fish", false},
    {"tcl", "source lib/x.tcl\n", "x.tcl", "lib/x.tcl", false},
    {"nu", "use lib/y.nu\n", "y.nu", "lib/y.nu", false},
    {"nu", "source x.nu\n", "x.nu", "x.nu", false},
    {"racket", "#lang racket\n(require \"x.rkt\" racket/list)\n", "x.rkt", "x.rkt", false},
    {"racket", "#lang racket\n(require (file \"y.rkt\"))\n", "y.rkt", "y.rkt", false},
    {"scheme", "(load \"x.scm\")\n", "x.scm", "x.scm", false},
    {"commonlisp", "(load \"x.lisp\")\n", "x.lisp", "x.lisp", false},
    {"awk", "@include \"lib.awk\"\n", "lib.awk", "lib.awk", false},
    {"fsharp", "#load \"x.fsx\"\n", "x.fsx", "x.fsx", false},
    {"erlang", "-include(\"x.hrl\").\n", "x.hrl", "x.hrl", false},
    {"fortran", "include 'x.inc'\n", "x.inc", "x.inc", false},
    {"verilog", "`include \"x.vh\"\n", "x.vh", "x.vh", false},
    {"solidity", "import {A} from \"../y.sol\";\n", "y.sol", "../y.sol", false},
    {"powershell", ". ./x.ps1\n", "x.ps1", "./x.ps1", false},
    {"powershell", "Import-Module ./z.psm1\n", "z.psm1", "./z.psm1", false},
    {"pkl", "amends \"base.pkl\"\n", "base", "base.pkl", false},
    {"pkl", "import \"x.pkl\"\n", "x.pkl", "x.pkl", false},
    {"asciidoc", "include::chapter.adoc[]\n", "chapter", "chapter.adoc", false},
    {"rst", ".. include:: other.rst\n", "other", "other.rst", false},
    {"caddy", "example.com {\n\timport snippets/common.caddy\n}\n", "common", "snippets/common.caddy", false},
    {"nginx", "include mime.types;\n", "mime", "mime.types", false},
    {"apacheconf", "Include conf/extra.conf\n", "extra", "conf/extra.conf", false},
    {"ssh_config", "Include other.conf\n", "other", "other.conf", false},
    {"gitconfig", "[include]\n\tpath = extra.gitconfig\n", "extra", "extra.gitconfig", false},
    {"gdscript", "extends \"base.gd\"\n", "base", "base.gd", false},
    {"gdscript", "const A = preload(\"a.gd\")\n", "a.gd", "a.gd", false},
    {"hcl", "module \"vpc\" {\n  source = \"./modules/vpc\"\n}\n", "modules", "./modules/vpc", false},
    {"dart", "import 'foo.dart';\n", "foo", "foo.dart", false},
    {"dart", "part 'z.dart';\n", "z.dart", "z.dart", false},
    {"nim", "import std/strutils, ./foo, bar\n", "foo", "./foo", false},
    {"nim", "import std/strutils, ./foo, bar\n", "strutils", "std/strutils", false},
    {"nim", "include inc\n", "inc", "inc", false},
    {"gleam", "import app/foo.{bar}\n", "foo", "app/foo", false},
    {"scss", "@use 'base/vars' as v;\n", "vars", "base/vars", false},
    {"scss", "@forward 'src/list' hide list-reset;\n", "list", "src/list", false},
    {"gdscript", "const A = preload(\"res://a.gd\")\n", "a.gd", "res://a.gd", false},
    {"dart", "import 'package:app/x.dart';\n", "x.dart", "package:app/x.dart", false},
    {"objc", "#import \"x.h\"\n", "x.h", "x.h", false},
    {"cuda", "#include \"x.cuh\"\n", "x.cuh", "x.cuh", false},
    {"hlsl", "#include \"x.hlsli\"\n", "x.hlsli", "x.hlsli", false},
    {"glsl", "#include \"x.glsl\"\n", "x.glsl", "x.glsl", false},
    {"lua", "local a = require(\"a.b\")\n", "a.b", "a.b", true},
    {"lua", "local c = require \"c\"\n", "require", "c", true},
    {"lua", "dofile(\"e.lua\")\n", "e.lua", "e.lua", false},
    {"fennel", "(local a (require :a.b))\n", "a.b", "a.b", true},
    {"fennel", "(require \"c.d\")\n", "c.d", "c.d", true},
    {"fennel", "(import-macros m :e.f)\n", "e.f", "e.f", true},
    {"haskell", "module Main where\nimport qualified Data.Bar as B\n", "Bar", "Data.Bar", true},
    {"elm", "module Main exposing (..)\nimport Data.Foo\n", "Foo", "Data.Foo", true},
    {"purescript", "module Main where\nimport Data.Foo\n", "Foo", "Data.Foo", true},
    {"d", "import a.b, c;\n", "a.b", "a.b", true},
    {"d", "import x = foo.bar;\n", "bar", "foo.bar", true},
    {"perl", "use Foo::Bar;\n", "Bar", "Foo::Bar", true},
    {"perl", "require Qux::Quux;\n", "Quux", "Qux::Quux", true},
    {"perl", "require \"x.pl\";\n", "x.pl", "x.pl", false},
    {"java", "import a.b.C;\n", "C", "a.b.C", true},
    {"java", "import static a.b.C.m;\n", "m;", "a.b.C", true},
    {"kotlin", "import a.b.C\n", "C", "a.b.C", true},
    {"kotlin", "import a.b.C as D\n", "D", "a.b.C", true},
    {"scala", "import a.b.C\n", "C", "a.b.C", true},
    {"groovy", "import a.b.C\n", "C", "a.b.C", true},
    {"groovy", "apply from: \"x.gradle\"\n", "x.gradle", "x.gradle", false},
    {"clojure", "(require 'my-app.core)\n", "core", "my-app.core", true},
    {"go", "import \"example.com/m/store\"\n", "store", "example.com/m/store", false},
    {"go", "import (\n\tf \"fmt\"\n\t\"net/http\"\n)\n", "http", "net/http", false},
    {"odin", "package main\nimport u \"../shared/util\"\n", "util", "../shared/util", false},
    {"v", "module main\nimport foo.bar as b\n", "bar", "foo.bar", true}};

struct NoImportCase {
    const char* language;
    const char* source;
    const char* at;
};

const NoImportCase kNoImportCases[] = {
    {"java", "import a.b.*;\n", "a.b"},
    {"kotlin", "import a.b.*\n", "a.b"},
    {"scala", "import a.b.{C, D}\n", "a.b"},
    {"nim", "import pkg/[a, b]\n", "pkg"},
    {"ruby", "puts \"x.rb\"\n", "x.rb"},
    {"awk", "@namespace \"ns\"\n", "ns"},
    {"fsharp", "#r \"y.dll\"\n", "y.dll"},
    {"cmake", "message(\"lib\")\n", "lib"}};

} // namespace

TEST_CASE("Each language's imports query reads the target at point", "[ImportLanguages]") {
    for (const ImportCase& importCase : kImportCases) {
        const std::string source = importCase.source;
        INFO(importCase.language << ": " << source);
        const std::optional<ned::editor::Mode> mode = ned::editor::ModeByName(std::string(importCase.language) + "-mode");
        REQUIRE(mode.has_value());
        REQUIRE(static_cast<bool>(mode->importTarget));
        const std::size_t at = source.find(importCase.at);
        REQUIRE(at != std::string::npos);
        const auto found = mode->importTarget(source, at);
        REQUIRE(found.has_value());
        CHECK(found->target == importCase.target);
        CHECK(found->isModulePath == importCase.isModule);
        CHECK(source.substr(found->targetStartByte, found->targetEndByte - found->targetStartByte) == importCase.target);
    }
}

TEST_CASE("A line that names no one file reads as no import", "[ImportLanguages]") {
    for (const NoImportCase& noImport : kNoImportCases) {
        const std::string source = noImport.source;
        INFO(noImport.language << ": " << source);
        const std::optional<ned::editor::Mode> mode = ned::editor::ModeByName(std::string(noImport.language) + "-mode");
        REQUIRE(mode.has_value());
        REQUIRE(static_cast<bool>(mode->importTarget));
        CHECK_FALSE(mode->importTarget(source, source.find(noImport.at)).has_value());
    }
}

//
// Resolution and rename fixups: real files on disk under a scratch project
// root, since both are on-disk questions.
//

namespace {

namespace fs = std::filesystem;

void Write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

std::string Read(const fs::path& path) {
    std::ifstream      in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

struct ScratchProject {
    fs::path root;
    fs::path previousRoot;
    bool     previousAutoDetect;

    explicit ScratchProject(const std::string& name) : root(fs::temp_directory_path() / ("ned_import_languages_test_" + name)),
                                                       previousRoot(ned::editor::ProjectRoot()), previousAutoDetect(ned::editor::AutoDetectProjectRoot()) {
        fs::remove_all(root);
        fs::create_directories(root);
        ned::editor::SetProjectRoot(root);
        ned::editor::SetAutoDetectProjectRoot(true);
    }
    ~ScratchProject() {
        ned::editor::SetProjectRoot(previousRoot);
        ned::editor::SetAutoDetectProjectRoot(previousAutoDetect);
        fs::remove_all(root);
    }
    ScratchProject(const ScratchProject&)            = delete;
    ScratchProject& operator=(const ScratchProject&) = delete;
};

// What go-to-file opens from `at` in `file`, whose text is already on disk.
std::optional<fs::path> ResolveAt(const fs::path& file, const std::string& at) {
    const ned::editor::Mode mode   = ned::editor::ModeForPath(file);
    const std::string       text   = Read(file);
    const auto              target = mode.importTarget(text, text.find(at));
    if (!target) {
        return std::nullopt;
    }
    const auto resolved = ned::editor::ResolveImportLink(
        ned::editor::ImportLinkFor(*target, ned::editor::ImportResolutionConfigFor(mode)), file, mode);
    return resolved ? std::optional<fs::path>(resolved->path.lexically_normal()) : std::nullopt;
}

// `file`'s text after planning `moved` and applying its fixups -- found under
// `as` when the file itself moves.
std::string FixedText(const std::vector<ned::editor::importfix::MovedFile>& moved, const fs::path& file,
                      const fs::path& as = {}) {
    const auto reader = [](const fs::path& path) -> std::optional<std::string> {
        if (!fs::exists(path)) {
            return std::nullopt;
        }
        return Read(path);
    };
    const ned::editor::importfix::FixupPlan plan = ned::editor::importfix::PlanImportFixups(moved, {file}, reader);
    for (const auto& fixup : plan.files) {
        if (fixup.file == (as.empty() ? file : as)) {
            return ned::editor::importfix::ApplyFixup(fixup);
        }
    }
    return Read(file);
}

} // namespace

TEST_CASE("A module path is spelled as the file path it stands for", "[ImportLanguages]") {
    ned::editor::ImportResolutionConfig config;
    CHECK(ned::editor::ModulePathToFilePath("pkg.mod", config) == "pkg/mod");
    CHECK(ned::editor::ModulePathToFilePath("", config).empty());

    config.moduleSeparator = "::";
    CHECK(ned::editor::ModulePathToFilePath("Foo::Bar::Baz", config) == "Foo/Bar/Baz");
    CHECK(ned::editor::ModulePathToFilePath("Foo.pm", config) == "Foo.pm");

    ned::editor::ImportResolutionConfig clojure;
    clojure.moduleSubstitutions = {{"-", "_"}};
    CHECK(ned::editor::ModulePathToFilePath("my-app.core-util", clojure) == "my_app/core_util");
}

TEST_CASE("A module resolves under its language's source roots", "[ImportLanguages]") {
    const ScratchProject project("source_roots");
    Write(project.root / "pom.xml", "");
    Write(project.root / "src/main/java/a/b/C.java", "package a.b;\n");
    Write(project.root / "src/main/java/a/x/D.java", "package a.x;\nimport a.b.C;\n");
    CHECK(ResolveAt(project.root / "src/main/java/a/x/D.java", "C;") == project.root / "src/main/java/a/b/C.java");
}

TEST_CASE("Source roots are found under the importing file's own package first", "[ImportLanguages]") {
    const ScratchProject project("package_root");
    Write(project.root / "services/api/cpanfile", "");
    Write(project.root / "services/api/lib/Api/Model.pm", "package Api::Model;\n1;\n");
    Write(project.root / "services/api/bin/serve.pl", "use Api::Model;\n");
    CHECK(ResolveAt(project.root / "services/api/bin/serve.pl", "Model") ==
          project.root / "services/api/lib/Api/Model.pm");
}

TEST_CASE("Clojure namespaces resolve through their munged file names", "[ImportLanguages]") {
    const ScratchProject project("clojure_munge");
    Write(project.root / "deps.edn", "{}");
    Write(project.root / "src/my_app/core_util.clj", "(ns my-app.core-util)\n");
    Write(project.root / "src/my_app/main.clj", "(ns my-app.main (:require [my-app.core-util]))\n(require 'my-app.core-util)\n");
    CHECK(ResolveAt(project.root / "src/my_app/main.clj", "(require 'my-app") ==
          project.root / "src/my_app/core_util.clj");
}

TEST_CASE("Index files resolve a directory import", "[ImportLanguages]") {
    const ScratchProject project("index_files");
    Write(project.root / "lib/CMakeLists.txt", "add_library(lib x.c)\n");
    Write(project.root / "CMakeLists.txt", "add_subdirectory(lib)\n");
    CHECK(ResolveAt(project.root / "CMakeLists.txt", "lib") == project.root / "lib/CMakeLists.txt");

    Write(project.root / "pkgs/tool/default.nix", "{ }\n");
    Write(project.root / "default.nix", "import ./pkgs/tool\n");
    CHECK(ResolveAt(project.root / "default.nix", "tool") == project.root / "pkgs/tool/default.nix");

    Write(project.root / "lua/plugin/init.lua", "return {}\n");
    Write(project.root / "main.lua", "local p = require(\"plugin\")\n");
    CHECK(ResolveAt(project.root / "main.lua", "plugin") == project.root / "lua/plugin/init.lua");
}

TEST_CASE("A Perl module's users follow it when it moves", "[ImportLanguages]") {
    const ScratchProject project("perl_fixup");
    Write(project.root / "lib/Foo/Bar.pm", "package Foo::Bar;\n1;\n");
    Write(project.root / "script.pl", "use Foo::Bar;\n");
    const std::vector<ned::editor::importfix::MovedFile> moved{
        {project.root / "lib/Foo/Bar.pm", project.root / "lib/Foo/Moved/Bar.pm"}};
    CHECK(FixedText(moved, project.root / "script.pl") == "use Foo::Moved::Bar;\n");
}

TEST_CASE("A Clojure namespace's users follow it in its own spelling", "[ImportLanguages]") {
    const ScratchProject project("clojure_fixup");
    Write(project.root / "deps.edn", "{}");
    Write(project.root / "src/my_app/core_util.clj", "(ns my-app.core-util)\n");
    Write(project.root / "src/my_app/main.clj", "(require 'my-app.core-util)\n");
    const std::vector<ned::editor::importfix::MovedFile> moved{
        {project.root / "src/my_app/core_util.clj", project.root / "src/my_app/text_util/core_util.clj"}};
    CHECK(FixedText(moved, project.root / "src/my_app/main.clj") == "(require 'my-app.text-util.core-util)\n");
}

TEST_CASE("A Java class's importers follow it to another package", "[ImportLanguages]") {
    const ScratchProject project("java_fixup");
    Write(project.root / "pom.xml", "");
    Write(project.root / "src/main/java/a/b/C.java", "package a.b;\n");
    Write(project.root / "src/main/java/a/x/D.java", "package a.x;\nimport a.b.C;\nimport static a.b.C.m;\n");
    const std::vector<ned::editor::importfix::MovedFile> moved{
        {project.root / "src/main/java/a/b/C.java", project.root / "src/main/java/a/c/C.java"}};
    CHECK(FixedText(moved, project.root / "src/main/java/a/x/D.java") ==
          "package a.x;\nimport a.c.C;\nimport static a.c.C.m;\n");
}

TEST_CASE("A Gleam module path stays root-relative and extensionless", "[ImportLanguages]") {
    const ScratchProject project("gleam_fixup");
    Write(project.root / "gleam.toml", "");
    Write(project.root / "src/app/foo.gleam", "pub fn bar() { 1 }\n");
    Write(project.root / "src/app.gleam", "import app/foo\n");
    const std::vector<ned::editor::importfix::MovedFile> moved{
        {project.root / "src/app/foo.gleam", project.root / "src/app/util/foo.gleam"}};
    CHECK(FixedText(moved, project.root / "src/app.gleam") == "import app/util/foo\n");
}

TEST_CASE("A Nix path follows a moved file and keeps its ./", "[ImportLanguages]") {
    const ScratchProject project("nix_fixup");
    Write(project.root / "pkgs/tool.nix", "{ }\n");
    Write(project.root / "default.nix", "import ./pkgs/tool.nix\n");
    const std::vector<ned::editor::importfix::MovedFile> moved{
        {project.root / "pkgs/tool.nix", project.root / "pkgs/tools/tool.nix"}};
    CHECK(FixedText(moved, project.root / "default.nix") == "import ./pkgs/tools/tool.nix\n");
}

TEST_CASE("A Godot res:// path resolves from the project, wherever the script is", "[ImportLanguages]") {
    const ScratchProject project("gdscript_res");
    Write(project.root / "game/project.godot", "");
    Write(project.root / "game/ui/menu.gd", "extends Node\n");
    Write(project.root / "game/scenes/level/main.gd", "const Menu = preload(\"res://ui/menu.gd\")\n");
    CHECK(ResolveAt(project.root / "game/scenes/level/main.gd", "menu") == project.root / "game/ui/menu.gd");

    const std::vector<ned::editor::importfix::MovedFile> moved{
        {project.root / "game/ui/menu.gd", project.root / "game/ui/menus/menu.gd"}};
    CHECK(FixedText(moved, project.root / "game/scenes/level/main.gd") ==
          "const Menu = preload(\"res://ui/menus/menu.gd\")\n");
}

TEST_CASE("A res:// import doesn't follow its importer when the importer moves", "[ImportLanguages]") {
    const ScratchProject project("gdscript_res_importer");
    Write(project.root / "project.godot", "");
    Write(project.root / "ui/menu.gd", "extends Node\n");
    Write(project.root / "main.gd", "const Menu = preload(\"res://ui/menu.gd\")\n");
    const std::vector<ned::editor::importfix::MovedFile> moved{
        {project.root / "main.gd", project.root / "scenes/main.gd"}};
    CHECK(FixedText(moved, project.root / "main.gd") == "const Menu = preload(\"res://ui/menu.gd\")\n");
}

TEST_CASE("A Dart package: URI resolves through the package's own pubspec", "[ImportLanguages]") {
    const ScratchProject project("dart_pubspec");
    Write(project.root / "pubspec.yaml", "name: my_app # the app\nversion: 1.0.0\n");
    Write(project.root / "lib/src/model.dart", "class Model {}\n");
    Write(project.root / "test/model_test.dart", "import 'package:my_app/src/model.dart';\n");
    CHECK(ResolveAt(project.root / "test/model_test.dart", "model.dart") == project.root / "lib/src/model.dart");

    Write(project.root / "test/other_test.dart", "import 'package:other/x.dart';\n");
    CHECK_FALSE(ResolveAt(project.root / "test/other_test.dart", "x.dart").has_value());

    const std::vector<ned::editor::importfix::MovedFile> moved{
        {project.root / "lib/src/model.dart", project.root / "lib/src/data/model.dart"}};
    CHECK(FixedText(moved, project.root / "test/model_test.dart") ==
          "import 'package:my_app/src/data/model.dart';\n");
}

TEST_CASE("A Dart package: URI resolves through package_config.json", "[ImportLanguages]") {
    const ScratchProject project("dart_package_config");
    Write(project.root / "app/pubspec.yaml", "name: app\n");
    Write(project.root / "shared/lib/util.dart", "void util() {}\n");
    Write(project.root / "app/.dart_tool/package_config.json",
          "{\"configVersion\": 2, \"packages\": ["
          "{\"name\": \"shared\", \"rootUri\": \"../../shared\", \"packageUri\": \"lib/\"},"
          "{\"name\": \"app\", \"rootUri\": \"../\", \"packageUri\": \"lib/\"}]}");
    Write(project.root / "app/lib/main.dart", "import 'package:shared/util.dart';\n");
    CHECK(ResolveAt(project.root / "app/lib/main.dart", "util") == project.root / "shared/lib/util.dart");
}

TEST_CASE("A Sass partial resolves and keeps its unwritten underscore", "[ImportLanguages]") {
    const ScratchProject project("scss_partial");
    Write(project.root / "base/_vars.scss", "$x: 1 !default;\n");
    Write(project.root / "theme/_index.scss", "$y: 2;\n");
    Write(project.root / "main.scss", "@use 'base/vars' as v;\n@use 'theme';\n");
    CHECK(ResolveAt(project.root / "main.scss", "vars") == project.root / "base/_vars.scss");
    CHECK(ResolveAt(project.root / "main.scss", "theme") == project.root / "theme/_index.scss");

    const std::vector<ned::editor::importfix::MovedFile> moved{
        {project.root / "base/_vars.scss", project.root / "abstracts/_vars.scss"}};
    CHECK(FixedText(moved, project.root / "main.scss") == "@use 'abstracts/vars' as v;\n@use 'theme';\n");
}

TEST_CASE("A directory import keeps naming the directory when its index moves", "[ImportLanguages]") {
    const ScratchProject project("cmake_subdirectory");
    Write(project.root / "lib/CMakeLists.txt", "add_library(lib x.c)\n");
    Write(project.root / "CMakeLists.txt", "add_subdirectory(lib)\n");
    const std::vector<ned::editor::importfix::MovedFile> moved{
        {project.root / "lib/CMakeLists.txt", project.root / "libs/core/CMakeLists.txt"}};
    CHECK(FixedText(moved, project.root / "CMakeLists.txt") == "add_subdirectory(libs/core)\n");
}

namespace {

class EnvVarGuard {
  public:
    EnvVarGuard(const char* name, const std::string& value) : name_(name) {
        if (const char* existing = std::getenv(name)) {
            previous_ = existing;
        }
        setenv(name, value.c_str(), 1);
    }
    ~EnvVarGuard() {
        if (previous_) {
            setenv(name_.c_str(), previous_->c_str(), 1);
        }
        else {
            unsetenv(name_.c_str());
        }
    }
    EnvVarGuard(const EnvVarGuard&)            = delete;
    EnvVarGuard& operator=(const EnvVarGuard&) = delete;

  private:
    std::string                name_;
    std::optional<std::string> previous_;
};

} // namespace

TEST_CASE("A Go import opens the file that stands for its package directory", "[ImportLanguages]") {
    const ScratchProject project("go_module");
    Write(project.root / "go.mod", "module example.com/m\n");
    Write(project.root / "store/store.go", "package store\n");
    Write(project.root / "store/doc.go", "// Package store.\npackage store\n");
    Write(project.root / "api/b.go", "package api\n");
    Write(project.root / "api/a_test.go", "package api\n");
    Write(project.root / "api/c.go", "package api\n");
    Write(project.root / "cmd/tool/main.go", "package main\nimport (\n\t\"example.com/m/store\"\n\t\"example.com/m/api\"\n)\n");
    const fs::path main = project.root / "cmd/tool/main.go";
    CHECK(ResolveAt(main, "store") == project.root / "store/doc.go");
    CHECK(ResolveAt(main, "api") == project.root / "api/b.go");

    fs::remove(project.root / "store/doc.go");
    CHECK(ResolveAt(main, "store") == project.root / "store/store.go");
}

TEST_CASE("A Go import resolves through the standard library, module cache and replacements", "[ImportLanguages]") {
    const ScratchProject project("go_roots");
    const EnvVarGuard    goRoot("GOROOT", (project.root / "goroot").string());
    const EnvVarGuard    modCache("GOMODCACHE", (project.root / "modcache").string());
    Write(project.root / "goroot/src/net/http/doc.go", "package http\n");
    Write(project.root / "modcache/github.com/!burnt!sushi/toml@v1.3.2/encode.go", "package toml\n");
    Write(project.root / "modcache/github.com/!burnt!sushi/toml@v1.3.2/decode.go", "package toml\n");
    Write(project.root / "lib/util/util.go", "package util\n");
    Write(project.root / "app/go.mod", "module example.com/app\n"
                                       "require (\n\tgithub.com/BurntSushi/toml v1.3.2\n\texample.com/lib v0.0.0\n)\n"
                                       "replace example.com/lib => ../lib\n");
    Write(project.root / "app/main.go", "package main\nimport (\n\t\"net/http\"\n\t\"github.com/BurntSushi/toml\"\n"
                                        "\t\"example.com/lib/util\"\n\t\"example.com/missing/pkg\"\n)\n");
    const fs::path main = project.root / "app/main.go";
    CHECK(ResolveAt(main, "http") == project.root / "goroot/src/net/http/doc.go");
    CHECK(ResolveAt(main, "toml") == project.root / "modcache/github.com/!burnt!sushi/toml@v1.3.2/decode.go");
    CHECK(ResolveAt(main, "util") == project.root / "lib/util/util.go");
    CHECK_FALSE(ResolveAt(main, "missing").has_value());
}

TEST_CASE("A Go import follows its package directory only when the whole package moves", "[ImportLanguages]") {
    const ScratchProject project("go_move");
    Write(project.root / "go.mod", "module example.com/m\n");
    Write(project.root / "store/a.go", "package store\n");
    Write(project.root / "store/b.go", "package store\n");
    Write(project.root / "store/a_test.go", "package store\n");
    Write(project.root / "main.go", "package main\nimport \"example.com/m/store\"\n");
    const fs::path main = project.root / "main.go";

    const std::vector<ned::editor::importfix::MovedFile> whole{
        {project.root / "store/a.go", project.root / "internal/store/a.go"},
        {project.root / "store/b.go", project.root / "internal/store/b.go"},
        {project.root / "store/a_test.go", project.root / "internal/store/a_test.go"}};
    CHECK(FixedText(whole, main) == "package main\nimport \"example.com/m/internal/store\"\n");

    const std::vector<ned::editor::importfix::MovedFile> oneFile{
        {project.root / "store/a.go", project.root / "internal/store/a.go"}};
    CHECK(FixedText(oneFile, main) == Read(main));

    // Heard about after the fact: the directory is already gone.
    fs::create_directories(project.root / "internal");
    fs::rename(project.root / "store", project.root / "internal/store");
    CHECK(FixedText(whole, main) == "package main\nimport \"example.com/m/internal/store\"\n");
}

TEST_CASE("Odin and V imports name package directories too", "[ImportLanguages]") {
    const ScratchProject project("odin_v_packages");
    Write(project.root / "shared/util/strings.odin", "package util\n");
    Write(project.root / "game/main.odin", "package main\nimport \"../shared/util\"\n");
    CHECK(ResolveAt(project.root / "game/main.odin", "util") == project.root / "shared/util/strings.odin");

    const std::vector<ned::editor::importfix::MovedFile> movedOdin{
        {project.root / "shared/util/strings.odin", project.root / "lib/util/strings.odin"}};
    CHECK(FixedText(movedOdin, project.root / "game/main.odin") == "package main\nimport \"../lib/util\"\n");

    Write(project.root / "v.mod", "Module {\n\tname: 'app'\n}\n");
    Write(project.root / "modules/net/http/client.v", "module http\n");
    Write(project.root / "cmd/main.v", "module main\nimport net.http\nfn main() {}\n");
    CHECK(ResolveAt(project.root / "cmd/main.v", "http") == project.root / "modules/net/http/client.v");

    const std::vector<ned::editor::importfix::MovedFile> movedV{
        {project.root / "modules/net/http/client.v", project.root / "modules/web/http/client.v"}};
    CHECK(FixedText(movedV, project.root / "cmd/main.v") == "module main\nimport web.http\nfn main() {}\n");
}

TEST_CASE("A ~/ include is counted from $HOME and keeps its ~/ when it moves", "[ImportLanguages]") {
    const ScratchProject project("home_prefix");
    const EnvVarGuard    home("HOME", (project.root / "home").string());
    Write(project.root / "home/.gitconfig.d/work.gitconfig", "[user]\n\tname = x\n");
    Write(project.root / "home/.ssh/work", "Host w\n");
    Write(project.root / "home/.gitconfig", "[include]\n\tpath = ~/.gitconfig.d/work.gitconfig\n");
    Write(project.root / "etc/ssh/ssh_config", "Include ~/.ssh/work\n");
    CHECK(ResolveAt(project.root / "home/.gitconfig", "work") == project.root / "home/.gitconfig.d/work.gitconfig");
    CHECK(ResolveAt(project.root / "etc/ssh/ssh_config", "work") == project.root / "home/.ssh/work");

    const std::vector<ned::editor::importfix::MovedFile> moved{
        {project.root / "home/.gitconfig.d/work.gitconfig", project.root / "home/conf/work.gitconfig"}};
    CHECK(FixedText(moved, project.root / "home/.gitconfig") == "[include]\n\tpath = ~/conf/work.gitconfig\n");
}

TEST_CASE("An Odin collection import resolves through ODIN_ROOT and ols.json", "[ImportLanguages]") {
    const ScratchProject project("odin_collections");
    const EnvVarGuard    odinRoot("ODIN_ROOT", (project.root / "odin").string());
    Write(project.root / "odin/core/fmt/fmt.odin", "package fmt\n");
    Write(project.root / "app/libs/ui/ui.odin", "package ui\n");
    Write(project.root / "app/ols.json", "{\"collections\": [{\"name\": \"libs\", \"path\": \"libs\"}]}");
    Write(project.root / "app/main.odin", "package main\nimport \"core:fmt\"\nimport \"libs:ui\"\nimport \"vendor:x\"\n");
    const fs::path main = project.root / "app/main.odin";
    CHECK(ResolveAt(main, "fmt") == project.root / "odin/core/fmt/fmt.odin");
    CHECK(ResolveAt(main, "ui") == project.root / "app/libs/ui/ui.odin");
    CHECK_FALSE(ResolveAt(main, "vendor").has_value());

    const std::vector<ned::editor::importfix::MovedFile> moved{
        {project.root / "app/libs/ui/ui.odin", project.root / "app/libs/widgets/ui.odin"}};
    CHECK(FixedText(moved, main) == "package main\nimport \"core:fmt\"\nimport \"libs:widgets\"\nimport \"vendor:x\"\n");
}

TEST_CASE("V searches the toolchain's vlib and the user's modules", "[ImportLanguages]") {
    const ScratchProject project("v_roots");
    Write(project.root / "v/vlib/os/os.v", "module os\n");
    Write(project.root / "v/v", "");
    Write(project.root / "home/.vmodules/pkg/pkg.v", "module pkg\n");
    const std::string home = (project.root / "home").string();
    CHECK(ned::editor::VSearchRoots(project.root / "v/v", nullptr, home.c_str()) ==
          std::vector<fs::path>{project.root / "v/vlib", project.root / "home/.vmodules"});
    const std::string custom = (project.root / "v").string();
    CHECK(ned::editor::VSearchRoots(std::nullopt, custom.c_str(), home.c_str()) == std::vector<fs::path>{project.root / "v"});
    CHECK(ned::editor::VSearchRoots(std::nullopt, nullptr, nullptr).empty());
}

TEST_CASE("A component's script imports resolve and follow moves", "[ImportLanguages]") {
    const ScratchProject project("component_imports");
    Write(project.root / "package.json", "{}\n");
    Write(project.root / "src/lib/util.ts", "export const x = 1;\n");
    Write(project.root / "src/routes/Button.svelte", "<button />\n");
    Write(project.root / "src/routes/Page.svelte",
          "<script>\n  import Button from './Button.svelte';\n  import { x } from '$lib/util';\n</script>\n<Button />\n");
    const fs::path page = project.root / "src/routes/Page.svelte";
    CHECK(ResolveAt(page, "Button.svelte") == project.root / "src/routes/Button.svelte");
    CHECK(ResolveAt(page, "util") == project.root / "src/lib/util.ts");

    const std::vector<ned::editor::importfix::MovedFile> moved{
        {project.root / "src/routes/Button.svelte", project.root / "src/components/Button.svelte"}};
    CHECK(FixedText(moved, page) == "<script>\n  import Button from '../components/Button.svelte';\n  import { x } from "
                                    "'$lib/util';\n</script>\n<Button />\n");

    Write(project.root / "src/App.vue", "<script setup>\nimport { x } from '@/lib/util'\n</script>\n");
    CHECK(ResolveAt(project.root / "src/App.vue", "util") == project.root / "src/lib/util.ts");
}

TEST_CASE("A JVM class moving package rewrites its package and the imports it and its old package need",
          "[ImportLanguages]") {
    const ScratchProject project("jvm_package_move");
    const fs::path       util = project.root / "src/main/java/com/acme/util";
    Write(util / "Strings.java", "package com.acme.util;\n\npublic class Strings {\n    Helper h;\n}\n");
    Write(util / "Helper.java", "package com.acme.util;\n\nclass Helper {\n    Strings s;\n}\n");
    Write(util / "Unrelated.java", "package com.acme.util;\n\nclass Unrelated {}\n");
    Write(project.root / "src/main/java/com/acme/app/Main.java",
          "package com.acme.app;\n\nimport com.acme.util.Strings;\n\nclass Main {}\n");
    const std::vector<ned::editor::importfix::MovedFile> moved{
        {util / "Strings.java", project.root / "src/main/java/com/acme/text/Strings.java"}};

    CHECK(FixedText(moved, util / "Strings.java", project.root / "src/main/java/com/acme/text/Strings.java") ==
          "package com.acme.text;\n\nimport com.acme.util.Helper;\n\npublic class Strings {\n    Helper h;\n}\n");
    CHECK(FixedText(moved, util / "Helper.java") ==
          "package com.acme.util;\n\nimport com.acme.text.Strings;\n\nclass Helper {\n    Strings s;\n}\n");
    CHECK(FixedText(moved, util / "Unrelated.java") == Read(util / "Unrelated.java"));
    CHECK(FixedText(moved, project.root / "src/main/java/com/acme/app/Main.java") ==
          "package com.acme.app;\n\nimport com.acme.text.Strings;\n\nclass Main {}\n");

    // Kotlin doesn't tie a package to its directory; one that doesn't match is left alone.
    Write(project.root / "src/main/kotlin/x/Tool.kt", "package com.acme.tools\n\nclass Tool\n");
    const std::vector<ned::editor::importfix::MovedFile> kotlin{
        {project.root / "src/main/kotlin/x/Tool.kt", project.root / "src/main/kotlin/y/Tool.kt"}};
    CHECK(FixedText(kotlin, project.root / "src/main/kotlin/x/Tool.kt") == "package com.acme.tools\n\nclass Tool\n");
}
