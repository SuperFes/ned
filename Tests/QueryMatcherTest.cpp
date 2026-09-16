//
// M0 of the Phase 4a matcher (ROADMAP: "The matcher, decided and underway"):
// a census of every structural construct the bundled query files actually
// use, at the Form level (Editor/QueryData.h), so the matcher's supported
// set is a measured enumeration rather than a guess -- and so a future
// query edit that starts using a construct outside it fails here as a named
// diff (feature + first sighting as path:line), not as a silent behavioral
// mismatch inside the matcher.
//
// Deliberately grammar-free: this walks Forms, not trees, so it needs no
// Parser and runs on every build. Grammar-dependent facts (whether a symbol
// names a real node type, supertype expansion) are M1's compile-time
// validation, differentially gated in M3.
//

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/BundledLanguages.h"
#include "Editor/LanguageFiles.h"
#include "Editor/QueryData.h"
#include "Editor/Grammar/Languages.h"
#include "Editor/Grammar/Parser.h"
#include "Editor/Grammar/QueryMatcher.h"
#include "Editor/Grammar/Tree.h"

using ned::editor::BundledLanguages;
using ned::editor::CompileQueryFiles;
using ned::editor::EmbeddedLanguageFiles;
using ned::editor::LanguageDefinition;
using ned::editor::QueryFiles;
using ned::editor::querydata::Form;
using ned::editor::querydata::ParseJanet;
using ned::editor::querydata::ParseScm;
using ned::editor::grammar::LanguageByName;
using ned::editor::grammar::Query;
using ned::editor::grammar::QueryCapture;
using ned::editor::grammar::QueryMatch;
using ned::editor::grammar::QueryMatcher;

namespace {

const std::set<std::string, std::less<>>& QueryKinds() {
    static const std::set<std::string, std::less<>> kKinds = {
        "highlights",
        "folds",
        "imports",
        "tags",
        "tests",
        "indents",
        "locals",
        "injections",
        "format",
    };
    return kKinds;
}

// Basename without extension, e.g. "cpp/upstream/highlights.janet" -> "highlights".
std::string_view KindOfPath(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    std::string_view  base  = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot   = base.rfind('.');
    return dot == std::string_view::npos ? base : base.substr(0, dot);
}

struct Census {
    std::map<std::string, int>         counts;
    std::map<std::string, std::string> firstSighting; // feature -> "path:line"

