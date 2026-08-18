#include "Mode.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "Key.h"
#include "Org.h"
#include "TreeSitter/Languages.h"
#include "TreeSitter/Parser.h"
#include "TreeSitter/Queries.h"
#include "TreeSitter/Query.h"
#include "TreeSitter/Tree.h"

namespace ned::editor {

namespace {

    // The "leaf" tree-sitter/Neovim capture names this project recognizes,
    // mapped to a SyntaxClass -- deliberately not exhaustive (there are
    // dozens more highly-specific ones across the whole grammar ecosystem,
    // e.g. "function.builtin.static"); SyntaxClassForCapture below resolves
    // anything not listed here by walking up to its nearest recognized
    // ancestor via the dotted-name convention itself, not straight to
    // Default. Grouped by SyntaxClass, with every alternate/older spelling a
    // real grammar might use (e.g. bare "conditional"/"repeat" predate the
    // now-standard "keyword.conditional"/"keyword.repeat") listed alongside
    // the canonical one.
    const std::unordered_map<std::string_view, SyntaxClass>& CaptureTable() {
        static const std::unordered_map<std::string_view, SyntaxClass> table = {
            {"comment", SyntaxClass::Comment},
            {"comment.documentation", SyntaxClass::DocComment},

            {"string", SyntaxClass::String},
            {"string.special", SyntaxClass::String},
            {"string.special.key", SyntaxClass::String},
            {"string.special.include", SyntaxClass::IncludePath}, // Ned's own addition -- see cpp.scm's header comment
            {"string.regexp", SyntaxClass::String},
            {"string.escape", SyntaxClass::StringEscape},
            {"escape", SyntaxClass::StringEscape},
            {"character", SyntaxClass::String}, // a char_literal ('a') -- found missing during the @spell investigation; c.scm's own real capture name

            {"number", SyntaxClass::Number},
            {"float", SyntaxClass::Number},

            {"keyword", SyntaxClass::Keyword},
            {"keyword.function", SyntaxClass::Keyword},
            {"keyword.operator", SyntaxClass::Operator},
            {"keyword.import", SyntaxClass::Keyword},
            {"keyword.storage", SyntaxClass::Keyword},
            {"keyword.modifier", SyntaxClass::KeywordModifier}, // access specifiers (public/private/protected), storage/type qualifiers
            {"keyword.type", SyntaxClass::Keyword},
            {"keyword.control", SyntaxClass::ControlKeyword},
            {"keyword.control.conditional", SyntaxClass::ControlKeyword},
            {"keyword.control.repeat", SyntaxClass::ControlKeyword},
            {"keyword.control.return", SyntaxClass::ControlKeyword},
            {"keyword.control.import", SyntaxClass::Keyword},
            {"keyword.conditional", SyntaxClass::ControlKeyword},
            {"keyword.repeat", SyntaxClass::ControlKeyword},
            {"keyword.return", SyntaxClass::ControlKeyword},
            {"conditional", SyntaxClass::ControlKeyword},
            {"repeat", SyntaxClass::ControlKeyword},
            {"include", SyntaxClass::Keyword},
            {"preproc", SyntaxClass::Keyword},
            {"label", SyntaxClass::Label},

            {"function", SyntaxClass::Function},
            {"function.call", SyntaxClass::Function},
            {"function.method", SyntaxClass::Method},
            {"function.method.call", SyntaxClass::Method},
            {"function.macro", SyntaxClass::Function},
            {"function.builtin", SyntaxClass::FunctionBuiltin},
            {"method", SyntaxClass::Method},
            {"constructor", SyntaxClass::Constructor},

            {"type", SyntaxClass::Type},
            {"type.definition", SyntaxClass::Type},
            {"type.builtin", SyntaxClass::TypeBuiltin},
            {"type.return", SyntaxClass::ReturnType}, // Ned's own addition -- see cpp.scm's header comment

            {"constant", SyntaxClass::Constant},
            {"constant.macro", SyntaxClass::Constant},
            {"constant.builtin", SyntaxClass::ConstantBuiltin},
            {"boolean", SyntaxClass::ConstantBuiltin},

            {"variable", SyntaxClass::Variable},
            {"variable.builtin", SyntaxClass::VariableBuiltin},
            {"variable.parameter", SyntaxClass::Parameter},
            {"parameter", SyntaxClass::Parameter},

            {"property", SyntaxClass::Property},
            {"variable.member", SyntaxClass::Property},
            {"field", SyntaxClass::Property},

            {"operator", SyntaxClass::Operator},

            {"punctuation", SyntaxClass::Punctuation},
            {"punctuation.bracket", SyntaxClass::Punctuation},
            {"punctuation.delimiter", SyntaxClass::Punctuation},
            {"punctuation.special", SyntaxClass::Punctuation},
            {"delimiter", SyntaxClass::Punctuation},

            {"tag", SyntaxClass::Tag},
            {"tag.delimiter", SyntaxClass::Punctuation},

            {"attribute", SyntaxClass::Attribute},

            {"module", SyntaxClass::Namespace},
            {"namespace", SyntaxClass::Namespace},

            // Org-mode syntax-highlighting follow-up -- Source/Editor/
            // TreeSitter/OrgHighlights.scm's own capture names for
            // constructs that don't need C++ post-processing (unlike
            // "org.headline.stars"/"org.keyword.candidate", resolved
            // directly in OrgMode() instead, see its own doc comment).
            {"checkbox", SyntaxClass::Checkbox},
            {"strong", SyntaxClass::Strong},
            {"emphasis", SyntaxClass::Emphasis},
            {"underline", SyntaxClass::Underline},
            {"strikethrough", SyntaxClass::Strikethrough},
        };
        return table;
    }

