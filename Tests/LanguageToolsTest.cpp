#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <unistd.h>

#include "Editor/Grammar/Compile/ImportCommand.h"
#include "Editor/Grammar/Compile/TestCommand.h"
#include "Editor/Grammar/Corpus.h"

using ned::editor::grammar::compile::ImportOptions;
using ned::editor::grammar::compile::RunImportLanguage;
using ned::editor::grammar::compile::RunTestLanguage;

namespace {

namespace fs = std::filesystem;

const fs::path kDemoGrammar   = fs::path(NED_REPO_ROOT) / "Tests" / "LanguagePackage" / "demo" / "grammar.janet";
const fs::path kDemoScanner   = NED_DEMO_SCANNER_LIBRARY;
const fs::path kImportFixture = fs::path(NED_REPO_ROOT) / "Tests" / "LanguagePackage" / "import-fixture";
int            g_seed         = 0;

fs::path NewDir() {
    const fs::path dir = fs::temp_directory_path() / ("ned-language-tools-" + std::to_string(::getpid()) + "-" + std::to_string(g_seed++));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::string ReadWhole(const fs::path& path) {
    std::ifstream      in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// A demo package with a corpus: one case right, one with a stale tree.
fs::path DemoPackageWithCorpus() {
    const fs::path dir = NewDir() / "demo";
    fs::create_directories(dir / "corpus");
    fs::copy_file(kDemoGrammar, dir / "grammar.janet");
    std::ofstream(dir / "language.janet") << "{:name \"demo\" :scanner-library \"" << kDemoScanner.string() << "\"}\n";
    // The input is everything between the header and the divider minus one
    // trailing newline (the reference's rule): no blank line after the
    // header, since this grammar has no extra to absorb it, and one before
    // the divider, so the last line still ends.
    std::ofstream(dir / "corpus" / "lines.txt") << "==================\nOne line\n==================\nfoo\n\n---\n\n(source_file\n  (item\n    (word)\n    (line_end)))\n\n"
                                                << "==================\nStale\n==================\nfoo\nbar\n\n---\n\n(source_file\n  (item\n    (word)))\n";
    return dir;
}

// The reference's `:cst` listing for the demo grammar over "foo\n": the
// root, one item, a named leaf with its text, and the scanner's line_end
// token whose text spans a line break, so it is listed line by line with
// each line's own range.
const char* const kDemoCst = "0:0 - 1:0   source_file\n"
                             "0:0 - 1:0     item\n"
                             "0:0 - 0:3       word `foo`\n"
                             "0:3 - 1:0       line_end\n"
                             "0:3 - 0:4         `\\n`";

} // namespace

TEST_CASE("The corpus reader keeps the byte span of each expected tree", "[LanguageTools]") {
    const std::string content = "====\nA\n====\nx\n---\n(a)\n\n====\nB\n====\ny\n---\n\n(b)\n";
    const auto        cases   = ned::editor::grammar::corpus::ParseCorpusFile(content, "f.txt");
    REQUIRE(cases.size() == 2);
    CHECK(content.substr(cases[0].expectedStart, cases[0].expectedEnd - cases[0].expectedStart) == "(a)\n\n");
    CHECK(content.substr(cases[1].expectedStart, cases[1].expectedEnd - cases[1].expectedStart) == "\n(b)\n");
    CHECK(ned::editor::grammar::corpus::PrettySexp("(source_file (item name: (word) (line_end)))") == "(source_file\n  (item\n    name: (word)\n    (line_end)))");
}

TEST_CASE("A :cst case compares the reference's concrete-syntax listing, and --bless writes it back in that form", "[LanguageTools]") {
    const std::string content = "====\nListing\n:cst\n====\nfoo\n\n---\n\n" + std::string(kDemoCst) + "\n\n====\nStale listing\n:cst\n====\nfoo\n\n---\n0:0 - 1:0   source_file\n";
    const auto        cases   = ned::editor::grammar::corpus::ParseCorpusFile(content, "f.txt");
    REQUIRE(cases.size() == 2);
    CHECK(cases[0].cst);
    CHECK_FALSE(cases[0].hasFields);
    CHECK(cases[0].expected == kDemoCst);

    const fs::path package = DemoPackageWithCorpus();
    std::ofstream(package / "corpus" / "cst.txt") << content;
    std::ostringstream out, err;
    CHECK(RunTestLanguage({package.string()}, false, out, err) == 1);
    CHECK(out.str().find("FAIL     cst.txt: Stale listing") != std::string::npos);
    CHECK(out.str().find("FAIL     cst.txt: Listing") == std::string::npos);
    CHECK(out.str().find("demo: 4 cases, 2 passed, 2 failed") != std::string::npos);

    std::ostringstream out2, err2;
    CHECK(RunTestLanguage({package.string()}, true, out2, err2) == 0);
    const std::string blessed = ReadWhole(package / "corpus" / "cst.txt");
    CHECK(blessed.find(std::string("Stale listing\n:cst\n====\nfoo\n\n---\n\n") + kDemoCst + "\n") != std::string::npos);
}

TEST_CASE("The test-language command reports a failing case, and --bless rewrites it", "[LanguageTools]") {
    const fs::path     package = DemoPackageWithCorpus();
    std::ostringstream out, err;
    CHECK(RunTestLanguage({package.string()}, false, out, err) == 1);
    CHECK(out.str().find("FAIL     lines.txt: Stale") != std::string::npos);
    CHECK(out.str().find("demo: 2 cases, 1 passed, 1 failed, 0 skipped") != std::string::npos);
    CHECK(err.str().empty());

    std::ostringstream out2, err2;
    CHECK(RunTestLanguage({package.string()}, true, out2, err2) == 0);
    CHECK(out2.str().find("blessed  lines.txt: Stale") != std::string::npos);
    const std::string blessed = ReadWhole(package / "corpus" / "lines.txt");
    CHECK(blessed.find("(source_file\n  (item\n    (word)\n    (line_end))\n  (item\n    (word)\n    (line_end)))") != std::string::npos);
    CHECK(blessed.find("One line") != std::string::npos); // the passing case is untouched

    std::ostringstream out3, err3;
    CHECK(RunTestLanguage({package.string()}, false, out3, err3) == 0);
    CHECK(out3.str().find("2 passed, 0 failed") != std::string::npos);

    std::ostringstream out4, err4;
    CHECK(RunTestLanguage({(package.parent_path() / "absent").string()}, false, out4, err4) == 2);
    CHECK(RunTestLanguage({}, false, out4, err4) == 2);
}

TEST_CASE("The import-language command turns a grammar repository into a package that compiles and passes its corpus", "[LanguageTools]") {
    const fs::path     into = NewDir();
    std::ostringstream out, err;
    setenv("NED_PORT_SCANNER", (fs::path(NED_REPO_ROOT) / "Tools" / "port-scanner.py").c_str(), 1);
    const int code = RunImportLanguage(ImportOptions{.source = kImportFixture.string(), .name = "", .subdir = "", .ref = "", .into = into.string()}, out, err);
    INFO(out.str() << err.str());
    CHECK(code == 0);
    CHECK(err.str().empty());

    const fs::path package = into / "words";
    REQUIRE(fs::exists(package / "grammar.janet"));
    REQUIRE(fs::exists(package / "tables"));
    CHECK(fs::exists(package / "upstream" / "highlights.janet"));
    CHECK(fs::exists(package / "corpus" / "basic.txt"));
    CHECK(fs::exists(package / "scanner" / "scanner.c"));
    CHECK(fs::exists(package / "scanner" / "WordsScanner.cpp"));
    const std::string definition = ReadWhole(package / "language.janet");
    CHECK(definition.find("{:name \"words\"") != std::string::npos);
    CHECK(definition.find(":extensions [\".words\" \".wd\"]") != std::string::npos);
    CHECK(definition.find(":filenames [\".wordsrc\" \"words.lock\"]") != std::string::npos);
    CHECK(definition.find("generated ABI 14, scanner 7 lines, corpus 1 files") != std::string::npos);
    CHECK(definition.find("# :scanner-library") != std::string::npos);
    CHECK(out.str().find("words: 2 cases, 2 passed, 0 failed") != std::string::npos);

    // Importing over an existing package is refused.
    std::ostringstream out2, err2;
    CHECK(RunImportLanguage(ImportOptions{.source = kImportFixture.string(), .into = into.string()}, out2, err2) == 2);
    CHECK(err2.str().find("already exists") != std::string::npos);
    // A directory that is not a grammar repository.
    std::ostringstream out3, err3;
    CHECK(RunImportLanguage(ImportOptions{.source = into.string(), .into = NewDir().string()}, out3, err3) == 2);
    CHECK(err3.str().find("grammar.json") != std::string::npos);
}
