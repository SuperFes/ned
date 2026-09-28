#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>

#include <sys/resource.h>
#include <sys/wait.h>
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
const fs::path kRunawayGrammar = fs::path(NED_REPO_ROOT) / "Tests" / "LanguagePackage" / "runaway" / "grammar.janet";
const fs::path kRunawayScanner = NED_RUNAWAY_SCANNER_LIBRARY;
int            g_packageSeed = 0;

// A fresh package directory per case: loaded packages are cached by path
// for the process, so a directory is never reused with different contents.
fs::path NewPackageDir(const std::string& name, const fs::path& grammar = kDemoGrammar) {
    const fs::path dir = fs::temp_directory_path() / ("ned-language-package-" + std::to_string(::getpid()) + "-" + std::to_string(g_packageSeed++)) / name;
    fs::remove_all(dir.parent_path());
    fs::create_directories(dir);
    fs::copy_file(grammar, dir / "grammar.janet");
    return dir;
}

std::string Sexp(const Language& language, std::string_view text) {
    const ned::editor::grammar::Parser parser(language);
    const ned::editor::grammar::Tree   tree = parser.Parse(text);
    return ned::editor::parse::SubtreeToSexp(tree.Green().Root(), tree.Green().Language());
}

// Runs `body` in a forked child with `extraBytes` more address space than it
// starts with and `seconds` of wall time; true when it returned true.
bool RunsBounded(const std::function<bool()>& body, std::size_t extraBytes, unsigned seconds) {
    const pid_t child = ::fork();
    if (child == 0) {
        long pages = 0;
        std::ifstream("/proc/self/statm") >> pages;
        const rlim_t limit = static_cast<rlim_t>(pages) * static_cast<rlim_t>(::sysconf(_SC_PAGESIZE)) + extraBytes;
        const rlimit cap{.rlim_cur = limit, .rlim_max = limit};
        ::setrlimit(RLIMIT_AS, &cap);
        ::alarm(seconds);
        bool ok = false;
        try {
            ok = body();
        }
        catch (...) {
        }
        std::_Exit(ok ? 0 : 1);
    }
    int status = 0;
    ::waitpid(child, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
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

TEST_CASE("An empty external token a repetition absorbs is accepted once per configuration, not forever",
          "[LanguagePackage][ParseEngine]") {
    const fs::path dir      = NewPackageDir("runaway", kRunawayGrammar);
    const Language language = LoadLanguagePackage(dir, PackageScanner{.name = "runaway", .library = kRunawayScanner});

    // A regression here parses forever and grows without bound, so the parse
    // runs in a child capped in address space and time.
    const auto parsesBounded = [&] {
        const std::string sexp = Sexp(language, "ab \\ cd\n");
        std::size_t       gaps = 0;
        for (std::size_t at = sexp.find("(gap)"); at != std::string::npos; at = sexp.find("(gap)", at + 1))
            ++gaps;
        std::fprintf(stderr, "%s\n", sexp.c_str());
        return gaps <= 2 && sexp.find("(word)") != std::string::npos;
    };
    CHECK(RunsBounded(parsesBounded, std::size_t{512} << 20, 20));
}
