#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/BundledLanguages.h"
#include "Editor/Grammar/Languages.h"
#include "Editor/Grammar/MatchCache.h"
#include "Editor/Grammar/Parser.h"
#include "Editor/Grammar/QueryMatcher.h"
#include "Editor/Grammar/Tree.h"
#include "Editor/LanguageFiles.h"
#include "Editor/Parse/Cursor.h"
#include "Editor/Parse/Node.h"
#include "Editor/Parse/Parser.h"
#include "Editor/Parse/Sexp.h"
#include "Text/OffsetRemap.h"

// The conformance bar for the parsing engine.
//
// Runs every bundled language's corpus (Source/Languages/<name>/corpus/,
// imported from the grammar's upstream test corpus and owned here since;
// read from the build tree's data copy, Editor/LanguageFiles.h) against the
// production parser and holds the per-corpus scorecard as a golden file.
//
// The corpus format and comparison rules are a faithful port of
// tree-sitter's own runner (cli/src/test.rs), so imported corpora run
// unchanged:
//  - a header is  ===fence [suffix] / name+marker lines / ===fence [suffix];
//    the FIRST header's suffix becomes mandatory on every later header and
//    divider line, which is how corpora whose sources contain `===`/`---`
//    lines (markdown setext, YAML document markers) stay parseable;
//  - the input/expected divider is the LONGEST ---fence line between two
//    headers (last one on a tie, matching Rust's max_by_key);
//  - expected output is compared as a whitespace-collapsed string against
//    ts_node_string(), with ` field: (` prefixes stripped from the actual
//    when the expectation names no fields;
//  - `:skip` skips, `:error` asserts only that the tree contains an error,
//    `:language(x)` routes a case to a dialect grammar.
//
// Regenerate after an intentional change:
//
//     NED_BLESS_PARSE_CONFORMANCE=1 ./build/ned_tests "[ParseConformance]"
//
// then read the diff before committing it.

namespace {

namespace fs = std::filesystem;
using ned::editor::grammar::Language;
using ned::editor::grammar::LanguageByName;
using ned::editor::grammar::Parser;
using ned::editor::grammar::Tree;

fs::path GoldenDir() {
    return fs::path(NED_REPO_ROOT) / "Tests" / "ParseConformance";
}

bool Blessing() {
    return std::getenv("NED_BLESS_PARSE_CONFORMANCE") != nullptr;
}

struct DialectMapping {
    std::string_view attribute; // the :language(...) value in the corpus
    std::string_view language;  // ned LanguageByName key; empty = grammar not linked, skip
};

struct CorpusSource {
    std::string_view            directory;       // relative to BundledLanguagesRoot()
    std::string_view            defaultLanguage; // for cases with no :language attribute
    std::vector<DialectMapping> dialects{};
};

// One entry per corpus directory. A corpus imported from a multi-grammar
// repo routes its :language(...) cases through `dialects` (typescript's
// corpus carries the tsx cases; php's and xml's name grammars ned doesn't
// bundle, which skip).
const std::vector<CorpusSource>& CorpusSources() {
    static const std::vector<CorpusSource> sources = {
        {"bash/corpus", "bash"},
        {"c/corpus", "c"},
        {"csharp/corpus", "csharp"},
        {"clojure/corpus", "clojure"},
        {"cmake/corpus", "cmake"},
        {"cpp/corpus", "cpp"},
        {"css/corpus", "css"},
        {"diff/corpus", "diff"},
        {"fish/corpus", "fish"},
        {"go/corpus", "go"},
        {"html/corpus", "html"},
        {"janet/corpus", "janet"},
        {"java/corpus", "java"},
        {"javascript/corpus", "javascript"},
        {"json/corpus", "json"},
        {"kotlin/corpus", "kotlin"},
        {"lua/corpus", "lua"},
        {"markdown/corpus", "markdown"},
        {"markdown-inline/corpus", "markdown-inline"},
        {"org/corpus", "org"},
        {"php/corpus", "php", {{"php", "php"}, {"php_only", ""}}},
        {"python/corpus", "python"},
        {"rust/corpus", "rust"},
        {"sql/corpus", "sql"},
        {"toml/corpus", "toml"},
        {"typescript/corpus", "typescript", {{"typescript", "typescript"}, {"tsx", "tsx"}}},
        {"xml/corpus", "xml", {{"xml", "xml"}, {"dtd", ""}}},
        {"yaml/corpus", "yaml"},
        {"dockerfile/corpus", "dockerfile"},
        {"make/corpus", "make"},
        {"hcl/corpus", "hcl"},
        {"nix/corpus", "nix"},
        {"ruby/corpus", "ruby"},
        {"gitcommit/corpus", "gitcommit"},
        {"gitrebase/corpus", "gitrebase"},
        {"r/corpus", "r"},
    };
    return sources;
}

// The corpus files under one source, sorted. tree-sitter-make is the one
// grammar whose corpus files end in .mk, not .txt.
std::vector<fs::path> CorpusFiles(const CorpusSource& source) {
    const fs::path        corpusDir = ned::editor::BundledLanguagesRoot() / source.directory;
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(corpusDir))
        if (entry.is_regular_file() && (entry.path().extension() == ".txt" || entry.path().extension() == ".mk"))
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    return files;
}

std::string CorpusLabel(const CorpusSource& source, const fs::path& file) {
    return fs::relative(file, ned::editor::BundledLanguagesRoot() / source.directory).string();
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

bool IsWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// --- Corpus file parsing -----------------------------------------------------

struct Line {
    std::size_t      start; // byte offset of the line's first character
    std::size_t      end;   // byte offset one past the newline (or EOF)
    std::string_view text;  // without the trailing \n / \r\n
};

std::vector<Line> SplitLines(std::string_view content) {
    std::vector<Line> lines;
    std::size_t       pos = 0;
    while (pos < content.size()) {
        std::size_t      nl   = content.find('\n', pos);
        std::size_t      end  = nl == std::string_view::npos ? content.size() : nl + 1;
        std::string_view text = content.substr(pos, (nl == std::string_view::npos ? content.size() : nl) - pos);
        if (!text.empty() && text.back() == '\r')
            text.remove_suffix(1);
        lines.push_back({pos, end, text});
        pos = end;
    }
    return lines;
}

// A fence is a maximal run of at least three of `marker`; the suffix is the
// rest of the line (which by maximality cannot begin with the marker).
// std::nullopt suffix = none; upstream's suffix capture never matches empty.
struct Fence {
    std::size_t                     length;
    std::optional<std::string_view> suffix;
};

std::optional<Fence> ParseFence(std::string_view line, char marker) {
    std::size_t run = 0;
    while (run < line.size() && line[run] == marker)
        ++run;
    if (run < 3)
        return std::nullopt;
    if (run == line.size())
        return Fence{run, std::nullopt};
    return Fence{run, line.substr(run)};
}

// Upstream's name/marker line rule: first char is not '=' (a fence-ish line
// ends the run), or the line is whitespace followed by ':'.
bool IsNameOrMarkerLine(std::string_view line) {
    if (line.empty())
        return true; // "[^=\r\n]" cannot match, but an empty line has no '=' either; upstream's
                     // regex requires >=1 char, so treat empty as terminating below instead.
    if (line.front() != '=')
        return true;
    std::size_t i = 0;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])) != 0)
        ++i;
    return i > 0 && i < line.size() && line[i] == ':';
}

