#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <unistd.h>

#include "Editor/Grammar/Compile/CompileCommand.h"
#include "Editor/Grammar/Compile/Compiler.h"
#include "Editor/Grammar/Compile/GrammarFile.h"
#include "Editor/Grammar/Compile/TableFile.h"
#include "Editor/LanguageFiles.h"
#include "Editor/Parse/Parser.h"
#include "Editor/Parse/Sexp.h"

using ned::editor::grammar::compile::CompiledLanguage;
using ned::editor::grammar::compile::CompileError;
using ned::editor::grammar::compile::CompileGrammar;
using ned::editor::grammar::compile::LoadLanguage;
using ned::editor::grammar::compile::ParseGrammarJanet;
using ned::editor::grammar::compile::RunCompileLanguage;
using ned::editor::grammar::compile::SerializeLanguage;

namespace {

namespace fs = std::filesystem;

std::unique_ptr<CompiledLanguage> CompileBundled(const std::string& language) {
    return CompileGrammar(ParseGrammarJanet(ned::editor::ReadLanguageFile(language + "/grammar.janet")));
}

std::string ParseWith(const CompiledLanguage& language, std::string_view text) {
    ned::editor::parse::Engine          engine(language.Data());
    const ned::editor::parse::GreenTree tree = engine.Parse(text);
    return ned::editor::parse::SubtreeToSexp(tree.Root(), tree.Language());
}

std::string ReadWhole(const fs::path& path) {
    std::ifstream      in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

struct TempDir {
    fs::path path;
    TempDir() : path(fs::temp_directory_path() / ("ned-language-tables-" + std::to_string(::getpid()))) {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        fs::remove_all(path);
    }
};

} // namespace

TEST_CASE("Compiled tables survive the file round trip", "[LanguageTables]") {
    const std::unique_ptr<CompiledLanguage> compiled = CompileBundled("json");
    const std::string                       bytes    = SerializeLanguage(*compiled);
    REQUIRE(bytes.starts_with("NEDTABLE"));

    const std::unique_ptr<CompiledLanguage> loaded = LoadLanguage(bytes);
    CHECK(SerializeLanguage(*loaded) == bytes);
    CHECK(loaded->name == "json");
    CHECK(loaded->Data()->symbolCount == compiled->Data()->symbolCount);
    CHECK(loaded->Data()->stateCount == compiled->Data()->stateCount);

    const std::string text = "{\"a\": [1, 2.5, true], \"b\": null}";
    CHECK(ParseWith(*loaded, text) == ParseWith(*compiled, text));
    CHECK(ParseWith(*loaded, "{\"broken\": [1,") == ParseWith(*compiled, "{\"broken\": [1,"));
}

TEST_CASE("A keyword language's tables round-trip with its reserved words and keyword lexer", "[LanguageTables]") {
    const std::unique_ptr<CompiledLanguage> compiled = CompileBundled("go");
    const std::string                       bytes    = SerializeLanguage(*compiled);
    const std::unique_ptr<CompiledLanguage> loaded   = LoadLanguage(bytes);
    CHECK(SerializeLanguage(*loaded) == bytes);
    CHECK(loaded->Data()->keywordCaptureToken == compiled->Data()->keywordCaptureToken);
    const std::string text = "package main\n\nfunc main() {\n\tfor i := range xs {\n\t\tbreak\n\t}\n}\n";
    CHECK(ParseWith(*loaded, text) == ParseWith(*compiled, text));
}

TEST_CASE("A table file that is not one is refused, and so is a truncated one", "[LanguageTables]") {
    CHECK_THROWS_AS(LoadLanguage("not a table file at all"), CompileError);
    CHECK_THROWS_AS(LoadLanguage(""), CompileError);
    const std::string bytes = SerializeLanguage(*CompileBundled("json"));
    CHECK_THROWS_AS(LoadLanguage(std::string_view(bytes).substr(0, bytes.size() / 2)), CompileError);
    CHECK_THROWS_AS(LoadLanguage(bytes + "x"), CompileError);
    std::string wrongVersion = bytes;
    wrongVersion[8]          = static_cast<char>(0x7f);
    CHECK_THROWS_AS(LoadLanguage(wrongVersion), CompileError);
}

TEST_CASE("The compile-language command writes a language's tables and reports it", "[LanguageTables]") {
    const TempDir      temp;
    const fs::path     output = temp.path / "json.tables";
    std::ostringstream out, err;
    const int          code = RunCompileLanguage({(ned::editor::BundledLanguagesRoot() / "json").string()}, output.string(), out, err);
    INFO(err.str());
    REQUIRE(code == 0);
    CHECK(err.str().empty());
    CHECK(out.str().starts_with("json: "));
    CHECK(out.str().find(" parse states, ") != std::string::npos);
    REQUIRE(fs::exists(output));
    CHECK(LoadLanguage(ReadWhole(output))->name == "json");
}

TEST_CASE("The compile-language command defaults to `tables` beside the grammar and names a grammar's mistakes", "[LanguageTables]") {
    const TempDir  temp;
    const fs::path languageDir = temp.path / "demo";
    fs::create_directories(languageDir);
    {
        std::ofstream grammar(languageDir / "grammar.janet");
        grammar << "{:name \"demo\" :rules {source (:seq \"a\" missing)}}\n";
    }
    std::ostringstream out, err;
    CHECK(RunCompileLanguage({languageDir.string()}, "", out, err) == 1);
    CHECK(err.str().find("Undefined symbol `missing`") != std::string::npos);
    CHECK_FALSE(fs::exists(languageDir / "tables"));

    {
        std::ofstream grammar(languageDir / "grammar.janet");
        grammar << "{:name \"demo\" :rules {source (:seq \"a\" (:repeat \"b\"))}}\n";
    }
    std::ostringstream out2, err2;
    REQUIRE(RunCompileLanguage({languageDir.string()}, "", out2, err2) == 0);
    CHECK(fs::exists(languageDir / "tables"));

    std::ostringstream out3, err3;
    CHECK(RunCompileLanguage({(temp.path / "absent").string()}, "", out3, err3) == 1);
    CHECK(err3.str().find("no such file") != std::string::npos);
    CHECK(RunCompileLanguage({}, "", out3, err3) == 2);
    CHECK(RunCompileLanguage({languageDir.string(), languageDir.string()}, "x", out3, err3) == 2);
}
