#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "Editor/BundledLanguages.h"
#include "Editor/LanguageDefinition.h"

// Docs/LanguageMatrix.md, generated from the bundled language definitions:
// which capability each language package actually ships, resolved the way
// ned resolves it (vendored upstream queries and :queries-from included).
// Regenerate after adding a language or a query file:
//
//     NED_BLESS_LANGUAGE_MATRIX=1 ./build/Tests/ned_tests "[LanguageMatrixDoc]"

namespace {

namespace fs = std::filesystem;

using ned::editor::BundledLanguage;
using ned::editor::BundledLanguages;
using ned::editor::LanguageDefinition;
using ned::editor::QueryFiles;

fs::path LanguagesRoot() {
    return fs::path(NED_REPO_ROOT) / "Source" / "Languages";
}

fs::path MatrixPath() {
    return fs::path(NED_REPO_ROOT) / "Docs" / "LanguageMatrix.md";
}

std::string ReadFile(const fs::path& path) {
    std::ifstream      in(path, std::ios::binary);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

// A kind a definition leaves empty falls back to its :queries-from donor.
const QueryFiles& EffectiveQueries(const LanguageDefinition& definition) {
    if (!definition.queriesFrom.empty()) {
        if (const LanguageDefinition* donor = BundledLanguage(definition.queriesFrom)) {
            return donor->queries;
        }
    }
    return definition.queries;
}

std::vector<std::string> Pick(const std::vector<std::string>& own, const std::vector<std::string>& donor) {
    return own.empty() ? donor : own;
}

// Whether any of the files holds a pattern rather than only comments (an
// upstream delta whose one line is `; inherits: html`).
bool HasPatterns(const std::vector<std::string>& files) {
    return std::any_of(files.begin(), files.end(), [](const std::string& file) {
        std::istringstream lines(ReadFile(LanguagesRoot() / file));
        for (std::string line; std::getline(lines, line);) {
            const std::size_t first = line.find_first_not_of(" \t");
            if (first != std::string::npos && line[first] != '#' && line[first] != ';') {
                return true;
            }
        }
        return false;
    });
}

// Whether any of a language's indents query files captures continuations.
bool CapturesContinuations(const std::vector<std::string>& indentFiles) {
    return std::any_of(indentFiles.begin(), indentFiles.end(), [](const std::string& file) {
        return ReadFile(LanguagesRoot() / file).find("@indent.continuation") != std::string::npos;
    });
}

const char* Mark(bool present) {
    return present ? "✓" : "·";
}

struct Column {
    const char* heading;
    // The `:not-applicable` capability name (kNotApplicableCapabilities).
    const char* capability;
    const char* meaning;
};

// clang-format off
const std::vector<Column> kColumns = {
    {"hl",      "highlights",        "highlights query"},
    {"ind",     "indents",           "indents query (without one, indent comes from the grammar's delimited bodies alone)"},
    {"cont",    "continuation",      "continuation lines -- the indents query captures `@indent.continuation`, so "
                                     "`x = a +` then `b` indents the `b` a continuation step"},
    {"loc",     "locals",            "locals query -- scope-aware rename, local-variable highlighting"},
    {"tags",    "tags",              "tags query -- symbol gutter, outline, breadcrumbs, class/file sync"},
    {"inj",     "injections",        "injections query -- embedded languages"},
    {"imp",     "imports",           "imports query -- go-to-file through imports, rename-file fixups"},
    {"test",    "tests",             "tests query -- test discovery for the test runner"},
    {"sig",     "signatures",        "signatures + calls queries -- change-signature"},
    {"fmt",     "format",            "format query -- capture-driven formatter rules can apply"},
    {"style",   "style",             "bundled `style.janet` -- formatter rules apply with no user config"},
    {"cmt",     "comments",          "line-comment prefix -- toggle-line-comment, comment-aware fill"},
    {"root",    "lsp-root",          "LSP root markers"},
    {"res",     "import-resolution", "import resolution config"},
};
// clang-format on
constexpr std::size_t kIndentColumn       = 1;
constexpr std::size_t kContinuationColumn = 2;
constexpr std::size_t kLocalsColumn       = 3;
constexpr std::size_t kTagsColumn         = 4;
constexpr std::size_t kImportsColumn      = 6;
constexpr std::size_t kFormatColumn       = 9;
constexpr std::size_t kStyleColumn        = 10;
constexpr std::size_t kCommentColumn      = 11;
constexpr std::size_t kResolutionColumn   = 13;

bool DeclaredNotApplicable(const LanguageDefinition& definition, std::size_t column) {
    return std::any_of(definition.notApplicable.begin(), definition.notApplicable.end(),
                       [&](const auto& entry) { return entry.first == kColumns[column].capability; });
}

// A column that only refines another follows it: no indentation means no
// continuation lines, no imports nothing to resolve, no formatter nothing
// for a style to configure.
bool NotApplicable(const LanguageDefinition& definition, std::size_t column) {
    if (DeclaredNotApplicable(definition, column)) {
        return true;
    }
    switch (column) {
        case kContinuationColumn:
            return DeclaredNotApplicable(definition, kIndentColumn);
        case kResolutionColumn:
            return DeclaredNotApplicable(definition, kImportsColumn);
        case kStyleColumn:
            return DeclaredNotApplicable(definition, kFormatColumn);
        default:
            return false;
    }
}

// `contradictions` collects "language: capability" for each cell declared
// not applicable that the package ships anyway.
std::string Render(std::vector<std::string>& contradictions) {
    std::ostringstream out;
    out << "# Language Matrix\n\n"
           "Which capability each bundled language package ships, as ned resolves it (vendored\n"
           "upstream queries and `:queries-from` included). **Generated** from\n"
           "`Source/Languages/` by `Tests/LanguageMatrixDocTest.cpp` -- the test fails when this\n"
           "file is stale. Regenerate with\n\n"
           "    NED_BLESS_LANGUAGE_MATRIX=1 ./build/Tests/ned_tests \"[LanguageMatrixDoc]\"\n\n"
           "A `✓` means the package ships the piece, not that the feature is verified to work\n"
           "well for that language. A `·` is an open gap: the capability applies to the language\n"
           "and the package doesn't ship it yet; the totals row counts shipped over applicable.\n"
           "What doesn't apply is declared per language (`:not-applicable`, with its reason). Folds,\n"
           "bracket matching and sticky scroll are not listed: they come from the grammar's own\n"
           "delimited bodies, except in Markdown and Org, whose structure is not delimiters and\n"
           "which fold from their own query and code. See `LanguageCoverage.md` for tiers and\n"
           "admission policy.\n\n";

    for (const Column& column : kColumns) {
        out << "- **" << column.heading << "** -- " << column.meaning << "\n";
    }
    out << "\nMarks:\n\n"
           "- `✓` -- the package ships it\n"
           "- `·` -- it doesn't, and should: an open gap\n"
           "- `–` -- doesn't apply to this language (`:not-applicable` in its `language.janet`, with the "
           "reason)\n"
           "- `=` (**ind**) -- `:preserve-indent`: indentation is syntax, so a reindent leaves every line as "
           "written\n"
           "- `i` (**loc**) -- no locals query of its own; rename reads its embedded scripts' locals, and a "
           "name bound at a script's top level declines (`:injected-locals`)\n"
           "- `i` (**tags**) -- no tags query of its own; the outline is what its embedded languages define "
           "(`:injected-symbols`)\n"
           "- `i` (**imp**) -- no imports query of its own; go-to-file and move fixups read its embedded "
           "scripts' imports (`:injected-imports`)\n"
           "- `b` (**cmt**) -- block comments only (`/* */`, `<!-- -->`, `(* *)`); toggle-line-comment wraps "
           "each line in one\n";
    out << "\n| language |";
    for (const Column& column : kColumns) {
        out << " " << column.heading << " |";
    }
    out << "\n|---|";
    for (std::size_t i = 0; i < kColumns.size(); ++i) {
        out << ":-:|";
    }
    out << "\n";

    std::vector<std::size_t> totals(kColumns.size(), 0);
    std::vector<std::size_t> applicable(kColumns.size(), 0);
    std::size_t              languages = 0;
    for (const LanguageDefinition& definition : BundledLanguages()) {
        const QueryFiles&        own   = definition.queries;
        const QueryFiles&        donor = EffectiveQueries(definition);
        const std::vector<bool>  row   = {
            !Pick(own.highlights, donor.highlights).empty(),
            !Pick(own.indents, donor.indents).empty() || definition.preserveIndent,
            CapturesContinuations(Pick(own.indents, donor.indents)),
            HasPatterns(Pick(own.locals, donor.locals)) || definition.injectedLocals,
            !Pick(own.tags, donor.tags).empty() || definition.injectedSymbols,
            !Pick(own.injections, donor.injections).empty(),
            !Pick(own.imports, donor.imports).empty() || definition.injectedImports,
            !Pick(own.tests, donor.tests).empty(),
            !Pick(own.signatures, donor.signatures).empty() && !Pick(own.calls, donor.calls).empty(),
            !Pick(own.format, donor.format).empty(),
            fs::exists(LanguagesRoot() / definition.name / "style.janet"),
            !definition.lineCommentPrefix.empty() || !definition.blockCommentOpen.empty(),
            !definition.lspRootMarkers.empty(),
            definition.importResolution.has_value(),
        };
        out << "| " << definition.name << " |";
        for (std::size_t i = 0; i < row.size(); ++i) {
            const bool preserved = i == kIndentColumn && definition.preserveIndent;
            const bool blockOnly = i == kCommentColumn && definition.lineCommentPrefix.empty() && row[i];
            const bool injected      = (i == kLocalsColumn && definition.injectedLocals && !HasPatterns(Pick(own.locals, donor.locals))) ||
                                       (i == kTagsColumn && definition.injectedSymbols && Pick(own.tags, donor.tags).empty()) ||
                                       (i == kImportsColumn && definition.injectedImports &&
                                        Pick(own.imports, donor.imports).empty());
            const bool notApplicable = NotApplicable(definition, i);
            if (notApplicable && row[i]) {
                contradictions.push_back(definition.name + ": " + kColumns[i].capability);
            }
            out << " " << (preserved ? "=" : blockOnly   ? "b"
                                         : injected      ? "i"
                                         : notApplicable ? "–"
                                                         : Mark(row[i]))
                << " |";
            totals[i] += row[i] ? 1 : 0;
            applicable[i] += notApplicable ? 0 : 1;
        }
        out << "\n";
        ++languages;
    }

    out << "| **" << languages << " languages** |";
    for (std::size_t i = 0; i < kColumns.size(); ++i) {
        out << " " << totals[i] << "/" << applicable[i] << " |";
    }
    out << "\n";
    return out.str();
}

} // namespace

TEST_CASE("Docs/LanguageMatrix.md matches the bundled language packages", "[LanguageMatrixDoc]") {
    std::vector<std::string> contradictions;
    const std::string        rendered = Render(contradictions);
    std::string              shippedAnyway;
    for (const std::string& contradiction : contradictions) {
        shippedAnyway += "\n  " + contradiction;
    }
    INFO("declared :not-applicable but shipped anyway:" << shippedAnyway);
    CHECK(contradictions.empty());
    if (std::getenv("NED_BLESS_LANGUAGE_MATRIX") != nullptr) {
        std::ofstream out(MatrixPath(), std::ios::binary | std::ios::trunc);
        REQUIRE(out);
        out << rendered;
        SUCCEED("regenerated " + MatrixPath().string() + " -- read the diff");
        return;
    }

    INFO("regenerate: NED_BLESS_LANGUAGE_MATRIX=1 ./build/Tests/ned_tests \"[LanguageMatrixDoc]\"");
    REQUIRE(fs::exists(MatrixPath()));
    CHECK(ReadFile(MatrixPath()) == rendered);
}
