#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "Editor/BundledLanguages.h"
#include "Editor/Grammar/Languages.h"
#include "Editor/Grammar/Node.h"
#include "Editor/Grammar/Parser.h"
#include "Editor/Grammar/Tree.h"
#include "Editor/Indent.h"
#include "Editor/LanguageDefinition.h"
#include "Editor/Mode.h"
#include "Text/Buffer.h"

// The two properties every formatting rule has to hold, applied to the one
// Ned already ships.
//
// `Docs/FormattingCapabilities.md` calls these "the single highest-value item
// in Tier C" and says they "should exist from the first rule, not be
// retrofitted". Ned is past the first rule: structural indentation (rule kind
// 1) is live across 21 languages via Editor/Indent.h. So the harness exists
// now, proven against something real, and every later rule inherits it rather
// than needing it invented under pressure.
//
//   Idempotence   format(format(x)) == format(x)
//                 A formatter that does not converge is worse than none --
//                 every save churns the file and every diff is noise.
//
//   Safety        parse(format(x)) has the same node-kind sequence as parse(x)
//                 Formatting may move bytes; it may never change what the code
//                 MEANS. Comparing the named-node kind sequence catches a
//                 reformat that silently reparses into different structure,
//                 which is the failure that matters and the one a byte diff
//                 cannot see.
//
//   Verbatim      the text of every multi-line string is byte-identical
//                 safety
//                 The one thing formatting may not move at all. A docstring, a
//                 C++ raw string literal, a PHP heredoc: the interior IS the
//                 value, and reindenting it edits what the program says. Its
//                 own property rather than a case of Safety above, because the
//                 tree is identical either way -- a reindented docstring is
//                 still one `string` node -- which is exactly how indent-buffer
//                 rewrote all three for as long as it existed without either
//                 property noticing.
//
// Deliberately run over the oracle corpus rather than bespoke snippets: those
// are real files in 14 languages that other tests already depend on, so a rule
// that breaks one of these properties breaks it on code someone recognises.

namespace {

namespace fs = std::filesystem;

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

// Every named node's kind, in tree order. Anonymous nodes (punctuation) are
// skipped on purpose: a formatter is allowed to add or remove a brace's
// surrounding whitespace, and in some grammars that shifts which anonymous
// tokens appear, without any change to what the code means.
void CollectKinds(const ned::editor::grammar::Node& node, int depth, std::vector<std::string>& out) {
    if (node.IsNull()) return;
    // DEPTH is part of the identity, not decoration. A flat sequence of kinds
    // is preserved under re-nesting -- moving a Python statement out of an
    // if-block yields the same kinds in the same order, and that is exactly
    // the change that alters what the program does. Found by this file's own
    // guard case failing, which is what the guard case is for.
    if (node.IsNamed()) out.emplace_back(std::to_string(depth) + ":" + std::string(node.Type()));
    for (std::size_t i = 0; i < node.ChildCount(); ++i) CollectKinds(node.Child(i), depth + 1, out);
}

std::vector<std::string> NodeKinds(const ned::editor::grammar::Language& language, const std::string& text) {
    const ned::editor::grammar::Parser parser(language);
    const ned::editor::grammar::Tree   tree = parser.Parse(text);
    std::vector<std::string>              kinds;
    CollectKinds(tree.RootNode(), 0, kinds);
    return kinds;
}

std::string IndentAll(const std::string& text, const ned::editor::Mode& mode) {
    ned::text::Buffer buffer("property-test");
    buffer.InsertAtPoint(text);
    ned::editor::IndentBuffer(buffer, mode);
    return buffer.Text();
}

struct Case {
    std::string       file;
    std::string       language;
    ned::editor::Mode mode;
};

std::vector<Case> Corpus() {
    std::vector<Case> cases;
    cases.push_back({"sample.c", "c", ned::editor::CMode()});
    cases.push_back({"sample.cpp", "cpp", ned::editor::CppMode()});
    cases.push_back({"sample.py", "python", ned::editor::PythonMode()});
    cases.push_back({"sample.json", "json", ned::editor::JsonMode()});
    cases.push_back({"sample.go", "go", ned::editor::GoMode()});
    cases.push_back({"sample.rs", "rust", ned::editor::RustMode()});
    cases.push_back({"sample.java", "java", ned::editor::JavaMode()});
    cases.push_back({"sample.cs", "csharp", ned::editor::CSharpMode()});
    cases.push_back({"sample.js", "javascript", ned::editor::JavaScriptMode()});
    cases.push_back({"sample.ts", "typescript", ned::editor::TypeScriptMode()});
    cases.push_back({"sample.kt", "kotlin", ned::editor::KotlinMode()});
    cases.push_back({"sample.clj", "clojure", ned::editor::ClojureMode()});
    cases.push_back({"sample.php", "php", ned::editor::PhpMode()});
    cases.push_back({"sample.css", "css", ned::editor::CssMode()});
    return cases;
}

ned::editor::Mode BundledMode(std::string_view language) {
    const ned::editor::LanguageDefinition* definition = ned::editor::BundledLanguage(language);
    REQUIRE(definition != nullptr);
    return ned::editor::ModeFromDefinition(*definition);
}

// Languages whose indentation is part of the syntax. Kept out of the oracle
// corpus (whose every file carries a golden snapshot) and in their own
// directory, since what matters here is only what a reindent does to them.
std::vector<Case> OffsideCorpus() {
    std::vector<Case> cases;
    for (const auto& [file, language] : std::vector<std::pair<std::string, std::string>>{
             {"sample.hs", "haskell"},
             {"sample.elm", "elm"},
             {"sample.fs", "fsharp"},
             {"sample.purs", "purescript"},
             {"sample.gd", "gdscript"},
             {"sample.nim", "nim"},
             {"sample.ml", "ocaml"},
         })
        cases.push_back({file, language, BundledMode(language)});
    return cases;
}

std::string ReadOffside(const std::string& file) {
    return ReadFile(fs::path(NED_REPO_ROOT) / "Tests" / "Format" / "offside" / file);
}

void RequireSameStructure(const std::string& language, const std::string& original, const std::string& formatted) {
    const auto grammar = ned::editor::grammar::LanguageByName(language);
    REQUIRE(grammar.has_value());
    const auto before = NodeKinds(*grammar, original);
    const auto after  = NodeKinds(*grammar, formatted);
    INFO(before.size() << " named nodes before, " << after.size() << " after");
    REQUIRE(before.size() == after.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        INFO("node " << i << ": was \"" << before[i] << "\", now \"" << after[i] << "\"");
        REQUIRE(before[i] == after[i]);
    }
}

} // namespace

