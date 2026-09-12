#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>
#include <stdexcept>
#include <string>

#include "Editor/AutoPair.h"
#include "Editor/BundledLanguages.h"
#include "Editor/Key.h"
#include "Editor/LanguageDefinition.h"
#include "Editor/ModeInternal.h"
#include "Editor/ModeOverrides.h"
#include "Editor/TreeSitter/Queries.h"

using ned::editor::AutoPairSet;
using ned::editor::BundledLanguage;
using ned::editor::BundledLanguages;
using ned::editor::HasModeEscape;
using ned::editor::LanguageDefinition;
using ned::editor::Mode;
using ned::editor::ModeBuildContext;
using ned::editor::ModeFromDefinition;
using ned::editor::ModeNameFor;
using ned::editor::RegisterModeEscape;
using ned::editor::SyntaxClass;

// The bundled set, frozen: a definition added or dropped is a deliberate
// change that should show up here, not only in whichever feature notices.
TEST_CASE("Every bundled definition builds, under its own '-mode' name", "[LanguageDefinition]") {
    const std::set<std::string> kExpected = {
        "fundamental",
        "janet",
        "json",
        "c",
        "cpp",
        "php",
        "javascript",
        "typescript",
        "tsx",
        "html",
        "css",
        "python",
        "bash",
        "fish",
        "xml",
        "rust",
        "go",
        "csharp",
        "java",
        "kotlin",
        "yaml",
        "toml",
        "clojure",
        "jank",
        "markdown",
        "org",
    };
    std::set<std::string> seen;
    for (const LanguageDefinition& definition : BundledLanguages()) {
        seen.insert(definition.name);
        const Mode mode = ModeFromDefinition(definition);
        REQUIRE(mode.name == definition.name + "-mode");
        REQUIRE(ModeNameFor(definition) == mode.name);
        for (const std::string& escape : definition.escapes) {
            REQUIRE(HasModeEscape(escape));
        }
    }
    REQUIRE(seen == kExpected);
}

// The extension table the definitions derive, frozen the same way -- this is
// what ModeForPath resolves against, and what BundledExtensionTable used to
// hardcode.
TEST_CASE("The bundled definitions claim exactly the extensions the old table did", "[LanguageDefinition]") {
    const std::map<std::string, std::string> kExpected = {
        {".janet", "janet"},
        {".json", "json"},
        {".c", "c"},
        {".h", "c"},
        {".cpp", "cpp"},
        {".cc", "cpp"},
        {".cxx", "cpp"},
        {".hpp", "cpp"},
        {".hh", "cpp"},
        {".php", "php"},
        {".phtml", "php"},
        {".js", "javascript"},
        {".mjs", "javascript"},
        {".cjs", "javascript"},
        {".ts", "typescript"},
        {".mts", "typescript"},
        {".cts", "typescript"},
        {".tsx", "tsx"},
        {".html", "html"},
        {".htm", "html"},
        {".css", "css"},
        {".py", "python"},
        {".pyw", "python"},
        {".sh", "bash"},
        {".bash", "bash"},
        {".yaml", "yaml"},
        {".yml", "yaml"},
        {".toml", "toml"},
        {".fish", "fish"},
        {".xml", "xml"},
        {".xsd", "xml"},
        {".xsl", "xml"},
        {".xslt", "xml"},
        {".svg", "xml"},
        {".md", "markdown"},
        {".markdown", "markdown"},
        {".org", "org"},
        {".clj", "clojure"},
        {".cljs", "clojure"},
        {".cljc", "clojure"},
        {".edn", "clojure"},
        {".bb", "clojure"},
        {".jank", "jank"},
        {".rs", "rust"},
        {".go", "go"},
        {".cs", "csharp"},
        {".java", "java"},
        {".kt", "kotlin"},
        {".kts", "kotlin"},
    };
    std::map<std::string, std::string> claimed;
    for (const LanguageDefinition& definition : BundledLanguages()) {
        for (const std::string& extension : definition.extensions) {
            REQUIRE_FALSE(claimed.contains(extension)); // two languages claiming one extension is a bug, not a precedence question
            claimed[extension] = definition.name;
        }
    }
    REQUIRE(claimed == kExpected);
    REQUIRE(ned::editor::ModeForPath("x.kts").name == "kotlin-mode");
    REQUIRE(ned::editor::ModeForPath("x.unknown-extension").name == "fundamental-mode");
}

