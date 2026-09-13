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
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <tree_sitter/api.h>

#include "Editor/Parse/Cursor.h"
#include "Editor/Parse/Node.h"
#include "Editor/Parse/Parser.h"
#include "Editor/Parse/Sexp.h"
#include "Editor/TreeSitter/Languages.h"
#include "Editor/TreeSitter/Parser.h"
#include "Editor/TreeSitter/Tree.h"

// Phase 4b M0: the conformance bar for the parsing-engine replacement.
//
// Runs every upstream grammar's own test corpus (build/_deps/*/test/corpus)
// against the production parser and holds the per-corpus scorecard as a
// golden file. Today the production parser is the tree-sitter runtime, so
// this pins the baseline the ned engine must meet; at M5 the same harness
// holds the swapped engine to byte-identical results.
//
// The corpus format and comparison rules are a faithful port of upstream's
// own runner (build/_deps/tree-sitter-src/cli/src/test.rs):
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
using ned::editor::treesitter::Language;
using ned::editor::treesitter::LanguageByName;
using ned::editor::treesitter::Parser;
using ned::editor::treesitter::Tree;

fs::path DepsDir() {
    return fs::path(NED_REPO_ROOT) / "build" / "_deps";
}

fs::path GoldenPath() {
    return fs::path(NED_REPO_ROOT) / "Tests" / "ParseConformance" / "baseline.txt";
}

bool Blessing() {
    return std::getenv("NED_BLESS_PARSE_CONFORMANCE") != nullptr;
}

struct DialectMapping {
    std::string_view attribute; // the :language(...) value in the corpus
    std::string_view language;  // ned LanguageByName key; empty = grammar not linked, skip
};

struct CorpusSource {
    std::string_view            directory;       // relative to DepsDir()
    std::string_view            defaultLanguage; // for cases with no :language attribute
    std::vector<DialectMapping> dialects{};
};