TEST_CASE("Indenting is idempotent across the corpus", "[FormatterProperties]") {
    for (const Case& testCase : Corpus()) {
        INFO("corpus file: " << testCase.file);
        const std::string original = ReadFile(fs::path(NED_REPO_ROOT) / "Tests" / "Oracle" / "corpus" / testCase.file);

        const std::string once  = IndentAll(original, testCase.mode);
        const std::string twice = IndentAll(once, testCase.mode);
        CHECK(once == twice);
    }
}

TEST_CASE("Indenting never changes the parse structure", "[FormatterProperties]") {
    for (const Case& testCase : Corpus()) {
        INFO("corpus file: " << testCase.file);
        const auto language = ned::editor::grammar::LanguageByName(testCase.language);
        REQUIRE(language.has_value());

        const std::string original  = ReadFile(fs::path(NED_REPO_ROOT) / "Tests" / "Oracle" / "corpus" / testCase.file);
        const std::string formatted = IndentAll(original, testCase.mode);

        const auto before = NodeKinds(*language, original);
        const auto after  = NodeKinds(*language, formatted);
        INFO(before.size() << " named nodes before, " << after.size() << " after");
        REQUIRE(before.size() == after.size());
        for (std::size_t i = 0; i < before.size(); ++i) {
            INFO("node " << i << ": was \"" << before[i] << "\", now \"" << after[i] << "\"");
            REQUIRE(before[i] == after[i]);
        }
    }
}

TEST_CASE("Indenting never edits inside a multi-line string", "[FormatterProperties]") {
    bool anyVerbatim = false;
    for (const Case& testCase : Corpus()) {
        INFO("corpus file: " << testCase.file);
        const std::string original  = ReadFile(fs::path(NED_REPO_ROOT) / "Tests" / "Oracle" / "corpus" / testCase.file);
        const std::string formatted = IndentAll(original, testCase.mode);

        // Compared as content, not as offsets: formatting is allowed to move a
        // string sideways, only not to rewrite what is inside it. Joining the
        // spans is what makes the two comparable after every byte around them
        // has shifted.
        const auto text = [&testCase](const std::string& source) {
            std::string joined;
            for (const auto& [start, end] : ned::editor::VerbatimRanges(testCase.mode, source)) {
                joined += source.substr(start, end - start);
                joined += '\x1e';
            }
            return joined;
        };
        CHECK(text(original) == text(formatted));
        anyVerbatim = anyVerbatim || !text(original).empty();
    }

    // Guard the guard. Every corpus string used to be a single-line one, so
    // this property held over all fourteen files while testing nothing at all
    // -- sample.py, sample.cpp and sample.php carry a ragged multi-line
    // docstring, raw string literal and heredoc respectively for exactly this
    // reason, and a corpus edit that removed them would otherwise pass.
    CHECK(anyVerbatim);
}

