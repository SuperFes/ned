#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <unistd.h>

#include "Editor/Grammar/Compile/CompileCommand.h"
#include "Editor/Grammar/LanguagePackage.h"
#include "Editor/Grammar/Languages.h"
#include "Editor/Grammar/Parser.h"
#include "Editor/Grammar/Tree.h"
#include "Editor/LanguageRegistry.h"
#include "Editor/ModeOverrides.h"
#include "Editor/Parse/Sexp.h"

using ned::editor::grammar::Language;
using ned::editor::grammar::LoadLanguagePackage;
using ned::editor::grammar::PackageScanner;

namespace {

namespace fs = std::filesystem;

const fs::path kDemoGrammar  = fs::path(NED_REPO_ROOT) / "Tests" / "LanguagePackage" / "demo" / "grammar.janet";
const fs::path kDemoScanner  = NED_DEMO_SCANNER_LIBRARY;
int            g_packageSeed = 0;

// A fresh package directory per case: loaded packages are cached by path
// for the process, so a directory is never reused with different contents.
fs::path NewPackageDir(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("ned-language-package-" + std::to_string(::getpid()) + "-" + std::to_string(g_packageSeed++)) / name;
    fs::remove_all(dir.parent_path());
    fs::create_directories(dir);
    fs::copy_file(kDemoGrammar, dir / "grammar.janet");
    return dir;
}

std::string Sexp(const Language& language, std::string_view text) {
    const ned::editor::grammar::Parser parser(language);
    const ned::editor::grammar::Tree   tree = parser.Parse(text);
    return ned::editor::parse::SubtreeToSexp(tree.Green().Root(), tree.Green().Language());
}

struct RegistryGuard {
    ~RegistryGuard() {
        ned::editor::ClearRegisteredLanguages();
    }
};

} // namespace

TEST_CASE("A package with grammar.janet and a scanner library loads and parses", "[LanguagePackage]") {
    const fs::path dir      = NewPackageDir("demo");
    const Language language = LoadLanguagePackage(dir, PackageScanner{.name = "demo", .library = kDemoScanner});
    CHECK(Sexp(language, "foo\nbar  \n") == "(source_file (item (word) (line_end)) (item (word) (line_end)))");
    // Loaded once for the process: the same directory hands back the same tables.
    CHECK(LoadLanguagePackage(dir, PackageScanner{.name = "demo", .library = kDemoScanner}).Raw() == language.Raw());
}

TEST_CASE("A package's compiled tables file is preferred over compiling its grammar", "[LanguagePackage]") {
    const fs::path     dir = NewPackageDir("demo");
    std::ostringstream out, err;
    REQUIRE(ned::editor::grammar::compile::RunCompileLanguage({dir.string()}, "", out, err) == 0);
    REQUIRE(fs::exists(dir / "tables"));
    fs::remove(dir / "grammar.janet"); // only the tables remain
    const Language language = LoadLanguagePackage(dir, PackageScanner{.name = "demo", .library = kDemoScanner});
    CHECK(Sexp(language, "a\n") == "(source_file (item (word) (line_end)))");
}

TEST_CASE("A package that cannot be loaded says why", "[LanguagePackage]") {
    const fs::path dir = NewPackageDir("demo");
    // External tokens with no scanner anywhere.
    REQUIRE_THROWS_AS(LoadLanguagePackage(dir, PackageScanner{.name = "demo", .library = {}}), std::runtime_error);
    // A scanner library that does not exist, and one without the export.
    REQUIRE_THROWS_AS(LoadLanguagePackage(NewPackageDir("demo"), PackageScanner{.name = "demo", .library = "/not/a/real/libned-scanner.so"}), std::runtime_error);
    REQUIRE_THROWS_AS(LoadLanguagePackage(NewPackageDir("demo"), PackageScanner{.name = "other", .library = kDemoScanner}), std::runtime_error);
    // Nothing to load at all.
    const fs::path empty = NewPackageDir("demo");
    fs::remove(empty / "grammar.janet");
    REQUIRE_THROWS_AS(LoadLanguagePackage(empty, PackageScanner{.name = "demo", .library = {}}), std::runtime_error);
    // A corrupt tables file.
    const fs::path corrupt = NewPackageDir("demo");
    std::ofstream(corrupt / "tables", std::ios::binary) << "NEDTABLE garbage";
    REQUIRE_THROWS_AS(LoadLanguagePackage(corrupt, PackageScanner{.name = "demo", .library = kDemoScanner}), std::runtime_error);
}

TEST_CASE("A registered language directory with :scanner-library becomes a mode", "[LanguagePackage]") {
    const RegistryGuard guard;
    const fs::path      dir = NewPackageDir("demo");
    std::ofstream(dir / "language.janet") << "{:name \"demo\" :extensions [\".demo\"] :scanner-library \"" << kDemoScanner.string() << "\"}\n";
    ned::editor::LoadLanguageDirectory(dir);

    const std::optional<ned::editor::Mode> mode = ned::editor::ModeByName("demo-mode");
    REQUIRE(mode.has_value());
    REQUIRE_FALSE(static_cast<bool>(mode->highlight)); // no query files
    CHECK(ned::editor::ModeForPath("/some/where/notes.demo").name == "demo-mode");
}

TEST_CASE("A bundled language is a package too", "[LanguagePackage]") {
    const std::optional<Language> json = ned::editor::grammar::LanguageByName("json");
    REQUIRE(json.has_value());
    CHECK(Sexp(*json, "[1]") == "(document (array (number)))");
    CHECK_FALSE(ned::editor::grammar::LanguageByName("no-such-language").has_value());
}