// One entry per corpus directory on disk. The markdown repo carries two
// grammars (block and inline) with separate corpora; it is fetched twice
// (markdown / markdown-inline FetchContent names) but both clones are the
// same repo, so only one clone's two corpora are listed.
const std::vector<CorpusSource>& CorpusSources() {
    static const std::vector<CorpusSource> sources = {
        {"tree-sitter-bash-src/test/corpus", "bash"},
        {"tree-sitter-c-src/test/corpus", "c"},
        {"tree-sitter-c-sharp-src/test/corpus", "csharp"},
        {"tree-sitter-clojure-src/test/corpus", "clojure"},
        {"tree-sitter-cpp-src/test/corpus", "cpp"},
        {"tree-sitter-css-src/test/corpus", "css"},
        {"tree-sitter-fish-src/test/corpus", "fish"},
        {"tree-sitter-go-src/test/corpus", "go"},
        {"tree-sitter-html-src/test/corpus", "html"},
        {"tree-sitter-janet-simple-src/test/corpus", "janet"},
        {"tree-sitter-java-src/test/corpus", "java"},
        {"tree-sitter-javascript-src/test/corpus", "javascript"},
        {"tree-sitter-json-src/test/corpus", "json"},
        {"tree-sitter-kotlin-src/test/corpus", "kotlin"},
        {"tree-sitter-markdown-src/tree-sitter-markdown/test/corpus", "markdown"},
        {"tree-sitter-markdown-src/tree-sitter-markdown-inline/test/corpus", "markdown-inline"},
        {"tree-sitter-org-src/test/corpus", "org"},
        {"tree-sitter-php-src/test/corpus", "php", {{"php", "php"}, {"php_only", ""}}},
        {"tree-sitter-python-src/test/corpus", "python"},
        {"tree-sitter-rust-src/test/corpus", "rust"},
        {"tree-sitter-toml-src/test/corpus", "toml"},
        {"tree-sitter-typescript-src-src/test/corpus", "typescript", {{"typescript", "typescript"}, {"tsx", "tsx"}}},
        {"tree-sitter-xml-src/test/corpus", "xml", {{"xml", "xml"}, {"dtd", ""}}},
        {"tree-sitter-yaml-src/test/corpus", "yaml"},
    };
    return sources;
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

// Raw tree-sitter runtime handles: the C engine stays linked purely as the
// differential reference for the engine gates (dropped at M6).
struct TsParserHandle {
    TSParser* parser;
    explicit TsParserHandle(const TSLanguage* language) : parser(ts_parser_new()) {
        REQUIRE(ts_parser_set_language(parser, language));
    }
    ~TsParserHandle() {
        ts_parser_delete(parser);
    }
    TsParserHandle(const TsParserHandle&)            = delete;
    TsParserHandle& operator=(const TsParserHandle&) = delete;
};

struct TsTreeHandle {
    TSTree* tree   = nullptr;
    TsTreeHandle() = default;
    explicit TsTreeHandle(TSTree* t) : tree(t) {
    }
    ~TsTreeHandle() {
        if (tree != nullptr)
            ts_tree_delete(tree);
    }
    TsTreeHandle(TsTreeHandle&& other) noexcept : tree(other.tree) {
        other.tree = nullptr;
    }
    TsTreeHandle& operator=(TsTreeHandle&& other) noexcept {
        if (this != &other) {
            if (tree != nullptr)
                ts_tree_delete(tree);
            tree       = other.tree;
            other.tree = nullptr;
        }
        return *this;
    }
    TsTreeHandle(const TsTreeHandle&)            = delete;
    TsTreeHandle& operator=(const TsTreeHandle&) = delete;
};

TsTreeHandle TsParse(TsParserHandle& parser, const std::string& text, const TSTree* oldTree = nullptr) {
    return TsTreeHandle(
        ts_parser_parse_string(parser.parser, oldTree, text.data(), static_cast<std::uint32_t>(text.size())));
}

std::string TsSexpOf(const TsTreeHandle& tree, bool keepFields) {
    char*       raw = ts_node_string(ts_tree_root_node(tree.tree));
    std::string sexp(raw);
    std::free(raw);
    if (!keepFields)
        sexp = StripSexpFields(sexp);
    return sexp;
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

TEST_CASE("Upstream corpora conformance scorecard matches the blessed baseline", "[ParseConformance][Corpus]") {
    if (!fs::exists(DepsDir())) {
        SUCCEED("no build/_deps in this checkout -- grammars are FetchContent'd");
        return;
    }
    for (const CorpusSource& source : CorpusSources()) {
        if (!fs::exists(DepsDir() / source.directory)) {
            WARN("missing corpus directory: " << source.directory << " -- skipping the scorecard entirely so the golden stays comparable");
            SUCCEED();
            return;
        }
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
    rendered << "# Parse conformance scorecard: upstream grammar corpora vs the production engine.\n"
             << "# Regenerate: NED_BLESS_PARSE_CONFORMANCE=1 ./build/ned_tests \"[ParseConformance]\"\n";

    CorpusScore total;
    for (const CorpusSource& source : CorpusSources()) {
        const fs::path        corpusDir = DepsDir() / source.directory;
        std::vector<fs::path> files;
        for (const auto& entry : fs::recursive_directory_iterator(corpusDir))
            if (entry.is_regular_file() && entry.path().extension() == ".txt")
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());

        CorpusScore score;
        for (const fs::path& file : files) {
            const std::string content = ReadFile(file);
            const std::string label   = fs::relative(file, corpusDir).string();
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

    const fs::path golden = GoldenPath();
    if (Blessing()) {
        fs::create_directories(golden.parent_path());
        std::ofstream out(golden, std::ios::binary | std::ios::trunc);
        REQUIRE(out);
        out << rendered.str();
        SUCCEED("blessed " + golden.string());
        return;
    }
    INFO("golden: " << golden.string() << "  (regenerate: NED_BLESS_PARSE_CONFORMANCE=1 ./build/ned_tests \"[ParseConformance]\")");
    REQUIRE(fs::exists(golden));
    CHECK(ReadFile(golden) == rendered.str());
}

// --- M1: the ned engine against the scanner-free grammars' corpora ----------
//
// The go/no-go gate on ABI-15 table interpretation: json, c, go, java and
// clojure carry no external scanner, so every corpus case exercises only the
// generated lexer + parse tables + ned's runtime. Expected-sexp comparison
// uses exactly the scorecard's rules; a differential against the tree-sitter
// runtime's own rendering backs it up for the :error cases the corpus
// doesn't pin structurally.

TEST_CASE("Ned parse engine matches upstream corpora", "[ParseEngine][Corpus]") {
    if (!fs::exists(DepsDir())) {
        SUCCEED("no build/_deps in this checkout -- grammars are FetchContent'd");
        return;
    }

    // The markdown-inline grammar cannot parse the extension cases the
    // shipped artifacts weren't generated with; the M0 baseline records the
    // same 8 failures for the tree-sitter runtime.
    const auto isKnownBaselineFailure = [](std::string_view corpus, std::string_view file) {
        return corpus == "tree-sitter-markdown-src/tree-sitter-markdown-inline/test/corpus" &&
               (file == "extension_wikilink.txt" || file == "tags.txt" || file == "spec.txt");
    };

    std::size_t                                                totalRuns = 0;
    std::vector<std::string>                                   failures;
    std::map<std::string, std::pair<std::size_t, std::size_t>> perCorpus; // {failed, run}
    for (const CorpusSource& source : CorpusSources()) {
        const fs::path corpusDir = DepsDir() / source.directory;
        if (!fs::exists(corpusDir))
            continue;

        std::map<std::string, std::unique_ptr<ned::editor::parse::Engine>, std::less<>> engines;
        std::map<std::string, std::unique_ptr<TsParserHandle>, std::less<>>             tsParsers;
        const auto                                                                      engineFor = [&](std::string_view languageName) -> ned::editor::parse::Engine* {
            auto found = engines.find(languageName);
            if (found != engines.end())
                return found->second.get();
            const std::optional<Language> language = LanguageByName(languageName);
            REQUIRE(language.has_value());
            tsParsers.emplace(std::string(languageName),
                              std::make_unique<TsParserHandle>(reinterpret_cast<const TSLanguage*>(language->Raw())));
            return engines.emplace(std::string(languageName), std::make_unique<ned::editor::parse::Engine>(language->Raw()))
                .first->second.get();
        };

        std::vector<fs::path> files;
        for (const auto& entry : fs::recursive_directory_iterator(corpusDir))
            if (entry.is_regular_file() && entry.path().extension() == ".txt")
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());

        for (const fs::path& file : files) {
            const std::string content = ReadFile(file);
            const std::string label   = fs::relative(file, corpusDir).string();
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
                        // A case the shipped artifacts can't pass must still
                        // yield the same tree tree-sitter produces from those
                        // artifacts — both engines run the same tables.
                        if (isKnownBaselineFailure(source.directory, item.file)) {
                            const TsTreeHandle tsTree   = TsParse(*tsParsers.find(languageName)->second, item.input);
                            std::string        tsActual = TsSexpOf(tsTree, item.hasFields);
                            if (actual != tsActual)
                                failures.push_back(std::string(source.directory) + "/" + item.file + ": " + item.name +
                                                   " -- diverges from the ts runtime");
                            ++perCorpus[std::string(source.directory)].first;
                            continue;
                        }
                        recordFailure("");
                        if (std::getenv("NED_PARSE_CONFORMANCE_VERBOSE") != nullptr) {
                            const TsTreeHandle tsTree = TsParse(*tsParsers.find(languageName)->second, item.input);
                            std::cerr << "case:     " << source.directory << "/" << item.file << ": " << item.name << "\n"
                                      << "expected: " << item.expected << "\n"
                                      << "ned:      " << actual << "\n"
                                      << "ts:       " << TsSexpOf(tsTree, item.hasFields) << "\n\n";
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

// --- M4: incremental reparse ------------------------------------------------
//
// For every corpus input of a representative grammar set (the scanner-free
// five plus the scanner-heavy proving load), apply a chain of scripted edits;
// after each step the incremental reparse must equal a from-scratch parse of
// the same text, and equal the tree-sitter runtime's own incremental result
// over the identical edit sequence.

namespace {

TSPoint PointAtOffset(std::string_view text, std::size_t offset) {
    TSPoint point = {0, 0};
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

TSInputEdit MakeTsEdit(std::string_view oldText, std::string_view newText, const ScriptedEdit& edit) {
    TSInputEdit result;
    result.start_byte    = static_cast<std::uint32_t>(edit.position);
    result.old_end_byte  = static_cast<std::uint32_t>(edit.position + edit.deletedLength);
    result.new_end_byte  = static_cast<std::uint32_t>(edit.position + edit.insertedText.size());
    result.start_point   = PointAtOffset(oldText, edit.position);
    result.old_end_point = PointAtOffset(oldText, edit.position + edit.deletedLength);
    result.new_end_point = PointAtOffset(newText, edit.position + edit.insertedText.size());
    return result;
}

} // namespace

TEST_CASE("Ned parse engine incremental reparses match from-scratch and the ts runtime", "[ParseEngineIncremental][Corpus]") {
    if (!fs::exists(DepsDir())) {
        SUCCEED("no build/_deps in this checkout -- grammars are FetchContent'd");
        return;
    }

    const std::vector<std::string_view> grammars = {"json", "c", "go", "java", "clojure", "markdown",
                                                    "yaml", "bash", "python", "cpp", "rust", "markdown-inline"};

    std::size_t              totalSteps         = 0;
    std::size_t              scratchDivergences = 0;
    std::vector<std::string> failures;
    for (const CorpusSource& source : CorpusSources()) {
        bool included = false;
        for (const std::string_view grammar : grammars)
            included = included || source.defaultLanguage == grammar;
        if (!included)
            continue;

        const fs::path corpusDir = DepsDir() / source.directory;
        if (!fs::exists(corpusDir))
            continue;

        const std::optional<Language> language = LanguageByName(source.defaultLanguage);
        REQUIRE(language.has_value());
        ned::editor::parse::Engine engine(language->Raw());
        TsParserHandle             tsParser(reinterpret_cast<const TSLanguage*>(language->Raw()));

        std::vector<fs::path> files;
        for (const auto& entry : fs::recursive_directory_iterator(corpusDir))
            if (entry.is_regular_file() && entry.path().extension() == ".txt")
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());

        for (const fs::path& file : files) {
            const std::string content = ReadFile(file);
            const std::string label   = fs::relative(file, corpusDir).string();
            for (CorpusCase& item : ParseCorpusFile(content, label)) {
                if (item.skip || !item.platformMatches || !item.languages.front().empty())
                    continue;
                if (item.input.size() < 4)
                    continue;

                std::string                   text    = item.input;
                ned::editor::parse::GreenTree nedTree = engine.Parse(text);
                TsTreeHandle                  tsTree  = TsParse(tsParser, text);

                const std::vector<ScriptedEdit> edits = {
                    {text.size() / 2, 0, "x"},
                    {text.size() / 3, 1, ""},
                    {(text.size() * 2) / 3, 0, "\n"},
                };
                for (const ScriptedEdit& edit : edits) {
                    std::string newText = text;
                    newText.erase(edit.position, edit.deletedLength);
                    newText.insert(edit.position, edit.insertedText);
                    const TSInputEdit tsEdit = MakeTsEdit(text, newText, edit);

                    const ned::editor::parse::InputEdit nedEdit = {
                        .startByte   = tsEdit.start_byte,
                        .oldEndByte  = tsEdit.old_end_byte,
                        .newEndByte  = tsEdit.new_end_byte,
                        .startPoint  = {tsEdit.start_point.row, tsEdit.start_point.column},
                        .oldEndPoint = {tsEdit.old_end_point.row, tsEdit.old_end_point.column},
                        .newEndPoint = {tsEdit.new_end_point.row, tsEdit.new_end_point.column},
                    };

                    const ned::editor::parse::GreenTree edited      = nedTree.WithEdit(nedEdit);
                    ned::editor::parse::GreenTree       incremental = engine.Parse(newText, edited);
                    const ned::editor::parse::GreenTree scratch     = engine.Parse(newText);

                    ts_tree_edit(tsTree.tree, &tsEdit);
                    TsTreeHandle tsIncremental = TsParse(tsParser, newText, tsTree.tree);

                    const std::string incrementalSexp =
                        ned::editor::parse::SubtreeToSexp(incremental.Root(), incremental.Language());
                    const std::string scratchSexp =
                        ned::editor::parse::SubtreeToSexp(scratch.Root(), scratch.Language());
                    const std::string tsSexp = TsSexpOf(tsIncremental, true);

                    ++totalSteps;
                    // The hard invariant is agreement with the ts runtime's
                    // incremental result over the identical edit sequence. A
                    // handful of steps legitimately differ from a scratch
                    // parse — upstream's own incremental parser produces a
                    // different (equally valid) tree there, and ned inherits
                    // that behavior; the count is pinned below.
                    if (incrementalSexp != tsSexp)
                        failures.push_back(std::string(source.directory) + "/" + item.file + ": " + item.name +
                                           " -- incremental != ts");
                    else if (incrementalSexp != scratchSexp)
                        ++scratchDivergences;

                    text    = std::move(newText);
                    nedTree = std::move(incremental);
                    tsTree  = std::move(tsIncremental);
                }
            }
        }
    }

    CHECK(totalSteps > 4000);
    CHECK(scratchDivergences == 7); // both engines' incremental trees, verified identical, vs scratch

    INFO("first failures: " << [&] {
        std::string joined;
        for (std::size_t i = 0; i < failures.size() && i < 20; i++)
            joined += "\n  " + failures[i];
        return joined;
    }());
    CHECK(failures.size() == 0);
}

// --- M5 prerequisite: the red layer (Node/Cursor) against the ts runtime ----
//
// Dual preorder walks over every corpus tree, ned cursor beside TSTreeCursor,
// comparing each visited node's identity facts and the node-level operations
// QueryMatcher and the editor wrapper consume (parent, named siblings, field
// id, descendant-for-byte-range).

TEST_CASE("Ned red layer matches the ts runtime over upstream corpora", "[ParseEngineRedLayer][Corpus]") {
    if (!fs::exists(DepsDir())) {
        SUCCEED("no build/_deps in this checkout -- grammars are FetchContent'd");
        return;
    }

    std::size_t              comparedNodes = 0;
    std::vector<std::string> failures;
    const auto               fail = [&failures](const CorpusSource& source, const CorpusCase& item, const std::string& what) {
        if (failures.size() < 50)
            failures.push_back(std::string(source.directory) + "/" + item.file + ": " + item.name + " -- " + what);
    };

    for (const CorpusSource& source : CorpusSources()) {
        const fs::path corpusDir = DepsDir() / source.directory;
        if (!fs::exists(corpusDir))
            continue;

        const std::optional<Language> language = LanguageByName(source.defaultLanguage);
        REQUIRE(language.has_value());
        ned::editor::parse::Engine engine(language->Raw());
        TsParserHandle             tsParser(reinterpret_cast<const TSLanguage*>(language->Raw()));

        std::vector<fs::path> files;
        for (const auto& entry : fs::recursive_directory_iterator(corpusDir))
            if (entry.is_regular_file() && entry.path().extension() == ".txt")
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());

        for (const fs::path& file : files) {
            const std::string content = ReadFile(file);
            const std::string label   = fs::relative(file, corpusDir).string();
            for (CorpusCase& item : ParseCorpusFile(content, label)) {
                if (item.skip || !item.platformMatches || !item.languages.front().empty())
                    continue;

                const ned::editor::parse::GreenTree nedTree = engine.Parse(item.input);
                const TsTreeHandle                  tsTree  = TsParse(tsParser, item.input);
                REQUIRE_FALSE(nedTree.IsNull());

                const ned::editor::parse::RedNode nedRoot = nedTree.RootNode();
                const TSNode                      tsRoot  = ts_tree_root_node(tsTree.tree);

                ned::editor::parse::TreeCursor nedCursor(nedRoot);
                TSTreeCursor                   tsCursor = ts_tree_cursor_new(tsRoot);

                bool mismatch = false;
                for (;;) {
                    const ned::editor::parse::RedNode nedNode = nedCursor.CurrentNode();
                    const TSNode                      tsNode  = ts_tree_cursor_current_node(&tsCursor);

                    ++comparedNodes;
                    const bool same =
                        std::string_view(ned::editor::parse::NodeType(nedNode)) == ts_node_type(tsNode) &&
                        ned::editor::parse::NodeStartByte(nedNode) == ts_node_start_byte(tsNode) &&
                        ned::editor::parse::NodeEndByte(nedNode) == ts_node_end_byte(tsNode) &&
                        ned::editor::parse::NodeSymbol(nedNode) == ts_node_symbol(tsNode) &&
                        ned::editor::parse::NodeIsNamed(nedNode) == ts_node_is_named(tsNode) &&
                        ned::editor::parse::NodeIsExtra(nedNode) == ts_node_is_extra(tsNode) &&
                        ned::editor::parse::NodeIsMissing(nedNode) == ts_node_is_missing(tsNode) &&
                        nedCursor.CurrentFieldId() == ts_tree_cursor_current_field_id(&tsCursor) &&
                        ned::editor::parse::NodeStartPoint(nedNode).row == ts_node_start_point(tsNode).row &&
                        ned::editor::parse::NodeStartPoint(nedNode).column == ts_node_start_point(tsNode).column;
                    if (!same) {
                        fail(source, item, std::string("node mismatch at byte ") + std::to_string(ts_node_start_byte(tsNode)) + " (" + ts_node_type(tsNode) + " vs " + ned::editor::parse::NodeType(nedNode) + ")");
                        mismatch = true;
                        break;
                    }

                    // Node-level operations, compared by identity facts.
                    const auto sameNode = [](ned::editor::parse::RedNode a, TSNode b) {
                        const bool aNull = ned::editor::parse::NodeIsNull(a);
                        if (aNull != ts_node_is_null(b))
                            return false;
                        if (aNull)
                            return true;
                        return std::string_view(ned::editor::parse::NodeType(a)) == ts_node_type(b) &&
                               ned::editor::parse::NodeStartByte(a) == ts_node_start_byte(b) &&
                               ned::editor::parse::NodeEndByte(a) == ts_node_end_byte(b);
                    };

                    if (!sameNode(ned::editor::parse::NodeParent(nedNode), ts_node_parent(tsNode))) {
                        fail(source, item, "parent mismatch");
                        mismatch = true;
                        break;
                    }
                    if (!sameNode(ned::editor::parse::NodeNextNamedSibling(nedNode), ts_node_next_named_sibling(tsNode))) {
                        fail(source, item, "next named sibling mismatch");
                        mismatch = true;
                        break;
                    }
                    if (!sameNode(ned::editor::parse::NodePrevNamedSibling(nedNode), ts_node_prev_named_sibling(tsNode))) {
                        fail(source, item, "prev named sibling mismatch");
                        mismatch = true;
                        break;
                    }
                    if (ned::editor::parse::NodeChildCount(nedNode) != ts_node_child_count(tsNode)) {
                        fail(source, item, "child count mismatch");
                        mismatch = true;
                        break;
                    }
                    const std::uint32_t start = ned::editor::parse::NodeStartByte(nedNode);
                    const std::uint32_t end   = ned::editor::parse::NodeEndByte(nedNode);
                    const std::uint32_t mid   = start + (end - start) / 2;
                    if (!sameNode(ned::editor::parse::NodeNamedDescendantForByteRange(nedNode, mid, mid),
                                  ts_node_named_descendant_for_byte_range(tsNode, mid, mid))) {
                        fail(source, item, "named descendant mismatch");
                        mismatch = true;
                        break;
                    }
                    if (!sameNode(ned::editor::parse::NodeDescendantForByteRange(nedNode, mid, mid),
                                  ts_node_descendant_for_byte_range(tsNode, mid, mid))) {
                        fail(source, item, "descendant mismatch");
                        mismatch = true;
                        break;
                    }

                    // Advance both cursors in lockstep preorder.
                    const bool nedDown = nedCursor.GotoFirstChild();
                    const bool tsDown  = ts_tree_cursor_goto_first_child(&tsCursor);
                    if (nedDown != tsDown) {
                        fail(source, item, "goto_first_child mismatch");
                        mismatch = true;
                        break;
                    }
                    if (nedDown)
                        continue;

                    bool done = false;
                    for (;;) {
                        const bool nedNext = nedCursor.GotoNextSibling();
                        const bool tsNext  = ts_tree_cursor_goto_next_sibling(&tsCursor);
                        if (nedNext != tsNext) {
                            fail(source, item, "goto_next_sibling mismatch");
                            mismatch = true;
                            break;
                        }
                        if (nedNext)
                            break;
                        const bool nedUp = nedCursor.GotoParent();
                        const bool tsUp  = ts_tree_cursor_goto_parent(&tsCursor);
                        if (nedUp != tsUp) {
                            fail(source, item, "goto_parent mismatch");
                            mismatch = true;
                            break;
                        }
                        if (!nedUp) {
                            done = true;
                            break;
                        }
                    }
                    if (mismatch || done)
                        break;
                }

                ts_tree_cursor_delete(&tsCursor);
                if (mismatch && failures.size() >= 50)
                    break;
            }
        }
    }

    CHECK(comparedNodes > 90000);
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