TEST_CASE("The safety property can actually fail", "[FormatterProperties]") {
    // Guards the guard. A structural check that passes for everything proves
    // nothing, so this shows the comparison noticing a real change in meaning:
    // moving a closing brace turns two functions into one nested inside the
    // other, which no byte-level diff would flag as dangerous.
    // Python, because there whitespace genuinely IS structure -- the clearest
    // case that a formatter can change meaning without changing a single
    // non-whitespace byte, which is exactly the failure this property guards.
    const auto python = ned::editor::grammar::LanguageByName("python");
    REQUIRE(python.has_value());

    const auto inside  = NodeKinds(*python, "def f():\n    if x:\n        a()\n        b()\n");
    const auto outside = NodeKinds(*python, "def f():\n    if x:\n        a()\n    b()\n");
    CHECK(inside != outside); // b() left the if-block: same bytes, different program

    // Two earlier attempts at this guard both quietly passed, and each taught
    // something. Moving a C closing brace so one function nested inside
    // another did not trip it -- tree-sitter-c's recovery produced the same
    // kinds. Then this Python case did not trip it either, until the
    // comparison started carrying DEPTH: without that, re-nesting is
    // invisible. A guard case that proves nothing is the exact failure this
    // test exists to prevent, so both are recorded rather than quietly fixed.

    // Whitespace that does not change meaning must not trip it, or the
    // property would reject every legitimate reformat.
    const auto c = ned::editor::grammar::LanguageByName("c");
    REQUIRE(c.has_value());
    const auto tight = NodeKinds(*c, "int f(void) {\nreturn 1;\n}\n");
    const auto loose = NodeKinds(*c, "int f(void) {\n        return 1;\n}\n");
    CHECK(tight == loose);
}

TEST_CASE("Offside samples parse cleanly", "[FormatterProperties]") {
    // A sample with a parse error would be skipped by the batch reindent
    // (Mode::unreliableIndentRanges), and every property below would then
    // hold without testing anything.
    for (const Case& testCase : OffsideCorpus()) {
        INFO("sample: " << testCase.file);
        REQUIRE(testCase.mode.unreliableIndentRanges);
        CHECK(testCase.mode.unreliableIndentRanges(ReadOffside(testCase.file)).empty());
    }
}

TEST_CASE("Indenting never changes the parse structure of an offside language", "[FormatterProperties]") {
    for (const Case& testCase : OffsideCorpus()) {
        INFO("sample: " << testCase.file);
        const std::string original  = ReadOffside(testCase.file);
        const std::string formatted = IndentAll(original, testCase.mode);
        RequireSameStructure(testCase.language, original, formatted);
        CHECK(IndentAll(formatted, testCase.mode) == formatted);
    }
}

TEST_CASE("An offside sample in its house style reindents to itself", "[FormatterProperties]") {
    // For a `:preserve-indent` language this holds because no line is touched;
    // for the others (GDScript, Nim, OCaml) because the indents query agrees
    // with the style guide the sample is written in.
    for (const Case& testCase : OffsideCorpus()) {
        INFO("sample: " << testCase.file);
        const std::string original = ReadOffside(testCase.file);
        CHECK(IndentAll(original, testCase.mode) == original);
    }
}

TEST_CASE("Reindent brings a re-widened GDScript or Nim file back to house style", "[FormatterProperties]") {
    // Widening every indent keeps the program the same (and parseable), so
    // this is what reindent recomputing -- not merely preserving -- looks like.
    // Flattening would not do: it changes the program.
    const auto widen = [](const std::string& text, const std::string& unit, int factor) {
        std::string out;
        bool        atLineStart = true;
        for (const char c : text) {
            if (atLineStart && std::string(1, c) == unit) {
                for (int i = 0; i < factor; ++i)
                    out += unit;
                continue;
            }
            atLineStart = c == '\n';
            out += c;
        }
        return out;
    };
    for (const auto& [file, language, unit] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {"sample.gd", "gdscript", "\t"},
             {"sample.nim", "nim", " "},
         }) {
        INFO("sample: " << file);
        const ned::editor::Mode mode     = BundledMode(language);
        const std::string       original = ReadOffside(file);
        const std::string       wide     = widen(original, unit, 3);
        REQUIRE(wide != original);
        RequireSameStructure(language, original, wide);
        CHECK(IndentAll(wide, mode) == original);
    }
}