    void Record(const std::string& feature, std::string_view path, int line) {
        if (++counts[feature] == 1) {
            firstSighting[feature] = std::string(path) + ":" + std::to_string(line);
        }
    }
};

bool IsCapture(const Form& form) {
    return form.kind == Form::Kind::Symbol && !form.text.empty() && form.text.front() == '@';
}

bool IsAnchor(const Form& form) {
    return form.kind == Form::Kind::Symbol && form.text == ".";
}

bool IsField(const Form& form) {
    return form.kind == Form::Kind::Symbol && !form.text.empty() && form.text.back() == ':';
}

bool IsNegatedField(const Form& form) {
    return form.kind == Form::Kind::Symbol && !form.text.empty() && form.text.front() == '!';
}

bool IsWildcard(const Form& form) {
    return form.kind == Form::Kind::Symbol && form.text == "_";
}

std::string KindName(const Form& form) {
    switch (form.kind) {
        case Form::Kind::List:
            return "list";
        case Form::Kind::Alternation:
            return "alternation";
        case Form::Kind::String:
            return "string";
        case Form::Kind::Predicate:
            return "predicate";
        case Form::Kind::Comment:
            return "comment";
        case Form::Kind::Symbol:
            break;
    }
    if (IsCapture(form)) {
        return "capture";
    }
    if (IsAnchor(form)) {
        return "anchor";
    }
    if (IsField(form)) {
        return "field";
    }
    if (IsNegatedField(form)) {
        return "negated-field";
    }
    if (IsWildcard(form)) {
        return "wildcard";
    }
    return "symbol";
}

void RecordQuantifier(const Form& form, Census& census, std::string_view path) {
    if (form.quantifier == 0) {
        return;
    }
    census.Record("quantifier '" + std::string(1, form.quantifier) + "' on " + KindName(form), path, form.line);
}

void WalkForm(const Form& form, Census& census, std::string_view path);

// A sibling sequence: the top level of a file, a node pattern's children
// after its type symbol, a group's items, or an alternation's items.
// `context` names which, since several constructs are only legal (or only
// meaningful) in some of them.
void WalkSequence(const std::vector<Form>& items, Census& census, std::string_view path, const std::string& context) {
    const Form* previousPattern = nullptr; // last non-comment, non-capture, non-predicate item
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Form& item = items[i];
        if (item.kind == Form::Kind::Comment) {
            continue;
        }
        if (item.kind == Form::Kind::Predicate) {
            std::string signature = "predicate :" + item.text + " (";
            for (std::size_t arg = 0; arg < item.items.size(); ++arg) {
                if (arg != 0) {
                    signature += ' ';
                }
                const Form& operand = item.items[arg];
                if (IsCapture(operand)) {
                    signature += "capture";
                }
                else if (operand.kind == Form::Kind::String) {
                    signature += "string";
                }
                else if (operand.kind == Form::Kind::Symbol) {
                    signature += "token";
                }
                else {
                    signature += KindName(operand);
                }
            }
            signature += ")";
            census.Record(signature, path, item.line);
            census.Record("predicate in " + context, path, item.line);
            continue;
        }
        if (IsAnchor(item)) {
            // Leading/trailing/between, ignoring comments/predicates around it.
            bool first = previousPattern == nullptr;
            bool last  = true;
            for (std::size_t j = i + 1; j < items.size(); ++j) {
                const Form::Kind k = items[j].kind;
                if (k != Form::Kind::Comment && k != Form::Kind::Predicate) {
                    last = false;
                    break;
                }
            }
            const std::string position = first ? "leading" : (last ? "trailing" : "between");
            census.Record("anchor " + position + " in " + context, path, item.line);
            continue;
        }
        if (IsCapture(item)) {
            if (previousPattern == nullptr) {
                census.Record("capture with no preceding pattern in " + context, path, item.line);
            }
            else {
                census.Record("capture after " + KindName(*previousPattern), path, item.line);
            }
            // A capture does not reset previousPattern: `(x) @a @b` attaches
            // both to the same pattern, and an anchor after `(x) @a` still
            // anchors against (x).
            continue;
        }
        if (IsField(item)) {
            // The field prefixes the next non-comment item.
            const Form* target = nullptr;
            for (std::size_t j = i + 1; j < items.size(); ++j) {
                if (items[j].kind != Form::Kind::Comment) {
                    target = &items[j];
                    break;
                }
            }
            census.Record("field before " + (target ? KindName(*target) : std::string("nothing")), path, item.line);
            continue;
        }
        if (IsNegatedField(item)) {
            census.Record("negated field in " + context, path, item.line);
            continue;
        }
        WalkForm(item, census, path);
        census.Record(KindName(item) + " in " + context, path, item.line);
        previousPattern = &item;
    }
}

void WalkForm(const Form& form, Census& census, std::string_view path) {
    RecordQuantifier(form, census, path);
    switch (form.kind) {
        case Form::Kind::List: {
            // A node pattern's first meaningful item is its type symbol (or
            // the `_` wildcard); a paren group's is itself a pattern.
            const Form* head = nullptr;
            for (const Form& item : form.items) {
                if (item.kind != Form::Kind::Comment) {
                    head = &item;
                    break;
                }
            }
            if (head == nullptr) {
                census.Record("empty list", path, form.line);
                return;
            }
            if (head->kind == Form::Kind::Symbol && !IsCapture(*head) && !IsAnchor(*head) && !IsField(*head) &&
                !IsNegatedField(*head)) {
                const std::string headName = IsWildcard(*head) ? "wildcard" : "type";
                if (!IsWildcard(*head) && head->text.find('/') != std::string::npos) {
                    census.Record("supertype-scoped type name", path, head->line);
                }
                if (!IsWildcard(*head) && (head->text == "ERROR" || head->text == "MISSING")) {
                    census.Record("special node " + head->text, path, head->line);
                }
                census.Record("node pattern (" + headName + " head)", path, form.line);
                const std::vector<Form> children(form.items.begin() + (head - form.items.data()) + 1,
                                                 form.items.end());
                WalkSequence(children, census, path, "node children");
                return;
            }
            census.Record("group (head is " + KindName(*head) + ")", path, form.line);
            WalkSequence(form.items, census, path, "group");
            return;
        }
        case Form::Kind::Alternation:
            WalkSequence(form.items, census, path, "alternation");
            return;
        case Form::Kind::String:
        case Form::Kind::Symbol:
        case Form::Kind::Predicate:
        case Form::Kind::Comment:
            return;
    }
}

} // namespace