    // Maps a tree-sitter query capture name (e.g. "string.special.key",
    // without the leading '@') to a SyntaxClass. A name not found in
    // CaptureTable() verbatim is resolved by repeatedly stripping the last
    // '.'-separated segment and trying again -- tree-sitter/Neovim's own
    // convention is that a capture name forms a dotted hierarchy from most
    // to least specific ("string.special.key" is a kind of "string.special"
    // is a kind of "string"), so an unrecognized specific name should
    // resolve to its nearest recognized ancestor, not straight to Default
    // the way a totally unrelated/unknown name correctly does.
    SyntaxClass SyntaxClassForCapture(std::string_view captureName) {
        const auto& table = CaptureTable();

        while (true) {
            if (const auto it = table.find(captureName); it != table.end()) {
                return it->second;
            }
            const std::size_t dot = captureName.rfind('.');
            if (dot == std::string_view::npos) {
                return SyntaxClass::Default;
            }
            captureName = captureName.substr(0, dot);
        }
    }

    // A real, confirmed-via-reading-the-vendored-query bug found and fixed
    // here: nvim-treesitter's own query convention has capture names that
    // are never meant to become a highlight at all -- a leading underscore
    // (e.g. "_parent"/"_type"/"_u" in c.scm/cpp.scm, used only by that
    // pattern's own #eq?/#not-has-parent? predicate to inspect a node's
    // text/relationship) and the literal names "spell"/"nospell" (Neovim's
    // own spell-check-hint captures). None of these have a CaptureTable
    // entry, so they used to resolve to SyntaxClass::Default like any other
    // unrecognized name -- but unlike a genuinely unrecognized *real*
    // highlight name, these frequently tag the exact same node a real
    // capture already tagged (e.g. c.scm's own "(comment) @comment @spell"
    // -- one pattern, two captures on one node), and HighlightSpan's
    // documented "later capture wins" overlap rule then let the spurious
    // Default-classed span silently clobber the real one on every affected
    // node -- comments rendering with no color/italic at all, regardless of
    // Theme configuration, was this bug's own real, reported symptom.
    // Filtering these out before they ever become a HighlightSpan (not
    // just mapping them to Default, which wouldn't fix the clobbering) is
    // the correct, general fix -- not a per-language patch, since this
    // exact convention is universal across every nvim-treesitter query file,
    // including ones not yet vendored here.
    bool IsHighlightableCapture(std::string_view captureName) {
        return !captureName.empty() && captureName.front() != '_' && captureName != "spell" && captureName != "nospell";
    }