TEST_CASE("A flattened sample reindents back to its house style", "[FormatterProperties]") {
    // Indentation is not syntax in these, so stripping it keeps the program
    // the same; reindent has to rebuild every level from structure alone.
    for (const auto& [file, language] : std::vector<std::pair<std::string, std::string>>{
             {"sample.pas", "pascal"},
             {"sample.tf", "hcl"},
             {"sample.cmake", "cmake"},
             {"sample.pl", "perl"},
             {"sample.R", "r"},
             {"sample.ml", "ocaml"},
             {"sample.mli", "ocaml-interface"},
             {"sample.ex", "elixir"},
             {"sample.erl", "erlang"},
             {"sample.swift", "swift"},
             {"sample.dart", "dart"},
             {"sample.jl", "julia"},
             {"sample.cr", "crystal"},
             {"sample.f90", "fortran"},
             {"sample.v", "verilog"},
             {"sample.adb", "ada"},
             {"sample.ads", "ada"},
             {"sample.m", "matlab"},
             {"sample.vhd", "vhdl"},
             {"meson.build", "meson"},
             {"sample.nix", "nix"},
             {"sample.scm", "scheme"},
             {"sample.rkt", "racket"},
             {"sample.lisp", "commonlisp"},
             {"sample.fnl", "fennel"},
             {"sample.clj", "clojure"},
             {"sample.gleam", "gleam"},
             {"sample.odin", "odin"},
             {"vlang.v", "v"},
             {"sample.cue", "cue"},
             {"sample.jsonnet", "jsonnet"},
             {"sample.pkl", "pkl"},
             {"sample.typ", "typst"},
             {"sample.res", "rescript"},
             {"sample.proto", "proto"},
             {"sample.sql", "sql"},
         }) {
        INFO("sample: " << file);
        const std::string original = ReadFile(fs::path(NED_REPO_ROOT) / "Tests" / "Format" / "reindent" / file);
        std::string       flat;
        std::size_t       at = 0;
        while (at < original.size()) {
            const std::size_t end  = std::min(original.find('\n', at), original.size());
            const std::string line = original.substr(at, end - at);
            flat += line.substr(std::min(line.find_first_not_of(" \t"), line.size()));
            if (end < original.size()) {
                flat += '\n';
            }
            at = end + 1;
        }
        REQUIRE(flat != original);
        CHECK(IndentAll(flat, BundledMode(language)) == original);
    }
}

TEST_CASE("A re-widened Scala sample reindents back to its house style", "[FormatterProperties]") {
    // Scala 3 reads indentation, so the sample is widened (which keeps its
    // parse) rather than flattened. Braces and Scala 3's indented syntax both.
    for (const std::string file : {"sample.scala", "sample3.scala"}) {
        INFO("sample: " << file);
        const std::string original = ReadFile(fs::path(NED_REPO_ROOT) / "Tests" / "Format" / "reindent" / file);
        std::string       wide;
        bool              atLineStart = true;
        for (const char c : original) {
            wide += (atLineStart && c == ' ') ? std::string(3, ' ') : std::string(1, c);
            atLineStart = (atLineStart && c == ' ') || c == '\n';
        }
        REQUIRE(wide != original);
        RequireSameStructure("scala", original, wide);
        CHECK(IndentAll(wide, BundledMode("scala")) == original);
    }
}

TEST_CASE("A flattened LaTeX sample reindents back, verbatim and math bodies untouched", "[FormatterProperties]") {
    // Verbatim content is literal text and a math body is its author's own
    // alignment: both are String-highlighted, which reindent leaves as written.
    const std::string           original = ReadFile(fs::path(NED_REPO_ROOT) / "Tests" / "Format" / "reindent" / "sample.tex");
    const std::set<std::string> literal  = {"  keep   this", "exactly", "    E = mc^2"};
    std::string                 flat;
    std::size_t                 at = 0;
    while (at < original.size()) {
        const std::size_t end  = std::min(original.find('\n', at), original.size());
        const std::string line = original.substr(at, end - at);
        flat += literal.contains(line) ? line : line.substr(std::min(line.find_first_not_of(" \t"), line.size()));
        if (end < original.size()) {
            flat += '\n';
        }
        at = end + 1;
    }
    REQUIRE(flat != original);
    CHECK(IndentAll(flat, BundledMode("latex")) == original);
}