TEST_CASE("query construct census: every bundled query file uses only the enumerated feature set", "[QueryMatcher]") {
    Census census;
    int    filesWalked = 0;

    for (const auto& file : EmbeddedLanguageFiles()) {
        if (!QueryKinds().contains(std::string(KindOfPath(file.path)))) {
            continue;
        }
        ++filesWalked;
        const std::vector<Form> forms = ParseJanet(file.content);
        WalkSequence(forms, census, file.path, "top level");
    }

    // The census only means something if it actually walked the corpus.
    REQUIRE(filesWalked > 90);

    // The supported enumeration -- the exact measured surface of the 98
    // bundled query files (counts as of 2026-09-12; the census run with
    // NED_QUERY_CENSUS=1 reprints them). Adding a feature here is a conscious
    // matcher scope change; a census failure below names the new construct
    // and where it first appears. Notable absences the matcher therefore
    // does NOT need: the '+' quantifier (nowhere), quantifiers on strings,
    // bare-symbol node references as children or inside alternations,
    // top-level predicates, and anchors inside alternations.
    static const std::set<std::string, std::less<>> kSupported = {
        // Sequence membership.
        "list in top level",            // x988
        "alternation in top level",     // x139
        "string in top level",          // x92
        "list in node children",        // x999
        "alternation in node children", // x39
        "string in node children",      // x54
        "wildcard in node children",    // x6
        "list in group",                // x169
        "alternation in group",         // x4
        "string in group",              // x5
        "list in alternation",          // x359
        "string in alternation",        // x881
        // Node patterns and groups.
        "node pattern (type head)",     // x2321
        "node pattern (wildcard head)", // x26 -- (_) matches any NAMED node
        "special node ERROR",           // x1, xml upstream -- the parser's error node is matchable
        "group (head is list)",         // x161
        "group (head is string)",       // x5
        "group (head is alternation)",  // x2
        // Captures.
        "capture after list",        // x1218
        "capture after string",      // x136
        "capture after alternation", // x157
        "capture after wildcard",    // x4 -- bare _ matches named AND anonymous
        // Fields.
        "field before list",              // x505
        "field before string",            // x2
        "field before alternation",       // x19
        "field before wildcard",          // x2
        "negated field in node children", // x4
        // Anchors.
        "anchor leading in node children",  // x47
        "anchor between in node children",  // x38
        "anchor trailing in node children", // x1, kotlin upstream
        "anchor between in group",          // x10, go upstream tags
        // Quantifiers -- lists, bare wildcards and single-branch
        // alternations, never '+'.
        "quantifier '*' on list",        // x7
        "quantifier '?' on list",        // x3
        "quantifier '?' on wildcard",    // x2
        "quantifier '?' on alternation", // x1, sql upstream -- `parameter: [(literal)]?`
        // Predicates: placement.
        "predicate in node children", // x50
        "predicate in group",         // x162
        // Predicates: name + operand signature, exactly as used. A `token`
        // operand is a bare symbol; through tree-sitter's compiler it is the
        // same String step a quoted operand is, and the matcher must treat
        // the two identically.
        "predicate :eq? (capture string)",                // x29
        "predicate :not-eq? (capture string)",            // x5
        "predicate :match? (capture string)",             // x43
        "predicate :not-match? (capture string)",         // x2
        "predicate :lua-match? (capture string)",         // x24
        "predicate :any-of? (capture string string)",     // x57 (variadic, normalized below)
        "predicate :not-any-of? (capture string string)", // x4
        "predicate :has-ancestor? (capture token)",       // x10
        "predicate :not-has-ancestor? (capture token)",   // x2
        "predicate :not-has-parent? (capture string)",    // x7
        // The two variadic has-parent spellings are INERT today: the
        // evaluator handles exactly two operands and treats anything else as
        // pass-through (Query.cpp), so these never filter. Recorded in the
        // ROADMAP watch list; the matcher reproduces the inertness for the
        // differential gate, and fixing it is a separate, deliberate change.
        "predicate :has-parent? (capture token token)",           // x1, cpp/highlights.janet:370
        "predicate :not-has-parent? (capture token token token)", // x2, c/highlights.janet:181
        // Non-filtering directives.
        "predicate :set! (token string)", // x7 -- injection.language, read by Matches()
        "predicate :set! (token token)",  // x3 -- priority, currently unread
        "predicate :is-not? (token)",     // x2, nvim convention, inert
        // nvim-treesitter capture-text directives in upstream files; inert
        // in ned (they rewrite capture TEXT for nvim's own consumers).
        "predicate :strip! (capture string)",            // x7
        "predicate :select-adjacent! (capture capture)", // x5
        "predicate :set-adjacent! (capture capture)",    // x2
    };

    // any-of? carries a variable operand count; normalize "string string
    // string ..." down to the two-operand signature before comparing, so the
    // enumeration stays readable without listing every arity in use.
    std::map<std::string, int>         normalized;
    std::map<std::string, std::string> sightings;
    for (const auto& [feature, count] : census.counts) {
        std::string key = feature;
        if (key.starts_with("predicate :any-of?") || key.starts_with("predicate :not-any-of?")) {
            const std::size_t open = key.find('(');
            std::string       name = key.substr(0, open);
            key                    = name + "(capture string string)";
        }
        normalized[key] += count;
        if (!sightings.contains(key)) {
            sightings[key] = census.firstSighting.at(feature);
        }
    }

    // NED_QUERY_CENSUS=1 prints the full measured surface, counts and first
    // sightings included -- the number the ROADMAP's Phase 4a scope quotes.
    if (std::getenv("NED_QUERY_CENSUS") != nullptr) {
        for (const auto& [feature, count] : normalized) {
            WARN(feature << " (x" << count << ", first at " << sightings.at(feature) << ")");
        }
    }

    std::string unsupported;
    for (const auto& [feature, count] : normalized) {
        if (!kSupported.contains(feature)) {
            unsupported += "  " + feature + " (x" + std::to_string(count) + ", first at " + sightings.at(feature) +
                           ")\n";
        }
    }
    INFO("constructs outside the supported enumeration:\n"
         << unsupported);
    REQUIRE(unsupported.empty());
}