    // Org-mode syntax-highlighting follow-up: cyclic heading-level color
    // from a headline's own star count, shared by OrgMode()'s custom
    // HighlightFunction below. Cycles every 3 (real Org itself cycles
    // through 8 level faces; curated down here, see SyntaxClass::
    // HeadlineLevel1's own doc comment in Mode.h).
    SyntaxClass HeadlineLevelForStarCount(std::size_t starCount) {
        static constexpr SyntaxClass kLevels[] = {SyntaxClass::HeadlineLevel1, SyntaxClass::HeadlineLevel2,
                                                  SyntaxClass::HeadlineLevel3};
        return kLevels[(starCount - 1) % (sizeof(kLevels) / sizeof(kLevels[0]))];
    }

} // namespace

Mode FundamentalMode() {
    return Mode{.name = "fundamental-mode", .keymap = Keymap(), .highlight = HighlightFunction()};
}

Mode TreeSitterModeFromLanguage(std::string name, const treesitter::Language& language, std::string_view querySource,
                                std::string_view foldQuerySource) {
    const auto parser = std::make_shared<treesitter::Parser>(language);
    const auto query  = std::make_shared<treesitter::Query>(language, querySource);

    // Shared between highlight and fold below (generic-code-folding
    // follow-up) so that a single Paint() cycle calling both against the
    // exact same buffer text -- the common case, since BufferView recomputes
    // each independently but only when content actually changed -- parses
    // only once, not twice. Not a preemptive optimization: two genuinely
    // independent full reparses regressed a real [Performance] test
    // (JsonMode's highlighting-stays-fast test) once JsonMode also gained a
    // real fold query, under -DNED_ENABLE_SANITIZERS=ON's heavier
    // instrumentation -- caught by that test, not assumed. lastText is a
    // real owned copy (not a string_view into the caller's buffer, whose
    // lifetime this closure can't assume anything about past the call that
    // handed it over).
    struct SharedParse {
        std::string                     lastText;
        std::optional<treesitter::Tree> lastTree;
    };
    const auto sharedParse = std::make_shared<SharedParse>();

    // parser/query/sharedParse are captured by shared_ptr, not by value --
    // Parser/Query/Tree are move-only (own a real tree-sitter handle each),
    // but Mode itself needs to stay a plain, freely-copyable value type the
    // way every existing caller (tests, main.cpp) already treats it; a
    // std::function's captured state only needs to be copyable, not the
    // captured objects themselves, so this preserves that contract without
    // Mode having to change shape.
    HighlightFunction highlight = [parser, query, sharedParse](std::string_view bufferText) -> std::vector<HighlightSpan> {
        if (!sharedParse->lastTree.has_value() || sharedParse->lastText != bufferText) {
            sharedParse->lastTree = parser->Parse(bufferText);
            sharedParse->lastText.assign(bufferText);
        }
        const treesitter::Tree& tree = *sharedParse->lastTree;
        if (tree.IsNull()) {
            return {};
        }

        std::vector<HighlightSpan> spans;
        for (const treesitter::QueryCapture& capture : query->Captures(tree.RootNode(), bufferText)) {
            if (!IsHighlightableCapture(capture.name)) {
                continue;
            }
            spans.push_back(HighlightSpan{
                .startByte   = capture.startByte,
                .endByte     = capture.endByte,
                .syntaxClass = SyntaxClassForCapture(capture.name),
            });
        }
        return spans;
    };

    // generic-code-folding follow-up: a second Query against the same
    // parser -- shares sharedParse's cached Tree with highlight above (see
    // its own doc comment). Only built when a fold query source was
    // actually given; otherwise mode.fold stays a default-constructed,
    // empty std::function, which is exactly the "no fold support" signal
    // BufferView checks for.
    FoldFunction fold;
    if (!foldQuerySource.empty()) {
        const auto foldQuery = std::make_shared<treesitter::Query>(language, foldQuerySource);
        fold = [parser, foldQuery, sharedParse](std::string_view bufferText) -> std::vector<std::pair<std::size_t, std::size_t>> {
            if (!sharedParse->lastTree.has_value() || sharedParse->lastText != bufferText) {
                sharedParse->lastTree = parser->Parse(bufferText);
                sharedParse->lastText.assign(bufferText);
            }
            const treesitter::Tree& tree = *sharedParse->lastTree;
            if (tree.IsNull()) {
                return {};
            }

            std::vector<std::pair<std::size_t, std::size_t>> ranges;
            for (const treesitter::QueryCapture& capture : foldQuery->Captures(tree.RootNode(), bufferText)) {
                if (capture.name == "fold") {
                    ranges.emplace_back(capture.startByte, capture.endByte);
                }
            }
            std::sort(ranges.begin(), ranges.end());
            return ranges;
        };
    }

    return Mode{.name = std::move(name), .keymap = Keymap(), .highlight = std::move(highlight), .fold = std::move(fold)};
}

Mode TreeSitterMode(std::string name, std::string_view languageName, const char* querySource,
                    const char* foldQuerySource) {
    const auto language = treesitter::LanguageByName(languageName);
    // Every languageName this is called with (below) names a grammar
    // Languages.cpp always bundles -- if this ever fires it's a build-time
    // bundling regression (a Mode function calling this with a typo'd or
    // no-longer-bundled name), not a runtime condition to recover from
    // gracefully.
    return TreeSitterModeFromLanguage(std::move(name), *language, querySource,
                                      foldQuerySource != nullptr ? std::string_view(foldQuerySource) : std::string_view());
}

Mode JanetMode() {
    return TreeSitterMode("janet-mode", "janet", treesitter::queries::kJanet);
}

Mode JsonMode() {
    return TreeSitterMode("json-mode", "json", treesitter::queries::kJson, treesitter::queries::kJsonFolds);
}

Mode CMode() {
    return TreeSitterMode("c-mode", "c", treesitter::queries::kC, treesitter::queries::kCFolds);
}

Mode CppMode() {
    return TreeSitterMode("cpp-mode", "cpp", treesitter::queries::kCpp, treesitter::queries::kCppFolds);
}

Mode PhpMode() {
    return TreeSitterMode("php-mode", "php", treesitter::queries::kPhp);
}

Mode JavaScriptMode() {
    return TreeSitterMode("javascript-mode", "javascript", treesitter::queries::kJavaScript,
                          treesitter::queries::kJavaScriptFolds);
}

Mode TypeScriptMode() {
    return TreeSitterMode("typescript-mode", "typescript", treesitter::queries::kTypeScript,
                          treesitter::queries::kTypeScriptFolds);
}

Mode TsxMode() {
    return TreeSitterMode("tsx-mode", "tsx", treesitter::queries::kTypeScript, treesitter::queries::kTypeScriptFolds);
}

Mode HtmlMode() {
    return TreeSitterMode("html-mode", "html", treesitter::queries::kHtml);
}

Mode CssMode() {
    return TreeSitterMode("css-mode", "css", treesitter::queries::kCss);
}

Mode PythonMode() {
    return TreeSitterMode("python-mode", "python", treesitter::queries::kPython, treesitter::queries::kPythonFolds);
}

Mode BashMode() {
    return TreeSitterMode("bash-mode", "bash", treesitter::queries::kBash);
}

Mode MarkdownMode() {
    Mode mode = TreeSitterMode("markdown-mode", "markdown", treesitter::queries::kMarkdown);
    // Tables follow-up: the second Mode in this codebase to ever construct
    // a non-empty Keymap (OrgMode() was the first, see its own doc comment
    // below) -- no shadowing risk, Markdown has no fold-cycle or other TAB
    // use to compete with.
    mode.keymap.Bind(ParseKeySequence("TAB"), "markdown-table-align");
    return mode;
}

Mode OrgMode() {
    Keymap keymap;
    keymap.Bind(ParseKeySequence("C-c C-t"), "org-cycle-todo");
    keymap.Bind(ParseKeySequence("C-c C-p"), "org-cycle-priority"); // deliberately shadows toggle-project-sidebar; see Mode.h
    keymap.Bind(ParseKeySequence("C-c C-c"), "org-toggle-checkbox");
    // Real Org's own TAB binding -- unbound in both the global keymap
    // (self-insert only covers printable ASCII 0x20-0x7E, which excludes
    // Tab) and here before this, so no shadowing, unlike C-c C-p above.
    keymap.Bind(ParseKeySequence("TAB"), "org-cycle");
    keymap.Bind(ParseKeySequence("C-c C-q"), "org-set-tags"); // real Org's own binding
    // Links follow-up: real Org's own org-open-at-point binding --
    // deliberately shadows the global find-scratch binding while an
    // org-mode buffer is active, see this function's own doc comment in
    // Mode.h.
    keymap.Bind(ParseKeySequence("C-c C-o"), "open-link-at-point");

    // Org-mode syntax-highlighting follow-up: NOT built via
    // TreeSitterMode()/TreeSitterModeFromLanguage() -- see this function's
    // own doc comment in Mode.h for why. "org" is Ned's own forked grammar
    // (Languages.cpp), never absent from the bundled build, matching
    // TreeSitterMode's own "build-time bundling regression, not a runtime
    // condition" convention for the same kind of lookup.
    const auto orgLanguage = treesitter::LanguageByName("org");
    const auto parser      = std::make_shared<treesitter::Parser>(*orgLanguage);
    const auto query       = std::make_shared<treesitter::Query>(*orgLanguage, treesitter::queries::kOrg);

    HighlightFunction highlight = [parser, query](std::string_view bufferText) -> std::vector<HighlightSpan> {
        const treesitter::Tree tree = parser->Parse(bufferText);
        if (tree.IsNull()) {
            return {};
        }
        const std::vector<treesitter::QueryCapture> captures = query->Captures(tree.RootNode(), bufferText);

        // Three passes, concatenated in this order so a later, narrower
        // span visually wins over an earlier, broader one via
        // HighlightSpan's own documented "later wins" rule -- e.g. a tag or
        // a TODO/DONE keyword sitting inside a HeadlineLevelN span that
        // covers the whole headline line.
        std::vector<HighlightSpan> spans;

        // Pass 1: "org.headline.stars" -> cyclic heading level, covering
        // the WHOLE headline line (stars through its own end-of-line, not
        // just the stars themselves) -- a headline reads as one visual
        // unit, matching real Org's own convention.
        for (const treesitter::QueryCapture& capture : captures) {
            if (capture.name != "org.headline.stars") {
                continue;
            }
            const std::size_t starCount  = capture.endByte - capture.startByte; // stars are literal '*' bytes, one byte each
            const std::size_t newlinePos = bufferText.find('\n', capture.startByte);
            const std::size_t lineEnd    = (newlinePos == std::string_view::npos) ? bufferText.size() : newlinePos;
            spans.push_back(HighlightSpan{
                .startByte   = capture.startByte,
                .endByte     = lineEnd,
                .syntaxClass = HeadlineLevelForStarCount(starCount),
            });
        }

        // Pass 2: "org.keyword.candidate" -> TodoKeyword/DoneKeyword, but
        // only for an EXACT match against org::TodoKeywords()'s own
        // configured list -- a headline whose first word merely isn't a
        // configured keyword gets no span here at all, falling through to
        // Default, the same "exact match or nothing" rule Org.cpp's own
        // ParseHeadlineLine already applies. The LAST configured keyword is
        // treated as the "done" state, everything earlier as "still open"
        // -- the standard single-sequence Org convention, no new config
        // surface needed.
        const std::vector<std::string>& todoKeywords = org::TodoKeywords();
        for (const treesitter::QueryCapture& capture : captures) {
            if (capture.name != "org.keyword.candidate") {
                continue;
            }
            const std::string_view candidate = bufferText.substr(capture.startByte, capture.endByte - capture.startByte);
            for (std::size_t i = 0; i < todoKeywords.size(); ++i) {
                if (todoKeywords[i] == candidate) {
                    spans.push_back(HighlightSpan{
                        .startByte   = capture.startByte,
                        .endByte     = capture.endByte,
                        .syntaxClass = (i + 1 == todoKeywords.size()) ? SyntaxClass::DoneKeyword : SyntaxClass::TodoKeyword,
                    });
                    break;
                }
            }
        }

        // Pass 3: everything else, through the same shared, generic
        // CaptureTable()/SyntaxClassForCapture() mapping every other
        // bundled grammar's Mode already uses.
        for (const treesitter::QueryCapture& capture : captures) {
            if (capture.name == "org.headline.stars" || capture.name == "org.keyword.candidate") {
                continue;
            }
            spans.push_back(HighlightSpan{
                .startByte   = capture.startByte,
                .endByte     = capture.endByte,
                .syntaxClass = SyntaxClassForCapture(capture.name),
            });
        }

        return spans;
    };

    return Mode{.name = "org-mode", .keymap = std::move(keymap), .highlight = std::move(highlight)};
}

} // namespace ned::editor