struct CorpusCase {
    std::string              name;
    std::string              file; // corpus-relative path, for reporting
    std::string              input;
    std::string              expected; // normalized; empty for :error cases
    bool                     skip            = false;
    bool                     error           = false;
    bool                     platformMatches = true;
    bool                     hasFields       = false;
    std::vector<std::string> languages; // :language(...) values; single "" if none
};

// upstream: COMMENT_REGEX ^\s*;.*$ removed, trim, \s+ -> ' ', " )" -> ")"
std::string NormalizeExpected(std::string_view raw) {
    std::string noComments;
    noComments.reserve(raw.size());
    std::size_t pos = 0;
    while (pos <= raw.size()) {
        std::size_t      nl   = raw.find('\n', pos);
        std::string_view line = raw.substr(pos, (nl == std::string_view::npos ? raw.size() : nl) - pos);
        std::size_t      ws   = 0;
        while (ws < line.size() && std::isspace(static_cast<unsigned char>(line[ws])) != 0)
            ++ws;
        const bool comment = ws < line.size() && line[ws] == ';';
        if (!comment)
            noComments.append(line);
        if (nl == std::string_view::npos)
            break;
        noComments.push_back('\n');
        pos = nl + 1;
    }
    std::string collapsed;
    collapsed.reserve(noComments.size());
    bool inSpace = false;
    for (char c : noComments) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            inSpace = true;
            continue;
        }
        if (inSpace && !collapsed.empty() && c != ')')
            collapsed.push_back(' ');
        inSpace = false;
        collapsed.push_back(c);
    }
    return collapsed;
}

bool HasFieldSyntax(std::string_view s) {
    // upstream: regex " \w+: \("
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != ' ')
            continue;
        std::size_t j = i + 1;
        while (j < s.size() && IsWordChar(s[j]))
            ++j;
        if (j == i + 1)
            continue;
        if (j + 2 < s.size() && s[j] == ':' && s[j + 1] == ' ' && s[j + 2] == '(')
            return true;
    }
    return false;
}

std::string StripSexpFields(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        if (s[i] == ' ') {
            std::size_t j = i + 1;
            while (j < s.size() && IsWordChar(s[j]))
                ++j;
            if (j > i + 1 && j + 2 < s.size() && s[j] == ':' && s[j + 1] == ' ' && s[j + 2] == '(') {
                out += " (";
                i = j + 3;
                continue;
            }
        }
        out.push_back(s[i++]);
    }
    return out;
}

struct HeaderMatch {
    std::size_t              startOffset; // of the opening fence line
    std::size_t              endOffset;   // one past the closing fence's newline
    std::string              name;
    bool                     skip            = false;
    bool                     error           = false;
    bool                     platformMatches = true;
    std::vector<std::string> languages;
};

std::vector<CorpusCase> ParseCorpusFile(std::string_view content, const std::string& fileLabel) {
    const std::vector<Line> lines = SplitLines(content);

    // Pass 1: header candidates (fence / >=1 name-marker lines / fence),
    // consumed non-overlapping, exactly as captures_iter does.
    struct Candidate {
        std::size_t                     startOffset, endOffset;
        std::optional<std::string_view> suffix1, suffix2;
        std::size_t                     nameFirst, nameLast; // line indices of the name/marker block
    };
    std::vector<Candidate> candidates;
    std::size_t            i = 0;
    while (i < lines.size()) {
        const auto open = ParseFence(lines[i].text, '=');
        if (!open) {
            ++i;
            continue;
        }
        std::size_t j          = i + 1;
        bool        blockValid = true;
        while (j < lines.size() && !ParseFence(lines[j].text, '=')) {
            if (lines[j].text.empty()) {
                // Upstream's name-line rule is ([^=\r\n]|\s+:)[^\r\n]* and \s
                // matches newlines, so a blank-line run is legal exactly when
                // it is absorbed as the leading whitespace of a following
                // `:attribute` line (the xml corpus's shape).
                std::size_t k = j + 1;
                while (k < lines.size() && lines[k].text.empty())
                    ++k;
                std::string_view next = k < lines.size() ? lines[k].text : std::string_view{};
                std::size_t      ws   = 0;
                while (ws < next.size() && std::isspace(static_cast<unsigned char>(next[ws])) != 0)
                    ++ws;
                if (k < lines.size() && !ParseFence(next, '=') && ws < next.size() && next[ws] == ':') {
                    j = k;
                    continue;
                }
                blockValid = false;
                break;
            }
            if (!IsNameOrMarkerLine(lines[j].text)) {
                blockValid = false;
                break;
            }
            ++j;
        }
        const bool haveNames = j > i + 1;
        const auto close     = blockValid && j < lines.size() ? ParseFence(lines[j].text, '=') : std::nullopt;
        if (haveNames && close) {
            candidates.push_back({lines[i].start, lines[j].end, open->suffix, close->suffix, i + 1, j - 1});
            i = j + 1;
        }
        else {
            ++i;
        }
    }
    if (candidates.empty())
        return {};

    // The first candidate's opening suffix governs the whole file.
    const std::optional<std::string_view> fileSuffix = candidates.front().suffix1;

    std::vector<HeaderMatch> headers;
    for (const Candidate& c : candidates) {
        if (c.suffix1 != fileSuffix || c.suffix2 != fileSuffix)
            continue;
        HeaderMatch h;
        h.startOffset   = c.startOffset;
        h.endOffset     = c.endOffset;
        bool seenMarker = false;
        for (std::size_t li = c.nameFirst; li <= c.nameLast; ++li) {
            const std::string_view line    = lines[li].text;
            std::string_view       trimmed = line;
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())) != 0)
                trimmed.remove_prefix(1);
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())) != 0)
                trimmed.remove_suffix(1);
            const std::string_view beforeParen = trimmed.substr(0, trimmed.find('('));
            if (beforeParen == ":skip") {
                seenMarker = true;
                h.skip     = true;
            }
            else if (beforeParen == ":fail-fast") {
                seenMarker = true; // fail-fast has no meaning for a scorecard run
            }
            else if (beforeParen == ":error") {
                seenMarker = true;
                h.error    = true;
            }
            else if (beforeParen == ":platform" && trimmed.size() > 10 && trimmed.back() == ')') {
                seenMarker                   = true;
                const std::string_view value = trimmed.substr(10, trimmed.size() - 11);
                h.platformMatches            = value == "linux";
            }
            else if (beforeParen == ":language" && trimmed.size() > 10 && trimmed.back() == ')') {
                seenMarker = true;
                h.languages.emplace_back(trimmed.substr(10, trimmed.size() - 11));
            }
            else if (!seenMarker) {
                h.name.append(line);
                h.name.push_back('\n');
            }
        }
        while (!h.name.empty() && std::isspace(static_cast<unsigned char>(h.name.back())) != 0)
            h.name.pop_back();
        if (h.skip)
            h.error = false;
        if (h.languages.empty())
            h.languages.emplace_back("");
        headers.push_back(std::move(h));
    }

    // Pass 2: pair consecutive headers; the longest suffix-matching divider
    // line between them splits input from expected (last one on a tie).
    std::vector<CorpusCase> cases;
    for (std::size_t hi = 0; hi < headers.size(); ++hi) {
        const HeaderMatch& h          = headers[hi];
        const std::size_t  segmentEnd = hi + 1 < headers.size() ? headers[hi + 1].startOffset : content.size();

        std::optional<Line> divider;
        std::size_t         bestLength = 0;
        for (const Line& line : lines) {
            if (line.start < h.endOffset || line.end > segmentEnd)
                continue;
            const auto fence = ParseFence(line.text, '-');
            if (!fence || fence->suffix != fileSuffix)
                continue;
            const std::size_t matchLength = line.end - line.start;
            if (matchLength >= bestLength) {
                bestLength = matchLength;
                divider    = line;
            }
        }
        if (!divider)
            continue;

        CorpusCase item;
        item.name            = h.name;
        item.file            = fileLabel;
        item.skip            = h.skip;
        item.error           = h.error;
        item.platformMatches = h.platformMatches;
        item.languages       = h.languages;
        item.input           = std::string(content.substr(h.endOffset, divider->start - h.endOffset));
        if (!item.input.empty() && item.input.back() == '\n')
            item.input.pop_back();
        item.expected  = NormalizeExpected(content.substr(divider->end, segmentEnd - divider->end));
        item.hasFields = HasFieldSyntax(item.expected);
        cases.push_back(std::move(item));
    }
    return cases;
}