TEST_CASE("A definition naming an unknown escape or grammar fails to build rather than building less", "[LanguageDefinition]") {
    LanguageDefinition bad{.name = "bad", .grammar = "json", .escapes = {"no.such.escape"}};
    REQUIRE_THROWS_AS(ModeFromDefinition(bad), std::runtime_error);

    LanguageDefinition noGrammar{.name = "nope"};
    REQUIRE_THROWS_AS(ModeFromDefinition(noGrammar), std::runtime_error);
}

TEST_CASE("An escape decorates the generic build and sees the shared parse state", "[LanguageDefinition]") {
    bool sawParser = false;
    RegisterModeEscape("test.probe", [&sawParser](Mode& mode, const LanguageDefinition& definition, const ModeBuildContext& context) {
        sawParser = context.parser != nullptr && context.sharedParse != nullptr && context.language != nullptr;
        REQUIRE(context.languageKey == definition.name);
        REQUIRE(static_cast<bool>(context.highlightQuery));
        mode.lineCommentPrefix = "probe:" + mode.lineCommentPrefix;
    });
    LanguageDefinition definition{.name              = "json",
                                  .lineCommentPrefix = "//",
                                  .queries           = {.highlights = ned::editor::treesitter::queries::kJson},
                                  .escapes           = {"test.probe"}};
    const Mode         mode = ModeFromDefinition(definition);
    REQUIRE(sawParser);
    REQUIRE(mode.lineCommentPrefix == "probe://"); // definition fields land before escapes run
    REQUIRE(static_cast<bool>(mode.highlight));
}

TEST_CASE("Definition fields land on the Mode: comment prefix, wrap, auto-pairs, keymap", "[LanguageDefinition]") {
    LanguageDefinition definition{.name              = "json",
                                  .lineCommentPrefix = "##",
                                  .autoPairs         = AutoPairSet::Lisp,
                                  .wrapLines         = true,
                                  .keymap            = {{"C-c C-z", "undo"}}};
    const Mode         mode = ModeFromDefinition(definition);
    REQUIRE(mode.lineCommentPrefix == "##");
    REQUIRE(mode.wrapLines);
    REQUIRE(mode.autoPairs == ned::editor::LispAutoPairs());
    REQUIRE(mode.keymap.Resolve(ned::editor::ParseKeySequence("C-c C-z")).commandName == "undo");
}

TEST_CASE("A grammar may differ from the name: jank is Clojure's grammar under its own mode", "[LanguageDefinition]") {
    const LanguageDefinition* jank = BundledLanguage("jank");
    REQUIRE(jank != nullptr);
    REQUIRE(jank->grammar == "clojure");
    const Mode mode = ModeFromDefinition(*jank);
    REQUIRE(mode.name == "jank-mode");
    REQUIRE_FALSE(mode.highlight("(defn f [x] x)", {}).empty());
}

TEST_CASE("captureClasses are the language's own defaults for a capture name", "[LanguageDefinition]") {
    REQUIRE(ned::editor::SyntaxClassForCapture("punctuation.special", "markdown") == SyntaxClass::MarkupMarker);
    REQUIRE(ned::editor::SyntaxClassForCapture("punctuation.special", "c") == SyntaxClass::Punctuation);
    REQUIRE(ned::editor::SyntaxClassForCapture("punctuation.special") == SyntaxClass::Punctuation);
}

TEST_CASE("An injections query is highlighted for everyone but synced only where the definition asks", "[LanguageDefinition]") {
    const Mode html = ModeFromDefinition(*BundledLanguage("html"));
    REQUIRE(static_cast<bool>(html.embeddedRegions));
    const Mode markdown = ModeFromDefinition(*BundledLanguage("markdown"));
    REQUIRE_FALSE(static_cast<bool>(markdown.embeddedRegions));
    const Mode org = ModeFromDefinition(*BundledLanguage("org"));
    REQUIRE_FALSE(static_cast<bool>(org.embeddedRegions));
}

TEST_CASE("A grammarless definition builds a mode with no capabilities", "[LanguageDefinition]") {
    const Mode mode = ModeFromDefinition(*BundledLanguage("fundamental"));
    REQUIRE(mode.name == "fundamental-mode");
    REQUIRE_FALSE(static_cast<bool>(mode.highlight));
    REQUIRE_FALSE(static_cast<bool>(mode.fold));
    REQUIRE_FALSE(static_cast<bool>(mode.expandSelection));
    REQUIRE(mode.autoPairs == ned::editor::DefaultAutoPairs());
}
