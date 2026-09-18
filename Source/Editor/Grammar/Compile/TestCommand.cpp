#include "TestCommand.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>

#include "Editor/Grammar/Corpus.h"
#include "Editor/Grammar/LanguagePackage.h"
#include "Editor/Grammar/Languages.h"
#include "Editor/LanguageParse.h"
#include "Editor/Parse/Parser.h"

namespace ned::editor::grammar::compile {

namespace {

    namespace fs = std::filesystem;

    std::string ReadWhole(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            throw std::runtime_error("cannot read " + path.string());
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    struct PackageLanguage {
        Language    language;
        std::string grammarName; // what a case's :language(...) marker must name to run here
    };

    // The package's grammar: its own when the directory holds one, else the
    // bundled grammar its definition names.
    PackageLanguage LoadPackageLanguage(const fs::path& directory) {
        const std::string          name = directory.filename().string();
        std::optional<std::string> grammar;
        std::string                scannerLibrary;
        if (fs::exists(directory / "language.janet")) {
            const LanguageDefinition definition = ParseLanguageDefinition(name, ReadWhole(directory / "language.janet"));
            grammar                             = definition.grammar.empty() ? definition.name : definition.grammar;
            scannerLibrary                      = definition.scannerLibrary;
        }
        if (fs::exists(directory / "grammar.janet") || fs::exists(directory / "tables"))
            return {LoadLanguagePackage(directory, PackageScanner{.name = grammar.value_or(name), .library = scannerLibrary}), grammar.value_or(name)};
        if (const std::optional<Language> bundled = LanguageByName(grammar.value_or(name)))
            return {*bundled, grammar.value_or(name)};
        throw std::runtime_error(directory.string() + ": no grammar.janet, no tables, and no bundled grammar named '" + grammar.value_or(name) + "'");
    }

    // A corpus shared by several grammars (tree-sitter-csv's csv/tsv/psv,
    // typescript's tsx) marks the other grammars' cases with :language(...);
    // those are skipped here, the way the reference skips them.
    bool CaseAppliesTo(const corpus::Case& item, const PackageLanguage& package, const std::string& packageName) {
        // Upstream spells a grammar name with underscores where a package
        // uses hyphens (ocaml_interface / ocaml-interface).
        const auto matches = [](std::string_view marker, std::string_view name) {
            if (marker.size() != name.size())
                return false;
            for (std::size_t i = 0; i < marker.size(); ++i) {
                const char a = marker[i] == '_' ? '-' : marker[i];
                const char b = name[i] == '_' ? '-' : name[i];
                if (a != b)
                    return false;
            }
            return true;
        };
        for (const std::string& language : item.languages)
            if (language.empty() || matches(language, packageName) || matches(language, package.grammarName))
                return true;
        return false;
    }

    fs::path CorpusDirectory(const fs::path& directory) {
        if (fs::is_directory(directory / "corpus"))
            return directory / "corpus";
        return directory / "test" / "corpus";
    }

    struct Tally {
        std::size_t cases = 0, passed = 0, failed = 0, skipped = 0, blessed = 0;
    };

    Tally TestPackage(const fs::path& directory, bool bless, std::ostream& out) {
        Tally                 tally;
        const PackageLanguage package = LoadPackageLanguage(directory);
        parse::Engine         engine(package.language.Raw());
        const fs::path        corpusDir = CorpusDirectory(directory);
        for (const fs::path& file : corpus::CorpusFiles(corpusDir)) {
            const std::string         content = ReadWhole(file);
            const std::string         label   = fs::relative(file, corpusDir).string();
            std::vector<corpus::Case> cases   = corpus::ParseCorpusFile(content, label);
            std::string               rewritten;
            std::size_t               copied = 0;
            for (const corpus::Case& item : cases) {
                ++tally.cases;
                if (item.skip || !item.platformMatches || !CaseAppliesTo(item, package, directory.filename().string())) {
                    ++tally.skipped;
                    continue;
                }
                const corpus::CaseResult result = corpus::RunCase(engine, item);
                if (result.passed) {
                    ++tally.passed;
                    continue;
                }
                ++tally.failed;
                if (bless && !item.error) {
                    rewritten.append(content, copied, item.expectedStart - copied);
                    rewritten.append("\n" + corpus::BlessedExpected(item, result.actual) + "\n\n");
                    copied = item.expectedEnd;
                    ++tally.blessed;
                    out << "blessed  " << label << ": " << item.name << "\n";
                    continue;
                }
                out << "FAIL     " << label << ": " << item.name << "\n";
                if (item.error)
                    out << "  expected an error, parsed: " << result.actual << "\n";
                else
                    out << "  expected: " << item.expected << "\n  actual:   " << result.actual << "\n";
            }
            if (copied > 0) {
                rewritten.append(content, copied, std::string::npos);
                std::ofstream rewrite(file, std::ios::binary | std::ios::trunc);
                if (!rewrite || !(rewrite << rewritten))
                    throw std::runtime_error("cannot write " + file.string());
            }
        }
        return tally;
    }

} // namespace

int RunTestLanguage(const std::vector<std::string>& directories, bool bless, std::ostream& out, std::ostream& err) {
    if (directories.empty()) {
        err << "ned --test-language: a language directory is required\n";
        return 2;
    }
    int exitCode = 0;
    for (const std::string& source : directories) {
        // Canonical, so `.` and a trailing slash still name the package.
        std::error_code ec;
        const fs::path  directory = fs::weakly_canonical(fs::absolute(source), ec);
        if (ec || !fs::is_directory(directory)) {
            err << source << ": not a directory\n";
            exitCode = 2;
            continue;
        }
        try {
            const Tally tally = TestPackage(directory, bless, out);
            out << directory.filename().string() << ": " << tally.cases << " cases, " << tally.passed << " passed, " << tally.failed << " failed, " << tally.skipped
                << " skipped";
            if (bless)
                out << ", " << tally.blessed << " blessed";
            out << "\n";
            if (tally.cases == 0) {
                err << source << ": no corpus cases under " << CorpusDirectory(directory).string() << "\n";
                exitCode = std::max(exitCode, 1);
            }
            else if (tally.failed > 0 && !bless) {
                exitCode = std::max(exitCode, 1);
            }
        }
        catch (const std::exception& e) {
            err << source << ": " << e.what() << "\n";
            exitCode = 2;
        }
    }
    return exitCode;
}

} // namespace ned::editor::grammar::compile