// --- Running -----------------------------------------------------------------

std::string ActualSexp(const Tree& tree, bool keepFields) {
    std::string sexp = ned::editor::parse::SubtreeToSexp(tree.Green().Root(), tree.Green().Language());
    if (!keepFields)
        sexp = StripSexpFields(sexp);
    return sexp;
}

// Holds `rendered` against the golden file `name` under Tests/ParseConformance/,
// or rewrites it when blessing.
void HoldAgainstGolden(const char* name, const std::string& rendered) {
    const fs::path golden = GoldenDir() / name;
    if (Blessing()) {
        fs::create_directories(golden.parent_path());
        std::ofstream out(golden, std::ios::binary | std::ios::trunc);
        REQUIRE(out);
        out << rendered;
        SUCCEED("blessed " + golden.string());
        return;
    }
    INFO("golden: " << golden.string() << "  (regenerate: NED_BLESS_PARSE_CONFORMANCE=1 ./build/ned_tests \"[ParseConformance]\")");
    REQUIRE(fs::exists(golden));
    CHECK(ReadFile(golden) == rendered);
}

struct CorpusScore {
    std::size_t              cases   = 0;
    std::size_t              runs    = 0;
    std::size_t              passed  = 0;
    std::size_t              failed  = 0;
    std::size_t              skipped = 0;
    std::vector<std::string> failures;
};

} // namespace

TEST_CASE("Bundled corpora conformance scorecard matches the blessed baseline", "[ParseConformance][Corpus]") {
    for (const CorpusSource& source : CorpusSources()) {
        INFO("corpus directory: " << source.directory);
        REQUIRE(fs::is_directory(ned::editor::BundledLanguagesRoot() / source.directory));
    }

    std::map<std::string, std::unique_ptr<Parser>, std::less<>> parsers;
    const auto                                                  parserFor = [&parsers](std::string_view languageName) -> Parser* {
        auto found = parsers.find(languageName);
        if (found != parsers.end())
            return found->second.get();
        const std::optional<Language> language = LanguageByName(languageName);
        if (!language)
            return nullptr;
        return parsers.emplace(std::string(languageName), std::make_unique<Parser>(*language)).first->second.get();
    };

    std::ostringstream rendered;
    rendered << "# Parse conformance scorecard: bundled corpora vs the production engine.\n"
             << "# Regenerate: NED_BLESS_PARSE_CONFORMANCE=1 ./build/ned_tests \"[ParseConformance]\"\n";

    CorpusScore total;
    for (const CorpusSource& source : CorpusSources()) {
        CorpusScore score;
        for (const fs::path& file : CorpusFiles(source)) {
            const std::string content = ReadFile(file);
            const std::string label   = CorpusLabel(source, file);
            for (CorpusCase& item : ParseCorpusFile(content, label)) {
                ++score.cases;
                if (item.skip || !item.platformMatches) {
                    ++score.skipped;
                    continue;
                }
                for (const std::string& attributeLanguage : item.languages) {
                    std::string_view languageName = source.defaultLanguage;
                    if (!attributeLanguage.empty()) {
                        languageName = {};
                        for (const DialectMapping& dialect : source.dialects)
                            if (dialect.attribute == attributeLanguage)
                                languageName = dialect.language;
                    }
                    if (languageName.empty()) {
                        ++score.skipped;
                        continue;
                    }
                    Parser* parser = parserFor(languageName);
                    REQUIRE(parser != nullptr);
                    ++score.runs;

                    const Tree tree   = parser->Parse(item.input);
                    bool       passed = false;
                    if (item.error) {
                        passed = tree.Green().HasError();
                    }
                    else {
                        passed = ActualSexp(tree, item.hasFields) == item.expected;
                    }
                    if (passed) {
                        ++score.passed;
                    }
                    else {
                        if (std::getenv("NED_PARSE_CONFORMANCE_VERBOSE") != nullptr) {
                            std::cerr << "case:     " << item.file << ": " << item.name << "\n"
                                      << "expected: " << item.expected << "\n"
                                      << "actual:   " << ActualSexp(tree, item.hasFields) << "\n\n";
                        }
                        ++score.failed;
                        score.failures.push_back(item.file + ": " + item.name +
                                                 (item.languages.size() > 1 || !attributeLanguage.empty()
                                                      ? " [" + std::string(languageName) + "]"
                                                      : ""));
                    }
                }
            }
        }

        rendered << source.directory << "  cases=" << score.cases << " runs=" << score.runs
                 << " passed=" << score.passed << " failed=" << score.failed << " skipped=" << score.skipped
                 << "\n";
        std::sort(score.failures.begin(), score.failures.end());
        for (const std::string& failure : score.failures)
            rendered << "  FAIL " << failure << "\n";

        total.cases += score.cases;
        total.runs += score.runs;
        total.passed += score.passed;
        total.failed += score.failed;
        total.skipped += score.skipped;
    }
    rendered << "total  cases=" << total.cases << " runs=" << total.runs << " passed=" << total.passed
             << " failed=" << total.failed << " skipped=" << total.skipped << "\n";

    // The corpora are real: an empty scorecard means the walk broke, not that
    // the corpus vanished.
    CHECK(total.cases > 2000);
    CHECK(total.runs > 2000);
    HoldAgainstGolden("baseline.txt", rendered.str());
}

// --- The engine against every corpus, case by case ---------------------------
//
// Expected-sexp comparison uses exactly the scorecard's rules; unlike the
// scorecard this names each failing case, so a regression reads as "this
// case broke" rather than "a count changed".

