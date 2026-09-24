#include <catch2/catch_test_macros.hpp>

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

const char* Mark(bool present) {
    return present ? "✓" : "·";
}

struct Column {
    const char* heading;
    const char* meaning;
};

// clang-format off
const std::vector<Column> kColumns = {
    {"hl",      "highlights query"},
    {"ind",     "indents query (without one, indent comes from the grammar's delimited bodies alone); "
                "`=` -- `:preserve-indent`, a reindent leaves every line as written"},
    {"loc",     "locals query -- scope-aware rename, local-variable highlighting"},
    {"tags",    "tags query -- symbol gutter, outline, breadcrumbs, class/file sync"},
    {"inj",     "injections query -- embedded languages"},
    {"imp",     "imports query -- go-to-file through imports, rename-file fixups"},
    {"test",    "tests query -- test discovery for the test runner"},
    {"sig",     "signatures + calls queries -- change-signature"},
    {"fmt",     "format query -- capture-driven formatter rules can apply"},
    {"style",   "bundled `style.janet` -- formatter rules apply with no user config"},
    {"cmt",     "line-comment prefix -- toggle-line-comment, comment-aware fill; "
                "`b` -- block comment only, which toggle-line-comment wraps each line in"},
    {"root",    "LSP root markers"},
    {"res",     "import resolution config"},
};
// clang-format on
constexpr std::size_t kIndentColumn  = 1;
constexpr std::size_t kCommentColumn = 10;

std::string Render() {
    std::ostringstream out;
    out << "# Language Matrix\n\n"
           "Which capability each bundled language package ships, as ned resolves it (vendored\n"
           "upstream queries and `:queries-from` included). **Generated** from\n"
           "`Source/Languages/` by `Tests/LanguageMatrixDocTest.cpp` -- the test fails when this\n"
           "file is stale. Regenerate with\n\n"
           "    NED_BLESS_LANGUAGE_MATRIX=1 ./build/Tests/ned_tests \"[LanguageMatrixDoc]\"\n\n"
           "A `✓` means the package ships the piece, not that the feature is verified to work\n"
           "well for that language; known behavioural gaps are tracked in `ROADMAP.md`. Folds,\n"
           "bracket matching and sticky scroll are not listed: they come from the grammar's own\n"
           "delimited bodies, except in Markdown and Org, whose structure is not delimiters and\n"
           "which fold from their own query and code. See `LanguageCoverage.md` for tiers and\n"
           "admission policy.\n\n";

    for (const Column& column : kColumns) {
        out << "- **" << column.heading << "** -- " << column.meaning << "\n";
    }
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
    std::size_t              languages = 0;
    for (const LanguageDefinition& definition : BundledLanguages()) {
        const QueryFiles&        own   = definition.queries;
        const QueryFiles&        donor = EffectiveQueries(definition);
        const std::vector<bool>  row   = {
            !Pick(own.highlights, donor.highlights).empty(),
            !Pick(own.indents, donor.indents).empty(),
            !Pick(own.locals, donor.locals).empty(),
            !Pick(own.tags, donor.tags).empty(),
            !Pick(own.injections, donor.injections).empty(),
            !Pick(own.imports, donor.imports).empty(),
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
            out << " " << (preserved ? "=" : blockOnly ? "b"
                                                       : Mark(row[i]))
                << " |";
            totals[i] += row[i] ? 1 : 0;
        }
        out << "\n";
        ++languages;
    }

    out << "| **" << languages << " languages** |";
    for (const std::size_t total : totals) {
        out << " " << total << " |";
    }
    out << "\n";
    return out.str();
}

} // namespace

TEST_CASE("Docs/LanguageMatrix.md matches the bundled language packages", "[LanguageMatrixDoc]") {
    const std::string rendered = Render();
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