// ---------------------------------------------------------------------------
// M1/M3: the matcher compiles every bundled language's every query kind, and
// matches identically to the tree-sitter engine over the oracle corpus.
// ---------------------------------------------------------------------------

namespace {

struct KindText {
    const char* kind;
    std::string text; // concatenated compiled query text ("" = language has none)
};

std::vector<KindText> QueryTextsFor(const LanguageDefinition& definition) {
    const QueryFiles&     files = definition.queries;
    std::vector<KindText> out;
    const auto            add = [&](const char* kind, const std::vector<std::string>& paths) {
        if (!paths.empty()) {
            out.push_back(KindText{kind, CompileQueryFiles(paths).text});
        }
    };
    add("highlights", files.highlights);
    add("folds", files.folds);
    add("imports", files.imports);
    add("tags", files.tags);
    add("tests", files.tests);
    add("indents", files.indents);
    add("locals", files.locals);
    add("injections", files.injections);
    add("format", files.format);
    return out;
}

std::string ReadCorpusFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string DescribeCapture(const QueryCapture& capture) {
    return "@" + capture.name + " [" + std::to_string(capture.startByte) + ", " + std::to_string(capture.endByte) +
           ")";
}

// Serializes a match for canonical comparison: capture list + directives.
std::string DescribeMatch(const QueryMatch& match) {
    std::string out = "{";
    for (const auto& capture : match.captures) {
        out += " @" + capture.name + "[" + std::to_string(capture.startByte) + "," + std::to_string(capture.endByte) +
               ")";
    }
    std::vector<std::string> directives;
    for (const auto& [key, value] : match.setDirectives) {
        directives.push_back(" #set! " + key + "=" + value);
    }
    std::sort(directives.begin(), directives.end());
    for (const auto& directive : directives) {
        out += directive;
    }
    out += " }";
    return out;
}

// The corpus file a language's differential run uses, by extension claim.
std::vector<std::filesystem::path> CorpusFilesFor(const LanguageDefinition& definition) {
    static const std::filesystem::path corpusDir = std::filesystem::path(NED_REPO_ROOT) / "Tests" / "Oracle" / "corpus";
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(corpusDir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string extension = entry.path().extension().string();
        for (const std::string& claimed : definition.extensions) {
            if (claimed == extension) {
                files.push_back(entry.path());
                break;
            }
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// Compares two capture streams as MULTISETS. The gate is deliberately
// set-level, not stream-order-level: the matcher finds exactly what
// tree-sitter finds, while emission ORDER in same-start-byte ties is ned's
// own documented deterministic merge (QueryMatcher.cpp's CollectCaptures)
// rather than tree-sitter's -- tree-sitter's exact interleave there is
// scheduled by in-progress states including ones that later fail, which
// only the real step machine can reproduce (measured 2026-09-12: 2 tie
// sites over 570 corpus comparisons, one of them render-neutral).
// Rendering stays pinned where rendering is pinned: the oracle and the
// highlight suites, which is where an order-semantics change would surface
// as a reviewable diff.
void RequireSameCaptures(const std::string& label, std::vector<QueryCapture> expected,
                         std::vector<QueryCapture> actual) {
    const auto key = [](const QueryCapture& capture) {
        return std::tuple<std::size_t, std::size_t, const std::string&>(capture.startByte, capture.endByte,
                                                                        capture.name);
    };
    const auto byKey = [&](const QueryCapture& a, const QueryCapture& b) { return key(a) < key(b); };
    std::sort(expected.begin(), expected.end(), byKey);
    std::sort(actual.begin(), actual.end(), byKey);
    const std::size_t limit = std::min(expected.size(), actual.size());
    for (std::size_t i = 0; i < limit; ++i) {
        if (key(expected[i]) != key(actual[i])) {
            INFO(label << ": capture multisets differ at sorted index " << i);
            REQUIRE(expected[i].name + DescribeCapture(expected[i]) == actual[i].name + DescribeCapture(actual[i]));
        }
    }
    INFO(label << ": ts produced " << expected.size() << " captures, matcher " << actual.size()
               << (expected.size() > actual.size() && limit < expected.size()
                       ? "; first ts-only: " + DescribeCapture(expected[limit])
                   : actual.size() > expected.size() && limit < actual.size()
                       ? "; first matcher-only: " + DescribeCapture(actual[limit])
                       : ""));
    REQUIRE(expected.size() == actual.size());
}

void RequireSameMatches(const std::string& label, std::vector<QueryMatch> expected, std::vector<QueryMatch> actual) {
    // Canonical order: matches carry no inherent order contract consumers
    // rely on beyond position, so both sides sort by serialized form.
    std::vector<std::string> expectedText;
    expectedText.reserve(expected.size());
    for (const auto& match : expected) {
        expectedText.push_back(DescribeMatch(match));
    }
    std::vector<std::string> actualText;
    actualText.reserve(actual.size());
    for (const auto& match : actual) {
        actualText.push_back(DescribeMatch(match));
    }
    std::sort(expectedText.begin(), expectedText.end());
    std::sort(actualText.begin(), actualText.end());
    const std::size_t limit = std::min(expectedText.size(), actualText.size());
    for (std::size_t i = 0; i < limit; ++i) {
        if (expectedText[i] != actualText[i]) {
            INFO(label << ": first differing match (canonical order) " << i << "\n  ts:      " << expectedText[i]
                       << "\n  matcher: " << actualText[i]);
            REQUIRE(expectedText[i] == actualText[i]);
        }
    }
    INFO(label << ": ts produced " << expectedText.size() << " matches, matcher " << actualText.size()
               << (expectedText.size() > limit ? "; first ts-only: " + expectedText[limit]
                   : actualText.size() > limit ? "; first matcher-only: " + actualText[limit]
                                               : ""));
    REQUIRE(expectedText.size() == actualText.size());
}

} // namespace

TEST_CASE("QueryMatcher compiles every bundled language's every query kind", "[QueryMatcher]") {
    int compiled = 0;
    for (const LanguageDefinition& definition : BundledLanguages()) {
        if (definition.grammarless) {
            continue;
        }
        const std::string_view grammar =
            definition.grammar.empty() ? std::string_view(definition.name) : definition.grammar;
        const auto language = LanguageByName(grammar);
        REQUIRE(language);
        for (const KindText& kind : QueryTextsFor(definition)) {
            INFO(definition.name << "/" << kind.kind);
            const std::vector<Form> forms = ParseScm(kind.text);
            REQUIRE_NOTHROW(QueryMatcher(*language, forms));
            ++compiled;
        }
    }
    REQUIRE(compiled > 80);
}

// per-subtree-fact-memoization follow-up (census, 2026-09-13): the exact set
// of bundled patterns whose result can depend on structure OUTSIDE the node
// they're attached to -- QueryMatch::ancestorCrossing, computed from an
// actually-evaluated (not-)has-ancestor?/(not-)has-parent? predicate (the
// variadic 3+-type-operand spelling is arity-inert per QueryPredicates.cpp
// and correctly does NOT count -- see PredicateReadsOutsideSubtree). A
// future per-subtree fact cache must always fully re-derive these, never
// reuse them across a reparse: the same subtree can answer differently once
// its ancestry changes even when its own bytes haven't. Pinned so a query
// edit that adds, removes, or changes the arity of one of these predicates
// is a conscious, named diff rather than a silent cache-correctness
// regression discovered later (NED_QUERY_CENSUS=1 reprints the breakdown).
TEST_CASE("query census: ancestor-crossing patterns are pinned per language/kind", "[QueryMatcher]") {
    std::map<std::string, std::size_t> counts;
    std::size_t                        total = 0;
    for (const LanguageDefinition& definition : BundledLanguages()) {
        if (definition.grammarless) {
            continue;
        }
        const std::string_view grammar =
            definition.grammar.empty() ? std::string_view(definition.name) : definition.grammar;
        const auto language = LanguageByName(grammar);
        REQUIRE(language);
        for (const KindText& kind : QueryTextsFor(definition)) {
            const QueryMatcher matcher(*language, kind.text);
            const std::size_t  n = matcher.AncestorCrossingPatternCount();
            if (n > 0) {
                counts[std::string(definition.name) + "/" + kind.kind] = n;
                total += n;
            }
        }
    }
    if (std::getenv("NED_QUERY_CENSUS") != nullptr) {
        for (const auto& [key, n] : counts) {
            WARN(key << ": " << n);
        }
        WARN("ancestor-crossing total: " << total);
    }
    const std::map<std::string, std::size_t> expected = {
        {"bash/format", 2},
        {"c/highlights", 2},
        {"cpp/highlights", 5},
        {"cpp/indents", 1},
        {"csharp/locals", 1},
        {"java/locals", 1},
        {"kotlin/locals", 1},
        {"python/locals", 1},
        {"rust/locals", 1},
        {"yaml/indents", 2},
    };
    CHECK(counts == expected);
    CHECK(total == 17);
}

// Ned's own emission order, pinned. The matcher's capture stream reproduces
// tree-sitter's incremental cursor merge for every case the corpus
// exercises EXCEPT same-start-byte ties whose scheduling depended on
// tree-sitter states that ultimately fail (predicates unevaluated in the C
// library, structural dead ends) -- reproducing those requires the real
// step machine, and 2026-09-12's measured decision was to define ned's own
// deterministic merge instead (2 tie sites over 570 corpus comparisons, one
// render-neutral). This test freezes the defined order on a minimal tie so
// a change to it is a conscious diff, not drift.
TEST_CASE("QueryMatcher emission order: a same-byte tie orders by finish position then pattern index",
          "[QueryMatcher]") {
    const auto language = LanguageByName("c");
    REQUIRE(language);
    // Pattern 0 finishes at the identifier (its last needed node); pattern 1
    // finishes at the primitive_type itself. Both capture "int" [0, 3).
    // Pattern 1 finishes first but is HELD -- pattern 0's capture is already
    // pending at the same byte with a lower pattern index -- so pattern 0
    // emits first once it finishes. (This mirrors the clojure indents tie,
    // where tree-sitter behaves identically.)
    const QueryMatcher matcher(*language,
                               "(declaration type: (_) @a declarator: (identifier))\n(primitive_type) @b\n");
    const std::string  source = "int f;";
    const auto         tree   = ned::editor::grammar::Parser(*language).Parse(source);

    const std::vector<QueryCapture> captures = matcher.Captures(tree.RootNode(), source);
    REQUIRE(captures.size() == 2);
    CHECK(captures[0].name == "a");
    CHECK(captures[0].startByte == 0);
    CHECK(captures[0].endByte == 3);
    CHECK(captures[1].name == "b");
    CHECK(captures[1].startByte == 0);
    CHECK(captures[1].endByte == 3);
}

// Outside the bundled census but legal in a foreign :queries-dir file (a
// system tree-sitter install's own .scm): the '+' quantifier. One maximal
// run per start, no zero-length run.
TEST_CASE("QueryMatcher supports '+' for foreign queries: one maximal run, no empty match", "[QueryMatcher]") {
    const auto language = LanguageByName("c");
    REQUIRE(language);
    const QueryMatcher matcher(*language, "(translation_unit (declaration)+ @d)\n");
    const std::string  source = "int a;\nint b;\n";
    const auto         tree   = ned::editor::grammar::Parser(*language).Parse(source);

    const std::vector<QueryMatch> matches = matcher.Matches(tree.RootNode(), source);
    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].captures.size() == 2);
    CHECK(matches[0].captures[0].startByte == 0);
    CHECK(matches[0].captures[1].startByte == 7);
}

// ---------------------------------------------------------------------------
// ROADMAP watch-list closure: "QueryMatcher scope: two upstream constructs
// the census excludes" (cmake's nested group, diff's top-level field
// prefix) plus the SQL quantified-single-alternation instance recorded
// alongside them. All three are now supported; these pin real upstream
// constructs (not synthetic stand-ins) as regression tests, per the
// ROADMAP entry's own "written against these two files as the test cases".
// ---------------------------------------------------------------------------

// tree-sitter-cmake's queries/highlights.scm ~line 131: a multi-pattern
// group -- CACHE followed immediately by a type keyword -- nested inside
// `argument_list`'s own children sequence. Verbatim upstream text.
TEST_CASE("QueryMatcher: nested multi-pattern group (cmake set/CACHE/type)", "[QueryMatcher]") {
    const auto language = LanguageByName("cmake");
    REQUIRE(language);
    const QueryMatcher matcher(*language,
                               "(normal_command\n"
                               "  (identifier) @_function\n"
                               "  (#match? @_function \"^[sS][eE][tT]$\")\n"
                               "  (argument_list\n"
                               "    .\n"
                               "    (argument)\n"
                               "    ((argument) @_cache @keyword.modifier\n"
                               "      .\n"
                               "      (argument) @_type @type\n"
                               "      (#any-of? @_cache \"CACHE\")\n"
                               "      (#any-of? @_type \"BOOL\" \"FILEPATH\" \"PATH\" \"STRING\" \"INTERNAL\"))))\n");

    SECTION("a CACHE/BOOL pair right after the variable name matches, both group items captured") {
        const std::string             source  = "set(FOO CACHE BOOL \"desc\")\n";
        const auto                    tree    = ned::editor::grammar::Parser(*language).Parse(source);
        const std::vector<QueryMatch> matches = matcher.Matches(tree.RootNode(), source);
        REQUIRE(matches.size() == 1);
        const QueryMatch& match = matches[0];
        REQUIRE(match.captures.size() == 5);
        CHECK(match.captures[0].name == "_function");
        CHECK(source.substr(match.captures[0].startByte, match.captures[0].endByte - match.captures[0].startByte) == "set");
        CHECK(match.captures[1].name == "_cache");
        CHECK(match.captures[2].name == "keyword.modifier");
        CHECK(source.substr(match.captures[1].startByte, match.captures[1].endByte - match.captures[1].startByte) == "CACHE");
        CHECK(match.captures[1].startByte == match.captures[2].startByte);
        CHECK(match.captures[3].name == "_type");
        CHECK(match.captures[4].name == "type");
        CHECK(source.substr(match.captures[3].startByte, match.captures[3].endByte - match.captures[3].startByte) == "BOOL");
    }

    SECTION("two non-CACHE arguments after the variable name: the group finds no match, predicate never lets it through") {
        const std::string             source  = "set(FOO \"value\" \"value2\")\n";
        const auto                    tree    = ned::editor::grammar::Parser(*language).Parse(source);
        const std::vector<QueryMatch> matches = matcher.Matches(tree.RootNode(), source);
        CHECK(matches.empty());
    }
}

// tree-sitter-diff's queries/highlights.scm tail: two top-level
// field-prefixed patterns constraining which field (forward/reverse) a
// binary_hunk must hold in its binary_patch parent. Verbatim upstream
// text; source is the grammar's own "Literal binary patch" corpus sample
// (test/corpus/binary.txt), whose binary_patch node holds exactly one
// forward hunk followed by one reverse hunk.
TEST_CASE("QueryMatcher: top-level field-prefixed pattern (diff forward:/reverse: binary_hunk)", "[QueryMatcher]") {
    const auto language = LanguageByName("diff");
    REQUIRE(language);
    const QueryMatcher matcher(*language,
                               "forward: (binary_hunk\n"
                               "  (payload) @diff.plus)\n"
                               "\n"
                               "reverse: (binary_hunk\n"
                               "  (payload) @diff.minus)\n");
    const std::string  source =
        "diff --git a/bin.dat b/bin.dat\n"
        "index d24dc6832d641b8bcae6c7bd4aaaf0fd11eb26ab..c93bd035c4823c2d202965b8c5a1addfd66ac157 100644\n"
        "GIT binary patch\n"
        "literal 13\n"
        "UcmZQzWMX#qaP)I`bzztZ01RIOiU0rr\n"
        "\n"
        "literal 11\n"
        "ScmZQzWMX#m^m7b~WB>pM^#RNP\n";
    const auto tree = ned::editor::grammar::Parser(*language).Parse(source);

    const std::vector<QueryCapture> captures = matcher.Captures(tree.RootNode(), source);
    REQUIRE(captures.size() == 2);
    CHECK(captures[0].name == "diff.plus");
    CHECK(source.substr(captures[0].startByte, captures[0].endByte - captures[0].startByte) == "UcmZQzWMX#qaP)I`bzztZ01RIOiU0rr");
    CHECK(captures[1].name == "diff.minus");
    CHECK(source.substr(captures[1].startByte, captures[1].endByte - captures[1].startByte) == "ScmZQzWMX#m^m7b~WB>pM^#RNP");
}

// tree-sitter-sql's queries/highlights.scm cast pattern: `parameter:
// [(literal)]?` -- a quantifier on a single-element alternation. Proven
// generically (not tied to sql's own, unvendored grammar.js/corpus) by
// showing `[(x)]?` and `(x)?` compile to observably identical matchers: a
// single-branch alternation has no fork to differ on, so quantifying it
// is exactly quantifying its one branch. Both present and absent cases are
// checked, since an absent optional is where a fork-vs-no-fork difference
// would most plausibly show up.
TEST_CASE("QueryMatcher: quantifier on a single-element alternation matches its bare-node equivalent", "[QueryMatcher]") {
    const auto language = LanguageByName("c");
    REQUIRE(language);
    const QueryMatcher alternationForm(*language, "(translation_unit [(declaration)]? @first)\n");
    const QueryMatcher bareForm(*language, "(translation_unit (declaration)? @first)\n");

    SECTION("the optional is present") {
        const std::string               source = "int x;\n";
        const auto                      tree   = ned::editor::grammar::Parser(*language).Parse(source);
        const std::vector<QueryCapture> a      = alternationForm.Captures(tree.RootNode(), source);
        const std::vector<QueryCapture> b      = bareForm.Captures(tree.RootNode(), source);
        REQUIRE(a.size() == 1);
        REQUIRE(b.size() == 1);
        CHECK(a[0].startByte == b[0].startByte);
        CHECK(a[0].endByte == b[0].endByte);
    }

    SECTION("the optional is absent") {
        const std::string               source = "";
        const auto                      tree   = ned::editor::grammar::Parser(*language).Parse(source);
        const std::vector<QueryCapture> a      = alternationForm.Captures(tree.RootNode(), source);
        const std::vector<QueryCapture> b      = bareForm.Captures(tree.RootNode(), source);
        CHECK(a.empty());
        CHECK(b.empty());
    }
}