TEST_CASE("Ned parse engine matches the bundled corpora", "[ParseEngine][Corpus]") {
    // The markdown-inline grammar's shipped tables were not generated with
    // the extensions these three files exercise; the scorecard baseline pins
    // the same 8 failures.
    const auto isKnownBaselineFailure = [](std::string_view corpus, std::string_view file) {
        return corpus == "markdown-inline/corpus" &&
               (file == "extension_wikilink.txt" || file == "tags.txt" || file == "spec.txt");
    };

    std::size_t                                                totalRuns = 0;
    std::vector<std::string>                                   failures;
    std::map<std::string, std::pair<std::size_t, std::size_t>> perCorpus; // {failed, run}
    for (const CorpusSource& source : CorpusSources()) {
        std::map<std::string, std::unique_ptr<ned::editor::parse::Engine>, std::less<>> engines;
        const auto                                                                      engineFor = [&](std::string_view languageName) -> ned::editor::parse::Engine* {
            auto found = engines.find(languageName);
            if (found != engines.end())
                return found->second.get();
            const std::optional<Language> language = LanguageByName(languageName);
            REQUIRE(language.has_value());
            return engines.emplace(std::string(languageName), std::make_unique<ned::editor::parse::Engine>(language->Raw()))
                .first->second.get();
        };

        for (const fs::path& file : CorpusFiles(source)) {
            const std::string content = ReadFile(file);
            const std::string label   = CorpusLabel(source, file);
            for (CorpusCase& item : ParseCorpusFile(content, label)) {
                if (item.skip || !item.platformMatches)
                    continue;
                for (const std::string& attributeLanguage : item.languages) {
                    std::string_view languageName = source.defaultLanguage;
                    if (!attributeLanguage.empty()) {
                        languageName = {};
                        for (const DialectMapping& dialect : source.dialects)
                            if (dialect.attribute == attributeLanguage)
                                languageName = dialect.language;
                    }
                    if (languageName.empty())
                        continue;

                    ++totalRuns;
                    ++perCorpus[std::string(source.directory)].second;
                    ned::editor::parse::Engine* engine = engineFor(languageName);

                    const auto recordFailure = [&](const std::string& detail) {
                        ++perCorpus[std::string(source.directory)].first;
                        if (!isKnownBaselineFailure(source.directory, item.file))
                            failures.push_back(std::string(source.directory) + "/" + item.file + ": " + item.name + detail);
                    };

                    const ned::editor::parse::GreenTree tree = engine->Parse(item.input);
                    if (tree.IsNull()) {
                        recordFailure(" -- null tree");
                        continue;
                    }

                    if (item.error) {
                        if (!tree.HasError())
                            recordFailure(" -- expected an error");
                        continue;
                    }

                    std::string actual = ned::editor::parse::SubtreeToSexp(tree.Root(), tree.Language());
                    if (!item.hasFields)
                        actual = StripSexpFields(actual);
                    if (actual != item.expected) {
                        recordFailure("");
                        if (std::getenv("NED_PARSE_CONFORMANCE_VERBOSE") != nullptr) {
                            std::cerr << "case:     " << source.directory << "/" << item.file << ": " << item.name << "\n"
                                      << "expected: " << item.expected << "\n"
                                      << "ned:      " << actual << "\n\n";
                        }
                    }
                }
            }
        }
    }

    if (std::getenv("NED_PARSE_CONFORMANCE_VERBOSE") != nullptr) {
        for (const auto& [corpus, score] : perCorpus)
            std::cerr << corpus << ": " << score.first << " failed of " << score.second << "\n";
    }

    CHECK(totalRuns > 2500);
    INFO("first failures: " << [&] {
        std::string joined;
        for (std::size_t i = 0; i < failures.size() && i < 20; i++)
            joined += "\n  " + failures[i];
        return joined;
    }());
    CHECK(failures.size() == 0);
}

// --- Incremental reparse ------------------------------------------------------
//
// For every corpus input of a representative grammar set (the scanner-free
// five plus the scanner-heavy proving load), apply a chain of scripted edits;
// after each step the incremental reparse must equal a from-scratch parse of
// the same text. A handful of steps legitimately differ -- the incremental
// parser reuses subtrees into a different (equally valid) tree, exactly as
// tree-sitter's own does over the same edits (verified against it when the
// engine landed) -- and those are pinned BY NAME in a golden so the set can
// neither grow nor drift silently.

namespace {

ned::editor::parse::abi::Point PointAtOffset(std::string_view text, std::size_t offset) {
    ned::editor::parse::abi::Point point = {0, 0};
    for (std::size_t i = 0; i < offset && i < text.size(); i++) {
        if (text[i] == '\n') {
            point.row++;
            point.column = 0;
        }
        else {
            point.column++;
        }
    }
    return point;
}

struct ScriptedEdit {
    std::size_t position;
    std::size_t deletedLength;
    std::string insertedText;
};

ned::editor::parse::InputEdit MakeEdit(std::string_view oldText, std::string_view newText, const ScriptedEdit& edit) {
    return {
        .startByte   = static_cast<std::uint32_t>(edit.position),
        .oldEndByte  = static_cast<std::uint32_t>(edit.position + edit.deletedLength),
        .newEndByte  = static_cast<std::uint32_t>(edit.position + edit.insertedText.size()),
        .startPoint  = PointAtOffset(oldText, edit.position),
        .oldEndPoint = PointAtOffset(oldText, edit.position + edit.deletedLength),
        .newEndPoint = PointAtOffset(newText, edit.position + edit.insertedText.size()),
    };
}

} // namespace

TEST_CASE("Ned parse engine incremental reparses match from-scratch parses", "[ParseEngineIncremental][ParseConformance][Corpus]") {
    const std::vector<std::string_view> grammars = {"json", "c", "go", "java", "clojure", "markdown",
                                                    "yaml", "bash", "python", "cpp", "rust", "markdown-inline"};

    std::size_t              totalSteps = 0;
    std::vector<std::string> divergences;
    for (const CorpusSource& source : CorpusSources()) {
        bool included = false;
        for (const std::string_view grammar : grammars)
            included = included || source.defaultLanguage == grammar;
        if (!included)
            continue;

        const std::optional<Language> language = LanguageByName(source.defaultLanguage);
        REQUIRE(language.has_value());
        ned::editor::parse::Engine engine(language->Raw());

        for (const fs::path& file : CorpusFiles(source)) {
            const std::string content = ReadFile(file);
            const std::string label   = CorpusLabel(source, file);
            for (CorpusCase& item : ParseCorpusFile(content, label)) {
                if (item.skip || !item.platformMatches || !item.languages.front().empty())
                    continue;
                if (item.input.size() < 4)
                    continue;

                std::string                   text    = item.input;
                ned::editor::parse::GreenTree nedTree = engine.Parse(text);

                const std::vector<ScriptedEdit> edits = {
                    {text.size() / 2, 0, "x"},
                    {text.size() / 3, 1, ""},
                    {(text.size() * 2) / 3, 0, "\n"},
                };
                std::size_t step = 0;
                for (const ScriptedEdit& edit : edits) {
                    ++step;
                    std::string newText = text;
                    newText.erase(edit.position, edit.deletedLength);
                    newText.insert(edit.position, edit.insertedText);

                    const ned::editor::parse::GreenTree edited      = nedTree.WithEdit(MakeEdit(text, newText, edit));
                    ned::editor::parse::GreenTree       incremental = engine.Parse(newText, edited);
                    const ned::editor::parse::GreenTree scratch     = engine.Parse(newText);

                    ++totalSteps;
                    if (ned::editor::parse::SubtreeToSexp(incremental.Root(), incremental.Language()) !=
                        ned::editor::parse::SubtreeToSexp(scratch.Root(), scratch.Language()))
                        divergences.push_back(std::string(source.directory) + "/" + item.file + ": " + item.name +
                                              " #" + std::to_string(step));

                    text    = std::move(newText);
                    nedTree = std::move(incremental);
                }
            }
        }
    }

    CHECK(totalSteps > 4000);

    std::sort(divergences.begin(), divergences.end());
    std::string rendered = "# Incremental-vs-scratch divergences (corpus/file: case #edit step).\n"
                           "# Each is a valid alternative tree the incremental parser's subtree reuse\n"
                           "# produces, matching tree-sitter's own behavior over the same edits.\n"
                           "# Regenerate: NED_BLESS_PARSE_CONFORMANCE=1 ./build/ned_tests \"[ParseConformance]\"\n";
    for (const std::string& divergence : divergences)
        rendered += divergence + "\n";
    HoldAgainstGolden("incremental-divergences.txt", rendered);
}

// --- per-subtree-fact-memoization follow-up: MatchCache differential -------
//
// Reuses this file's exact corpus + scripted-edit machinery, but checks a
// different invariant than M4: for every step, MatchCache::Reconcile's own
// output must equal a fresh, unwindowed QueryMatcher::Matches() call on the
// post-edit text -- run over each grammar's REAL bundled highlights.janet
// against real upstream corpus text, not just the hand-crafted sequences in
// Tests/MatchCacheTest.cpp. Unlike M4, the edit here is applied to known
// text via the exact (position, deletedLength, insertedText) triple, so the
// ChangedSpan is constructed directly rather than diffed -- this test is
// purely about MatchCache's own reconciliation, not diff quality.

namespace {

// The bundled highlights query text for `grammarName` (the same string
// CorpusSource::defaultLanguage/LanguageByName use), or nullopt if this
// grammar has no bundled highlights.janet of its own.
std::optional<std::string> HighlightsQueryTextFor(std::string_view grammarName) {
    for (const ned::editor::LanguageDefinition& definition : ned::editor::BundledLanguages()) {
        const std::string_view grammar =
            definition.grammar.empty() ? std::string_view(definition.name) : std::string_view(definition.grammar);
        if (grammar == grammarName && !definition.queries.highlights.empty()) {
            return ned::editor::CompileQueryFiles(definition.queries.highlights).text;
        }
    }
    return std::nullopt;
}

std::string DescribeMatchCacheMatch(const ned::editor::grammar::QueryMatch& match) {
    std::string out = "{root[" + std::to_string(match.rootStartByte) + "," + std::to_string(match.rootEndByte) + ")";
    for (const auto& capture : match.captures) {
        out += " @" + capture.name + "[" + std::to_string(capture.startByte) + "," + std::to_string(capture.endByte) +
               ")";
    }
    out += "}";
    return out;
}

std::vector<std::string> DescribeMatchCacheMatches(const std::vector<ned::editor::grammar::QueryMatch>& matches) {
    std::vector<std::string> out;
    out.reserve(matches.size());
    for (const auto& match : matches) {
        out.push_back(DescribeMatchCacheMatch(match));
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST_CASE("MatchCache reconciliation matches a fresh full recompute across the bundled corpora's scripted edits",
          "[MatchCache][Corpus]") {
    const std::vector<std::string_view> grammars = {"json", "c", "go",     "java", "clojure", "markdown",
                                                     "yaml", "bash", "python", "cpp",  "rust"};

    std::size_t               totalSteps = 0;
    std::vector<std::string>  failures;
    for (const CorpusSource& source : CorpusSources()) {
        bool included = false;
        for (const std::string_view grammar : grammars)
            included = included || source.defaultLanguage == grammar;
        if (!included)
            continue;

        const std::optional<Language> language = LanguageByName(source.defaultLanguage);
        REQUIRE(language.has_value());
        const std::optional<std::string> highlightsText = HighlightsQueryTextFor(source.defaultLanguage);
        if (!highlightsText.has_value()) {
            continue; // no bundled highlights for this grammar -- nothing to reconcile
        }
        const ned::editor::grammar::QueryMatcher matcher(*language, *highlightsText);

        for (const fs::path& file : CorpusFiles(source)) {
            const std::string content = ReadFile(file);
            const std::string label   = CorpusLabel(source, file);
            for (CorpusCase& item : ParseCorpusFile(content, label)) {
                if (item.skip || !item.platformMatches || !item.languages.front().empty())
                    continue;
                if (item.input.size() < 4)
                    continue;

                std::string                          text = item.input;
                ned::editor::grammar::MatchCache  cache;
                Parser parser(*language);
                {
                    const Tree tree = parser.Parse(text);
                    (void) cache.Reconcile(matcher, tree, text, std::nullopt);
                }

                const std::vector<ScriptedEdit> edits = {
                    {text.size() / 2, 0, "x"},
                    {text.size() / 3, 1, ""},
                    {(text.size() * 2) / 3, 0, "\n"},
                };
                // Diagnostic aid, not part of the assertion: NED_MATCHCACHE_DEBUG=<case name>
                // prints a full per-step trace (divergence detail, whether the divergence is
                // MatchCache's own or already present in a full-width MatchesInRange call) for
                // just that corpus case's edit sequence -- how the two structural gates below
                // (parse errors, external-scanner involvement) and the residual known-failures
                // set were actually found and narrowed.
                const char* debugCaseName = std::getenv("NED_MATCHCACHE_DEBUG");
                const bool  debugThisCase = debugCaseName != nullptr && item.name == debugCaseName;
                int         stepIndex     = 0;
                for (const ScriptedEdit& edit : edits) {
                    ++stepIndex;
                    std::string newText = text;
                    newText.erase(edit.position, edit.deletedLength);
                    newText.insert(edit.position, edit.insertedText);

                    const ned::text::ChangedSpan span{
                        .oldStart = edit.position,
                        .oldEnd   = edit.position + edit.deletedLength,
                        .newStart = edit.position,
                        .newEnd   = edit.position + edit.insertedText.size(),
                    };

                    const Tree newTree    = parser.Parse(newText);
                    const auto reconciled = cache.Reconcile(matcher, newTree, newText, span);
                    const auto fresh      = matcher.Matches(newTree.RootNode(), newText);

                    ++totalSteps;
                    const auto got      = DescribeMatchCacheMatches(reconciled);
                    const auto want     = DescribeMatchCacheMatches(fresh);
                    const bool diverged = got != want;
                    if (diverged) {
                        failures.push_back(std::string(source.directory) + "/" + item.file + ": " + item.name +
                                           " -- MatchCache diverged from fresh recompute");
                    }
                    if (debugThisCase) {
                        std::cerr << "=== DEBUG step " << stepIndex << " (" << (diverged ? "DIVERGED" : "ok")
                                  << ") hasError=" << ned::editor::parse::NodeHasError(newTree.RootNode().Raw())
                                  << " ===\n";
                        std::cerr << "edit: pos=" << edit.position << " del=" << edit.deletedLength << " ins=\""
                                  << edit.insertedText << "\"\n";
                        std::cerr << "text: [" << newText << "]\n";
                        if (diverged) {
                            const auto fullWindowed = DescribeMatchCacheMatches(
                                matcher.MatchesInRange(newTree.RootNode(), newText, 0, newText.size()));
                            std::cerr << "MatchesInRange(0,size) == Matches()? "
                                      << (fullWindowed == want ? "YES" : "NO -- QueryMatcher windowing bug!") << "\n";
                            std::cerr << "-- extra in got (not in want) --\n";
                            for (const auto& g : got)
                                if (std::find(want.begin(), want.end(), g) == want.end())
                                    std::cerr << "  " << g << "\n";
                            std::cerr << "-- missing from got (in want) --\n";
                            for (const auto& w : want)
                                if (std::find(got.begin(), got.end(), w) == got.end())
                                    std::cerr << "  " << w << "\n";
                        }
                    }

                    text = std::move(newText);
                }
            }
        }
    }

    CHECK(totalSteps > 2000);
    INFO("first failures: " << [&] {
        std::string joined;
        for (std::size_t i = 0; i < failures.size() && i < 20; i++)
            joined += "\n  " + failures[i];
        return joined;
    }());
    // Two structural gates were found and are load-bearing: MatchCache always
    // fully re-derives when the tree has a parse error (error recovery is a
    // whole-parse property -- a real bash case flipped an unrelated keyword's
    // classification 50 bytes from the edit) or when any node has external
    // scanner involvement (bash/heredoc-adjacent constructs specifically;
    // took the failure count from 818 to 9 on this exact corpus+edit-script
    // run). A third, narrower mechanism (ambiguity-reclassification
    // follow-up) closed the residual 9: pure grammar-AMBIGUITY reclassification
    // (no error, no external token) near an adversarial scripted edit -- e.g.
    // Go's "Grouped var declarations" flips a bare identifier between
    // @variable and @type depending on whether a token inserted immediately
    // before it reads as a type name. No single structural FLAG on the tree
    // signals this the way HasError/HasExternalTokens do (a candidate,
    // Subtree::dynamicPrecedence, was investigated and disproven -- see
    // MatchCache.h's own header comment), but a direct OLD-vs-NEW comparison
    // does: MatchCache now retains a Tree::Clone() of the tree its previous
    // generation reconciled against (cheap -- see Tree::Clone()'s own doc
    // comment) purely to compare the smallest named node enclosing the
    // edit's old span against the one enclosing its new span, widening the
    // redo window to their union on any type/range mismatch. This can only
    // widen what gets re-derived, never narrow it, so it cannot regress an
    // already-passing case -- confirmed here (0 divergences on this exact
    // corpus+edit-script run, down from 9) and by every other MatchCache/
    // Mode-level incremental-differential test in the suite staying green.
    CHECK(failures.size() == 0);
}

// --- The red layer: node operations against a cursor walk -------------------
//
// Node.cpp (the node.c port) and Cursor.cpp (the tree_cursor.c port) are two
// independent routes to the same facts. A full cursor walk over every corpus
// tree builds an explicit reference tree -- each node's visible children in
// order, with the field id the cursor reports -- and every node-level
// operation QueryMatcher and the editor wrapper consume is then held against
// it: parent, child by index, named children, siblings, child by field id,
// child-with-descendant, and descendant-for-byte-range (the reference applies
// node.c's own descent rule over the explicit children). Positions are held
// against the source text itself: a node's start/end point must be the
// row/column its byte offset lands on.

namespace {

struct RefNode {
    ned::editor::parse::RedNode      node;
    ned::editor::parse::abi::FieldId field;
    std::vector<RefNode>             children;
};

RefNode CollectSubtree(ned::editor::parse::TreeCursor& cursor) {
    RefNode ref{cursor.CurrentNode(), cursor.CurrentFieldId(), {}};
    if (cursor.GotoFirstChild()) {
        do {
            ref.children.push_back(CollectSubtree(cursor));
        }
        while (cursor.GotoNextSibling());
        cursor.GotoParent();
    }
    return ref;
}

bool SameNode(ned::editor::parse::RedNode a, ned::editor::parse::RedNode b) {
    const bool aNull = ned::editor::parse::NodeIsNull(a);
    if (aNull != ned::editor::parse::NodeIsNull(b))
        return false;
    if (aNull)
        return true;
    return ned::editor::parse::NodeEq(a, b) &&
           std::string_view(ned::editor::parse::NodeType(a)) == ned::editor::parse::NodeType(b) &&
           ned::editor::parse::NodeStartByte(a) == ned::editor::parse::NodeStartByte(b) &&
           ned::editor::parse::NodeEndByte(a) == ned::editor::parse::NodeEndByte(b);
}

// The path node.c's descent rule takes through the reference tree's visible
// children: at each level, into the first child whose end reaches the range
// (an empty child may merely touch its start) and whose start does not pass
// it. A zero-width child sitting exactly at the range is ambiguous from the
// cursor's flattened view -- when it is the trailing child of a hidden node
// ending there, node.c skips that hidden node and never sees it -- so
// `skipEmpty` computes the other legitimate answer.
std::vector<const RefNode*> RefDescentPath(const RefNode& root, std::uint32_t rangeStart, std::uint32_t rangeEnd, bool skipEmpty) {
    std::vector<const RefNode*> path{&root};
    bool                        didDescend = true;
    while (didDescend) {
        didDescend = false;
        for (const RefNode& child : path.back()->children) {
            const std::uint32_t childStart = ned::editor::parse::NodeStartByte(child.node);
            const std::uint32_t childEnd   = ned::editor::parse::NodeEndByte(child.node);
            if (childEnd < rangeEnd)
                continue;
            if (childStart == childEnd ? (skipEmpty || childEnd < rangeStart) : childEnd <= rangeStart)
                continue;
            if (rangeStart < childStart)
                break;
            path.push_back(&child);
            didDescend = true;
            break;
        }
    }
    return path;
}

// True when `actual` is what node.c's rule reaches for [mid, mid]: the last
// relevant node on the path -- or, tree-sitter's own quirk, an earlier
// relevant node the descent stopped at because a zero-width HIDDEN token
// (bash's `_concat`, say) sits exactly at `mid`. The cursor never shows such
// a token, but the stop can only happen where the next node on the path
// starts at `mid`, so that is the one place an earlier answer is accepted.
bool DescendantMatches(const std::vector<const RefNode*>& path, std::uint32_t mid, bool namedOnly, ned::editor::parse::RedNode actual) {
    const auto     relevant = [namedOnly](const RefNode* node) { return !namedOnly || ned::editor::parse::NodeIsNamed(node->node); };
    const RefNode* expected = path.front();
    for (const RefNode* node : path)
        if (relevant(node))
            expected = node;
    if (SameNode(actual, expected->node))
        return true;
    const RefNode* lastRelevant = path.front();
    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        if (relevant(path[i]))
            lastRelevant = path[i];
        if (ned::editor::parse::NodeStartByte(path[i + 1]->node) == mid && SameNode(actual, lastRelevant->node))
            return true;
    }
    return false;
}

bool SamePoint(ned::editor::parse::abi::Point a, ned::editor::parse::abi::Point b) {
    return a.row == b.row && a.column == b.column;
}

struct RedLayerTally {
    std::size_t compared     = 0;
    std::size_t fieldLookups = 0;
    std::size_t fieldNulls   = 0;
};

// Checks `ref` (child `index` of `parent`, null for the root) and its
// subtree; `what` names the first mismatch.
bool CheckRefNode(const RefNode& ref, const RefNode* parent, std::size_t index, std::string_view text, RedLayerTally& tally, std::string& what) {
    using namespace ned::editor::parse;
    const RedNode node = ref.node;
    ++tally.compared;

    const std::uint32_t start    = NodeStartByte(node);
    const std::uint32_t end      = NodeEndByte(node);
    const auto          describe = [](RedNode n) {
        return NodeIsNull(n) ? std::string("null")
                             : std::string(NodeType(n)) + "[" + std::to_string(NodeStartByte(n)) + "," + std::to_string(NodeEndByte(n)) + ")";
    };
    const auto fail = [&](const std::string& op, RedNode actual, RedNode expected) {
        what = op + " of " + describe(node) + ": ned " + describe(actual) + ", cursor " + describe(expected);
        return false;
    };
    const auto check = [&](const char* op, RedNode actual, RedNode expected) {
        return SameNode(actual, expected) || fail(op, actual, expected);
    };

    if (!SamePoint(NodeStartPoint(node), PointAtOffset(text, start)) || !SamePoint(NodeEndPoint(node), PointAtOffset(text, end)))
        return fail("point vs text", node, node);
    if (!check("parent", NodeParent(node), parent != nullptr ? parent->node : NodeNull()))
        return false;
    if (parent != nullptr) {
        const std::vector<RefNode>& siblings = parent->children;
        // node.c's next-sibling search skips every later child ending at or
        // before this node's end, so a zero-width sibling sitting exactly at
        // this node's end is passed over; modelled exactly.
        const auto nextAfter = [&](bool namedOnly) {
            for (std::size_t j = index + 1; j < siblings.size(); ++j)
                if (NodeEndByte(siblings[j].node) > end && (!namedOnly || NodeIsNamed(siblings[j].node)))
                    return siblings[j].node;
            return NodeNull();
        };
        if (!check("next sibling", NodeNextSibling(node), nextAfter(false)))
            return false;
        if (!check("next named sibling", NodeNextNamedSibling(node), nextAfter(true)))
            return false;
        // The previous-sibling search is a byte-position heuristic that can
        // give up (null) when this node itself is zero-width; exact otherwise.
        const auto prevBefore = [&](bool namedOnly) {
            for (std::size_t j = index; j-- > 0;)
                if (!namedOnly || NodeIsNamed(siblings[j].node))
                    return siblings[j].node;
            return NodeNull();
        };
        const auto checkPrev = [&](const char* op, RedNode actual, RedNode expected) {
            return (start == end && NodeIsNull(actual)) || check(op, actual, expected);
        };
        if (!checkPrev("prev sibling", NodePrevSibling(node), prevBefore(false)))
            return false;
        if (!checkPrev("prev named sibling", NodePrevNamedSibling(node), prevBefore(true)))
            return false;
    }

    if (NodeChildCount(node) != ref.children.size())
        return fail("child count " + std::to_string(NodeChildCount(node)) + " vs " + std::to_string(ref.children.size()), node, node);
    std::uint32_t named = 0;
    for (std::size_t i = 0; i < ref.children.size(); ++i) {
        const RefNode& child = ref.children[i];
        if (!check("child by index", NodeChild(node, static_cast<std::uint32_t>(i)), child.node))
            return false;
        if (NodeIsNamed(child.node)) {
            if (!check("named child by index", NodeNamedChild(node, named), child.node))
                return false;
            ++named;
        }
        if (!check("child with descendant (child)", NodeChildWithDescendant(node, child.node), child.node))
            return false;
        for (const RefNode& grandchild : child.children)
            if (!check("child with descendant (grandchild)", NodeChildWithDescendant(node, grandchild.node), child.node))
                return false;
    }
    if (NodeNamedChildCount(node) != named)
        return fail("named child count", node, node);
    for (std::size_t i = 0; i < ref.children.size(); ++i) {
        const abi::FieldId field = ref.children[i].field;
        if (field == 0)
            continue;
        bool first = true;
        for (std::size_t j = 0; j < i; ++j)
            first = first && ref.children[j].field != field;
        if (!first)
            continue;
        // A field declared inside a hidden wrapper (a supertype or repeat
        // node, lua's `statement`) is reported by the cursor but is not
        // reachable through the parent's inherited field map, so node.c
        // answers null there; anything non-null must be the cursor's child.
        const RedNode actual = NodeChildByFieldId(node, field);
        ++tally.fieldLookups;
        if (NodeIsNull(actual))
            ++tally.fieldNulls;
        else if (!check(("child by field id " + std::to_string(field)).c_str(), actual, ref.children[i].node))
            return false;
    }

    for (std::size_t i = 0; i < ref.children.size(); ++i)
        if (!CheckRefNode(ref.children[i], &ref, i, text, tally, what))
            return false;

    const std::uint32_t               mid             = start + (end - start) / 2;
    const std::vector<const RefNode*> path            = RefDescentPath(ref, mid, mid, false);
    const std::vector<const RefNode*> pathNoEmpties   = RefDescentPath(ref, mid, mid, true);
    const auto                        checkDescendant = [&](const char* op, bool namedOnly, RedNode actual) {
        if (DescendantMatches(path, mid, namedOnly, actual) || DescendantMatches(pathNoEmpties, mid, namedOnly, actual))
            return true;
        std::string pathText;
        for (const RefNode* step : path)
            pathText += " " + describe(step->node);
        what = std::string(op) + " at mid " + std::to_string(mid) + ": ned " + describe(actual) + ", path" + pathText;
        return false;
    };
    if (!checkDescendant("descendant for byte range", false, NodeDescendantForByteRange(node, mid, mid)))
        return false;
    if (!checkDescendant("named descendant for byte range", true, NodeNamedDescendantForByteRange(node, mid, mid)))
        return false;
    return true;
}

} // namespace

TEST_CASE("Ned red layer: node operations agree with a cursor walk over the bundled corpora", "[ParseEngineRedLayer][Corpus]") {
    RedLayerTally            tally;
    std::vector<std::string> failures;

    for (const CorpusSource& source : CorpusSources()) {
        const std::optional<Language> language = LanguageByName(source.defaultLanguage);
        REQUIRE(language.has_value());
        ned::editor::parse::Engine engine(language->Raw());
        const RedLayerTally        before = tally;

        for (const fs::path& file : CorpusFiles(source)) {
            const std::string content = ReadFile(file);
            const std::string label   = CorpusLabel(source, file);
            for (CorpusCase& item : ParseCorpusFile(content, label)) {
                if (item.skip || !item.platformMatches || !item.languages.front().empty())
                    continue;

                const ned::editor::parse::GreenTree tree = engine.Parse(item.input);
                REQUIRE_FALSE(tree.IsNull());
                ned::editor::parse::TreeCursor cursor(tree.RootNode());
                const RefNode                  root = CollectSubtree(cursor);

                std::string what;
                if (!CheckRefNode(root, nullptr, 0, item.input, tally, what) && failures.size() < 50)
                    failures.push_back(std::string(source.directory) + "/" + item.file + ": " + item.name + " -- " + what);
            }
        }
        if (std::getenv("NED_PARSE_CONFORMANCE_VERBOSE") != nullptr)
            std::cerr << source.defaultLanguage << ": " << (tally.fieldLookups - before.fieldLookups) << " field lookups, " << (tally.fieldNulls - before.fieldNulls)
                      << " null\n";
    }

    CHECK(tally.compared > 90000);
    // The null tolerance above must stay the rare exception, or a lookup that
    // always answered null would pass.
    INFO("field lookups: " << tally.fieldLookups << ", null: " << tally.fieldNulls);
    CHECK(tally.fieldLookups > 5000);
    CHECK(tally.fieldNulls * 100 < tally.fieldLookups);
    INFO("first failures: " << [&] {
        std::string joined;
        for (std::size_t i = 0; i < failures.size() && i < 20; i++)
            joined += "\n  " + failures[i];
        return joined;
    }());
    CHECK(failures.size() == 0);
}

// --- M8: stable node identity across an incremental reparse -----------------
//
// The engine-swap payoff Node::Id()'s byte-range-collision workaround was
// waiting for: a reused green subtree IS the same heap object after an
// incremental reparse, so an Id captured before an edit elsewhere in the
// document still identifies the same node afterwards. (The node's own SLOT
// address — what Id() returns — lives in its parent's child array, so the
// contract covers any node whose PARENT subtree was reused; the edit path
// itself is cloned.)

TEST_CASE("Ned parse engine keeps reused subtree identity across an incremental reparse", "[ParseEngine]") {
    const std::optional<Language> language = LanguageByName("c");
    REQUIRE(language.has_value());
    ned::editor::parse::Engine engine(language->Raw());

    const std::string                   before = "int first(void) { return 1; }\n\nint second(void) { return 2; }\n";
    const ned::editor::parse::GreenTree tree   = engine.Parse(before);

    // The body of `second` — a node whose parent (the function_definition)
    // lies wholly outside the edit below.
    const ned::editor::parse::RedNode root = tree.RootNode();
    const ned::editor::parse::RedNode secondBody =
        ned::editor::parse::NodeNamedDescendantForByteRange(root, before.find("return 2"), before.find("return 2") + 8);
    REQUIRE_FALSE(ned::editor::parse::NodeIsNull(secondBody));

    // Edit inside `first`'s body only.
    std::string       after  = before;
    const std::size_t editAt = after.find("return 1");
    after.insert(editAt, "int x = 0; ");
    const ned::editor::parse::InputEdit edit = {
        .startByte   = static_cast<std::uint32_t>(editAt),
        .oldEndByte  = static_cast<std::uint32_t>(editAt),
        .newEndByte  = static_cast<std::uint32_t>(editAt + 11),
        .startPoint  = {0, static_cast<std::uint32_t>(editAt - before.rfind('\n', editAt) - 1)},
        .oldEndPoint = {0, static_cast<std::uint32_t>(editAt - before.rfind('\n', editAt) - 1)},
        .newEndPoint = {0, static_cast<std::uint32_t>(editAt + 11 - before.rfind('\n', editAt) - 1)},
    };
    const ned::editor::parse::GreenTree edited   = tree.WithEdit(edit);
    const ned::editor::parse::GreenTree reparsed = engine.Parse(after, edited);

    const ned::editor::parse::RedNode newSecondBody = ned::editor::parse::NodeNamedDescendantForByteRange(
        reparsed.RootNode(), after.find("return 2"), after.find("return 2") + 8);
    REQUIRE_FALSE(ned::editor::parse::NodeIsNull(newSecondBody));

    // Same green slot: the identity survived the reparse.
    CHECK(newSecondBody.id == secondBody.id);

    // And the edited function's body did NOT keep its identity (its subtree
    // was genuinely reparsed), so the check above isn't vacuous.
    const ned::editor::parse::RedNode oldFirstBody =
        ned::editor::parse::NodeNamedDescendantForByteRange(root, before.find("return 1"), before.find("return 1") + 8);
    const ned::editor::parse::RedNode newFirstBody = ned::editor::parse::NodeNamedDescendantForByteRange(
        reparsed.RootNode(), after.find("return 1"), after.find("return 1") + 8);
    CHECK(newFirstBody.id != oldFirstBody.id);
}

// --- M11: the recovery corpus -----------------------------------------------
//
// Upstream's corpora barely pin error recovery (~25 cases in ~2,800), so
// this is ned's own: realistic mid-edit states — unclosed braces, half-typed
// calls, unterminated strings — with the engine's full recovery tree held as
// a golden per case. What makes recovery behavior a conscious diff instead
// of drift, and the baseline any future recovery improvement (missing-token
// generalization, anchor sets) is measured against.
//
// Regenerate after an intentional change:
//
//     NED_BLESS_PARSE_RECOVERY=1 ./build/ned_tests "[ParseRecovery]"

TEST_CASE("Recovery corpus trees match their blessed goldens", "[ParseRecovery]") {
    const fs::path corpusDir   = fs::path(NED_REPO_ROOT) / "Tests" / "ParseRecovery" / "corpus";
    const fs::path expectedDir = fs::path(NED_REPO_ROOT) / "Tests" / "ParseRecovery" / "expected";
    REQUIRE(fs::exists(corpusDir));

    const std::map<std::string, std::string, std::less<>> languageByExtension = {
        {".c", "c"},
        {".cpp", "cpp"},
        {".py", "python"},
        {".js", "javascript"},
        {".rs", "rust"},
        {".json", "json"},
        {".sh", "bash"},
        {".md", "markdown"},
    };

    const bool blessing = std::getenv("NED_BLESS_PARSE_RECOVERY") != nullptr;
    if (blessing)
        fs::create_directories(expectedDir);

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(corpusDir))
        if (entry.is_regular_file())
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    REQUIRE(files.size() >= 14);

    std::map<std::string, std::unique_ptr<ned::editor::parse::Engine>, std::less<>> engines;
    for (const fs::path& file : files) {
        const auto mapped = languageByExtension.find(file.extension().string());
        REQUIRE(mapped != languageByExtension.end());
        auto engineIt = engines.find(mapped->second);
        if (engineIt == engines.end()) {
            const std::optional<Language> language = LanguageByName(mapped->second);
            REQUIRE(language.has_value());
            engineIt =
                engines.emplace(mapped->second, std::make_unique<ned::editor::parse::Engine>(language->Raw())).first;
        }

        const std::string                   source = ReadFile(file);
        const ned::editor::parse::GreenTree tree   = engineIt->second->Parse(source);
        REQUIRE_FALSE(tree.IsNull());
        // Every case here is broken as EDITING state; almost all are also
        // grammar errors. The exception is deliberate: CommonMark defines an
        // unclosed fence as running to the end of the document, so
        // unclosed-fence.md parses clean -- the golden pins that the fence
        // swallows the trailing paragraph, which is exactly the surprising
        // behavior worth pinning.
        INFO(file.filename().string());
        if (file.filename() != "unclosed-fence.md")
            CHECK(tree.HasError());

        const std::string rendered = ned::editor::parse::SubtreeToSexp(tree.Root(), tree.Language()) + "\n";
        const fs::path    golden   = expectedDir / (file.filename().string() + ".sexp");
        if (blessing) {
            std::ofstream out(golden, std::ios::binary | std::ios::trunc);
            REQUIRE(out);
            out << rendered;
            continue;
        }
        INFO("golden: " << golden.string() << "  (regenerate: NED_BLESS_PARSE_RECOVERY=1 ./build/ned_tests \"[ParseRecovery]\")");
        REQUIRE(fs::exists(golden));
        CHECK(ReadFile(golden) == rendered);
    }
    if (blessing)
        SUCCEED("blessed " + expectedDir.string());
}

// Recovery-quality properties the goldens above also imply, stated
// directly so they read as contracts rather than incidental tree shapes:
// an error early in a file must stay CONTAINED — the structures after it
// still parse as themselves.
TEST_CASE("Recovery keeps later structures parsed past an early error", "[ParseRecovery]") {
    struct Expectation {
        std::string_view language;
        std::string_view file;
        std::string_view laterStructure; // node type that must still appear
    };
    const std::vector<Expectation> expectations = {
        {"c", "unclosed-function-brace.c", "function_definition"},
        {"python", "half-typed-def.py", "function_definition"},
        {"javascript", "unclosed-block.js", "function_declaration"},
        {"rust", "missing-brace.rs", "function_item"},
        {"bash", "unclosed-if.sh", "command"},
    };
    for (const Expectation& expectation : expectations) {
        const std::optional<Language> language = LanguageByName(expectation.language);
        REQUIRE(language.has_value());
        ned::editor::parse::Engine engine(language->Raw());
        const std::string          source =
            ReadFile(fs::path(NED_REPO_ROOT) / "Tests" / "ParseRecovery" / "corpus" / expectation.file);
        const ned::editor::parse::GreenTree tree = engine.Parse(source);
        const std::string                   sexp = ned::editor::parse::SubtreeToSexp(tree.Root(), tree.Language());
        INFO(expectation.file << ": " << sexp);
        CHECK(tree.HasError());
        // The structure defined AFTER the broken region still parses.
        const std::size_t lastOccurrence = sexp.rfind(expectation.laterStructure);
        CHECK(lastOccurrence != std::string::npos);
        CHECK(lastOccurrence > sexp.size() / 3);
    }
}
