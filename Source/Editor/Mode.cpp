#include "Mode.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "Key.h"
#include "Link.h"
#include "Org.h"
#include "SyntaxTheme.h"
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

            // exhaustive-highlighting follow-up: entries closing the gaps a
            // full enumeration of every bundled grammar's real query found
            // (17 queries, 87 distinct names -- see ROADMAP.md's entry).
            // First, names the ancestor-stripping fallback already resolved
            // acceptably, promoted to explicit entries so they're
            // documented, individually remappable defaults rather than
            // accidents of the walk:
            {"keyword.exception", SyntaxClass::ControlKeyword}, // throw/try/catch ARE control flow, not plain keywords (stripping gave Keyword)
            {"keyword.directive", SyntaxClass::Keyword},        // preprocessor directives -- same class "preproc" above already picked
            {"keyword.directive.define", SyntaxClass::Keyword},
            {"keyword.coroutine", SyntaxClass::Keyword},            // async/await/co_await -- most themes render these as plain keywords
            {"keyword.conditional.ternary", SyntaxClass::Operator}, // "?"/":" read as operators, not as an if-keyword's color
            {"module.builtin", SyntaxClass::Namespace},
            {"tag.error", SyntaxClass::Tag}, // no Error class exists; per-capture styling can redden it now
            // string.special.symbol is a Lisp symbol/keyword (:foo in
            // Clojure) -- Constant matches how symbol-like atoms
            // conventionally render (Ruby symbols, Elixir atoms), where
            // stripping to string.special -> String would paint half a
            // Clojure buffer string-colored.
            {"string.special.symbol", SyntaxClass::Constant},
            // CSS at-rule captures from tree-sitter-css's own query
            // (@media/@import/@keyframes/@supports/@charset each capture
            // under a bare name matching the at-rule) -- previously fell
            // all the way to Default, the enumeration's only genuine
            // every-day-visible misses.
            {"media", SyntaxClass::Keyword},
            {"import", SyntaxClass::Keyword},
            {"keyframes", SyntaxClass::Keyword},
            {"supports", SyntaxClass::Keyword},
            {"charset", SyntaxClass::Keyword},
            // Interpolation/substitution context (bash $(...) content, JS
            // template-literal ${...}, Python f-string braces' content).
            // Default is the *correct* class -- the code inside shouldn't
            // inherit the enclosing string's color -- but as an explicit
            // choice here, not a fallthrough that reads like a gap.
            {"embedded", SyntaxClass::Default},

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

            // Markdown-highlighting follow-up -- real capture names from
            // both tree-sitter-markdown's and tree-sitter-markdown-inline's
            // own vendored highlights.scm (checked directly, not invented).
            // text.title has a CaptureTable entry purely as a defensive
            // fallback -- MarkdownMode()'s own dedicated heading-level pass
            // (Mode.cpp) always overrides it with a real HeadlineLevel1/2/3
            // span per the "later capture wins" overlap rule, since a plain
            // query capture can't tell H1 from H6. text.strong/text.emphasis
            // reuse Org's own generic Strong/Emphasis classes -- bold text
            // is bold text regardless of source language. text.literal
            // (code spans, fenced/indented code blocks) reuses String, the
            // same "verbatim/code is close in spirit to String" precedent
            // Org's own doc comment already establishes.
            {"text.title", SyntaxClass::HeadlineLevel1},
            {"text.strong", SyntaxClass::Strong},
            {"text.emphasis", SyntaxClass::Emphasis},
            {"text.literal", SyntaxClass::String},
            {"text.uri", SyntaxClass::Link},
            {"text.reference", SyntaxClass::Link},
            // Deliberately NOT a "punctuation.special" entry here -- that
            // capture name is already shared by other bundled grammars
            // above (line 104, mapped to the generic Punctuation class),
            // and markdown's own use of it (list markers/thematic
            // break/heading markers/blockquote marker) wants the distinct,
            // dimmed MarkupMarker treatment instead -- CaptureTable has no
            // per-language scoping, so MarkdownMode()'s own highlight
            // closure special-cases this one capture name directly rather
            // than repointing it here and silently changing every other
            // language's punctuation.special color too.
            //
            // Ned's own addition -- the grammar has a real "strikethrough"
            // node (GFM extension) but the vendored inline query doesn't
            // capture it; MarkdownMode() appends a small supplemental
            // pattern of its own using this capture name (see Mode.cpp).
            {"text.strikethrough", SyntaxClass::Strikethrough},
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
            // A user remap (ned/set-capture-class, SyntaxTheme.h) wins over
            // the built-in table at every dotted level, so remapping a broad
            // name ("keyword") also re-bases every unlisted specific name
            // that would have fallen back to it. Parse-time path (per
            // capture per reparse), not the per-codepoint render path, so
            // the store's mutex lookup per level is fine here.
            if (const auto remapped = SyntaxClassOverrideForCapture(captureName)) {
                return *remapped;
            }
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
    // including ones not yet vendored here. Markdown-highlighting follow-up:
    // "none" joins this filter for the same reason -- tree-sitter-markdown's
    // own highlights.scm captures a fenced code block's inner content
    // "(code_fence_content) @none" (deliberately left unhighlighted upstream,
    // meant for a nested-language injection Ned doesn't implement), and
    // without filtering it out it would resolve to Default and clobber the
    // parent fenced_code_block's own real "text.literal" (String) span.
    bool IsHighlightableCapture(std::string_view captureName) {
        return !captureName.empty() && captureName.front() != '_' && captureName != "spell" && captureName != "nospell" &&
               captureName != "none";
    }

    // exhaustive-highlighting follow-up: collects query captures into
    // HighlightSpans, resolving one specific hazard the raw append never
    // could: two patterns capturing the *exact same node* under different
    // names (tree-sitter-json's own '(pair key: (_) @string.special.key)'
    // plus '(string) @string' is the canonical case -- one key node, two
    // equal-range captures). The documented "later span wins" render rule
    // assumes later means more-nested, which equal ranges from separate
    // patterns break: whichever pattern the query file happens to list
    // second wins, and json lists the *generic* one second. Invisible while
    // spans only carried a SyntaxClass (both resolve to String); a real,
    // live-smoke-test-caught bug once captureId made the difference
    // stylable (ned/set-capture-foreground "string.special.key" silently
    // did nothing). For an equal-range collision, the more specific capture
    // name (more dotted segments) wins regardless of pattern order --
    // tree-sitter/Neovim's own most-to-least-specific naming convention,
    // the same reasoning SyntaxClassForCapture's ancestor walk already
    // leans on; equal specificity keeps the later one (the pre-existing
    // rule). Distinct-range overlaps are untouched -- the render-time
    // later-wins rule still handles genuine nesting.
    class SpanCollector {
      public:
        void Add(std::string_view captureName, std::size_t startByte, std::size_t endByte, SyntaxClass syntaxClass) {
            const auto range = std::make_pair(startByte, endByte);
            const int  dots  = static_cast<int>(std::count(captureName.begin(), captureName.end(), '.'));
            if (const auto it = byRange_.find(range); it != byRange_.end()) {
                if (dots < specificity_[it->second]) {
                    return; // a more specific capture already holds this exact range
                }
                spans_[it->second]       = HighlightSpan{.startByte   = startByte,
                                                         .endByte     = endByte,
                                                         .syntaxClass = syntaxClass,
                                                         .captureId   = InternCaptureName(captureName)};
                specificity_[it->second] = dots;
                return;
            }
            byRange_.emplace(range, spans_.size());
            spans_.push_back(HighlightSpan{.startByte   = startByte,
                                           .endByte     = endByte,
                                           .syntaxClass = syntaxClass,
                                           .captureId   = InternCaptureName(captureName)});
            specificity_.push_back(dots);
        }

        [[nodiscard]] std::vector<HighlightSpan> Take() {
            return std::move(spans_);
        }

      private:
        std::vector<HighlightSpan>                                 spans_;
        std::vector<int>                                           specificity_;
        std::map<std::pair<std::size_t, std::size_t>, std::size_t> byRange_;
    };

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

    // Markdown-highlighting follow-up. Which atx_h<N>_marker child type
    // (N = 1..6) an atx_heading node has -- a plain query capture can only
    // say "this is A heading", not which level, since every level shares
    // the same "(atx_heading (inline) @text.title)" pattern; the level only
    // shows up as which specific marker child is present.
    std::optional<int> AtxHeadingMarkerLevel(std::string_view childType) {
        static constexpr std::pair<std::string_view, int> kMarkers[] = {
            {"atx_h1_marker", 1},
            {"atx_h2_marker", 2},
            {"atx_h3_marker", 3},
            {"atx_h4_marker", 4},
            {"atx_h5_marker", 5},
            {"atx_h6_marker", 6},
        };
        for (const auto& [name, level] : kMarkers) {
            if (childType == name) {
                return level;
            }
        }
        return std::nullopt;
    }

    // Markdown-highlighting follow-up. Walks the real parsed tree (not just
    // query captures -- see AtxHeadingMarkerLevel's own doc comment) for the
    // handful of constructs a plain query can't express: ATX/setext heading
    // levels (whole-node span, matching Org's own whole-headline-line
    // convention) and GFM task-list checkboxes (reusing Org's own Checkbox
    // class -- same visual concept). Recurses over every child regardless
    // of match, matching-node checks first then recursing unconditionally
    // -- headings/checkboxes never nest, so this never double-counts.
    void CollectMarkdownStructuralSpans(const treesitter::Node& node, std::vector<HighlightSpan>& spans) {
        const std::string_view type = node.Type();
        if (type == "atx_heading") {
            for (std::size_t i = 0; i < node.ChildCount(); ++i) {
                if (const std::optional<int> level = AtxHeadingMarkerLevel(node.Child(i).Type())) {
                    spans.push_back(HighlightSpan{.startByte   = node.StartByte(),
                                                  .endByte     = node.EndByte(),
                                                  .syntaxClass = HeadlineLevelForStarCount(static_cast<std::size_t>(*level))});
                    break;
                }
            }
        }
        else if (type == "setext_heading") {
            for (std::size_t i = 0; i < node.ChildCount(); ++i) {
                const std::string_view childType = node.Child(i).Type();
                if (childType == "setext_h1_underline" || childType == "setext_h2_underline") {
                    const std::size_t level = (childType == "setext_h1_underline") ? 1 : 2;
                    spans.push_back(HighlightSpan{
                        .startByte = node.StartByte(), .endByte = node.EndByte(), .syntaxClass = HeadlineLevelForStarCount(level)});
                    break;
                }
            }
        }
        else if (type == "task_list_marker_checked" || type == "task_list_marker_unchecked") {
            spans.push_back(HighlightSpan{.startByte = node.StartByte(), .endByte = node.EndByte(), .syntaxClass = SyntaxClass::Checkbox});
        }

        for (std::size_t i = 0; i < node.ChildCount(); ++i) {
            CollectMarkdownStructuralSpans(node.Child(i), spans);
        }
    }

    // Markdown-highlighting follow-up. The actual hand-rolled "injection":
    // markdown's own bold/italic/strikethrough/code-span/link formatting
    // lives entirely in the separate tree-sitter-markdown-inline grammar,
    // never in the block grammar walked by CollectMarkdownStructuralSpans
    // above -- real markdown tooling combines the two via tree-sitter
    // language injection, which Ned's TreeSitter/ wrapper has no generic
    // support for (see CMakeLists.txt's own tree-sitter-markdown-inline
    // entry). Every block-grammar "inline" node is raw text meant for this
    // second grammar; inline nodes don't nest, so finding one ends this
    // branch of the walk rather than recursing further.
    void CollectMarkdownInlineSpans(const treesitter::Node& node, std::string_view bufferText, const treesitter::Parser& inlineParser,
                                    const treesitter::Query& inlineQuery, std::vector<HighlightSpan>& spans) {
        if (node.Type() == "inline") {
            const std::size_t      start      = node.StartByte();
            const std::size_t      end        = node.EndByte();
            const std::string_view inlineText = bufferText.substr(start, end - start);
            const treesitter::Tree inlineTree = inlineParser.Parse(inlineText);
            if (!inlineTree.IsNull()) {
                // Per-node collector: equal-range double captures can only
                // come from one query run over one inline node (different
                // inline nodes never share a byte range).
                SpanCollector collector;
                for (const treesitter::QueryCapture& capture : inlineQuery.Captures(inlineTree.RootNode(), inlineText)) {
                    if (!IsHighlightableCapture(capture.name)) {
                        continue;
                    }
                    collector.Add(capture.name, start + capture.startByte, start + capture.endByte,
                                  SyntaxClassForCapture(capture.name));
                }
                for (const HighlightSpan& span : collector.Take()) {
                    spans.push_back(span);
                }
            }
            return;
        }
        for (std::size_t i = 0; i < node.ChildCount(); ++i) {
            CollectMarkdownInlineSpans(node.Child(i), bufferText, inlineParser, inlineQuery, spans);
        }
    }

} // namespace

std::vector<std::string> BuiltinCaptureNames() {
    std::vector<std::string> names;
    names.reserve(CaptureTable().size());
    for (const auto& [name, cls] : CaptureTable()) {
        names.emplace_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

Mode FundamentalMode() {
    return Mode{.name = "fundamental-mode", .keymap = Keymap(), .highlight = HighlightFunction()};
}

std::string LanguageKeyForMode(const Mode& mode) {
    constexpr std::string_view kSuffix = "-mode";
    if (mode.name.size() > kSuffix.size() && mode.name.ends_with(kSuffix)) {
        return mode.name.substr(0, mode.name.size() - kSuffix.size());
    }
    return mode.name;
}

Mode TreeSitterModeFromLanguage(std::string name, const treesitter::Language& language, std::string_view querySource,
                                std::string_view foldQuerySource, std::string_view importQuerySource) {
    const auto parser = std::make_shared<treesitter::Parser>(language);

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
    // Only built when a highlight query source was actually given -- some
    // real, bundled-elsewhere grammars have no highlights.scm at all (only a
    // folds/locals/tags query), the same "not every language has one" fact
    // already true of foldQuerySource below. An empty querySource leaves
    // mode.highlight a default-constructed, empty std::function, the exact
    // "no highlighting" signal BufferView already checks for.
    HighlightFunction highlight;
    if (!querySource.empty()) {
        const auto query = std::make_shared<treesitter::Query>(language, querySource);
        highlight        = [parser, query, sharedParse](std::string_view bufferText) -> std::vector<HighlightSpan> {
            if (!sharedParse->lastTree.has_value() || sharedParse->lastText != bufferText) {
                sharedParse->lastTree = parser->Parse(bufferText);
                sharedParse->lastText.assign(bufferText);
            }
            const treesitter::Tree& tree = *sharedParse->lastTree;
            if (tree.IsNull()) {
                return {};
            }

            SpanCollector collector;
            for (const treesitter::QueryCapture& capture : query->Captures(tree.RootNode(), bufferText)) {
                if (!IsHighlightableCapture(capture.name)) {
                    continue;
                }
                collector.Add(capture.name, capture.startByte, capture.endByte, SyntaxClassForCapture(capture.name));
            }
            return collector.Take();
        };
    }

    // generic-code-folding follow-up: a second Query against the same
    // parser -- shares sharedParse's cached Tree with highlight above (see
    // its own doc comment). Only built when a fold query source was
    // actually given; otherwise mode.fold stays a default-constructed,
    // empty std::function, which is exactly the "no fold support" signal
    // BufferView checks for.
    FoldFunction fold;
    if (!foldQuerySource.empty()) {
        const auto foldQuery = std::make_shared<treesitter::Query>(language, foldQuerySource);
        fold                 = [parser, foldQuery, sharedParse](std::string_view bufferText) -> std::vector<std::pair<std::size_t, std::size_t>> {
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

    // structural-selection-expansion follow-up: a third closure sharing the
    // same parser/sharedParse as highlight/fold above, so an expand-selection
    // keypress that happens to land on the same Paint() cycle as a
    // highlight/fold recompute (the common case -- nothing else invalidates
    // sharedParse's cache) doesn't trigger a third redundant full reparse.
    // Named-node-only walk (NamedDescendantForByteRange/Parent, not
    // Child/ChildCount, which are unnamed-inclusive) -- this is what keeps a
    // lone punctuation token (";", "(", ...) from ever being its own
    // expansion step, with no per-language "skip list" needed.
    ExpandSelectionFunction expandSelection = [parser, sharedParse](std::string_view bufferText, std::size_t startByte,
                                                                    std::size_t endByte) -> std::optional<std::pair<std::size_t, std::size_t>> {
        if (!sharedParse->lastTree.has_value() || sharedParse->lastText != bufferText) {
            sharedParse->lastTree = parser->Parse(bufferText);
            sharedParse->lastText.assign(bufferText);
        }
        const treesitter::Tree& tree = *sharedParse->lastTree;
        if (tree.IsNull()) {
            return std::nullopt;
        }

        treesitter::Node node = tree.RootNode().NamedDescendantForByteRange(startByte, endByte);
        while (!node.IsNull() && node.StartByte() == startByte && node.EndByte() == endByte) {
            node = node.Parent();
        }
        if (node.IsNull()) {
            return std::nullopt; // already at the root -- nothing bigger to expand to
        }
        return std::make_pair(node.StartByte(), node.EndByte());
    };

    // Emacs-keymap-round-2 follow-up: a fourth closure sharing the same
    // parser/sharedParse as highlight/fold/expandSelection above, for the
    // same "don't trigger a redundant full reparse on the same Paint()
    // cycle" reason. Named-node-only, same rationale as expandSelection.
    // `at` is the smallest named node whose span contains p (after the
    // whitespace skip). Three cases:
    //  - p sits exactly at at's own start/end: at itself is the sexp to
    //    move over.
    //  - p sits in the gap *between* two of at's own named children (e.g.
    //    right after a list's separating comma) -- at is a container here,
    //    not a token, so the answer is the next/previous of *at's own
    //    children*, not at's sibling (at's sibling is a whole other
    //    container one level up, way too big a jump).
    //  - p sits strictly inside an unnamed leaf token at is built from
    //    (rare -- at has no matching child either direction): climb at's
    //    own ancestor chain for the next/previous named sibling instead,
    //    since a sibling at a shallower level is still "the next sexp"
    //    once the current level runs out of children/siblings.
    // Falls back to at's own start/end if nothing is found at any level
    // (e.g. p is inside the last child of the outermost node) -- still a
    // real move, just to the edge of the innermost containing node rather
    // than further out.
    SexpMotionFunction sexpMotion = [parser, sharedParse](std::string_view bufferText, std::size_t point,
                                                          bool forward) -> std::optional<std::size_t> {
        if (!sharedParse->lastTree.has_value() || sharedParse->lastText != bufferText) {
            sharedParse->lastTree = parser->Parse(bufferText);
            sharedParse->lastText.assign(bufferText);
        }
        const treesitter::Tree& tree = *sharedParse->lastTree;
        if (tree.IsNull()) {
            return std::nullopt;
        }

        const auto isAsciiSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };

        if (forward) {
            std::size_t p = point;
            while (p < bufferText.size() && isAsciiSpace(bufferText[p])) {
                ++p;
            }
            if (p >= bufferText.size()) {
                return std::nullopt;
            }
            treesitter::Node at = tree.RootNode().NamedDescendantForByteRange(p, p);
            if (at.IsNull()) {
                return std::nullopt;
            }
            if (at.StartByte() == p) {
                return at.EndByte();
            }
            for (std::size_t i = 0; i < at.ChildCount(); ++i) {
                treesitter::Node child = at.Child(i);
                if (child.IsNamed() && child.StartByte() >= p) {
                    return child.EndByte();
                }
            }
            for (treesitter::Node node = at; !node.IsNull(); node = node.Parent()) {
                treesitter::Node sibling = node.NextNamedSibling();
                if (!sibling.IsNull() && sibling.StartByte() >= p) {
                    return sibling.EndByte();
                }
            }
            return at.EndByte();
        }

        std::size_t p = point;
        while (p > 0 && isAsciiSpace(bufferText[p - 1])) {
            --p;
        }
        if (p == 0) {
            return std::nullopt;
        }
        treesitter::Node at = tree.RootNode().NamedDescendantForByteRange(p - 1, p - 1);
        if (at.IsNull()) {
            return std::nullopt;
        }
        if (at.EndByte() == p) {
            return at.StartByte();
        }
        for (std::size_t i = at.ChildCount(); i > 0; --i) {
            treesitter::Node child = at.Child(i - 1);
            if (child.IsNamed() && child.EndByte() <= p) {
                return child.StartByte();
            }
        }
        for (treesitter::Node node = at; !node.IsNull(); node = node.Parent()) {
            treesitter::Node sibling = node.PrevNamedSibling();
            if (!sibling.IsNull() && sibling.EndByte() <= p) {
                return sibling.StartByte();
            }
        }
        return at.StartByte();
    };

    // import-target-tree-sitter follow-up: a fifth closure sharing the same
    // parser/sharedParse as highlight/fold/expandSelection/sexpMotion above,
    // for the same "don't trigger a redundant full reparse on the same
    // Paint() cycle" reason. Only built when an import query source was
    // actually given; otherwise mode.importTarget stays a default-
    // constructed, empty std::function -- the "no import query configured"
    // signal BufferView checks for before falling back to Editor/Link.h's
    // generic, mode-agnostic detection. No per-language branching here at
    // all -- every language's own *-imports.scm query does the language-
    // specific work by tagging its own captures "import.target"/
    // "import.module"/"import.statement" (see Mode.h's ImportTarget doc
    // comment); this closure only ever looks for those three fixed names.
    ImportTargetFunction importTarget;
    if (!importQuerySource.empty()) {
        const auto importQuery = std::make_shared<treesitter::Query>(language, importQuerySource);
        importTarget = [parser, importQuery, sharedParse](std::string_view bufferText,
                                                           std::size_t      point) -> std::optional<ImportTarget> {
            if (!sharedParse->lastTree.has_value() || sharedParse->lastText != bufferText) {
                sharedParse->lastTree = parser->Parse(bufferText);
                sharedParse->lastText.assign(bufferText);
            }
            const treesitter::Tree& tree = *sharedParse->lastTree;
            if (tree.IsNull()) {
                return std::nullopt;
            }

            struct TargetCapture {
                std::size_t targetStart, targetEnd;
                bool        isModule;
            };
            std::vector<TargetCapture>                        targets;
            std::vector<std::pair<std::size_t, std::size_t>> statements;
            for (const treesitter::QueryCapture& capture : importQuery->Captures(tree.RootNode(), bufferText)) {
                if (capture.name == "import.statement") {
                    statements.emplace_back(capture.startByte, capture.endByte);
                }
                else if (capture.name == "import.target") {
                    targets.push_back({capture.startByte, capture.endByte, false});
                }
                else if (capture.name == "import.module") {
                    targets.push_back({capture.startByte, capture.endByte, true});
                }
            }

            // The resolvable range for a target is the tightest
            // "import.statement" range enclosing it (so point anywhere in
            // e.g. "from foo.bar import baz" resolves, not just on "foo.bar"
            // itself), falling back to the target's own range when no
            // enclosing statement capture exists at all (e.g. Python's plain
            // "import a.b, c.d", where each name's own range is already the
            // most specific answer).
            const TargetCapture* best      = nullptr;
            std::size_t          bestStart = 0;
            std::size_t          bestEnd   = 0;
            for (const TargetCapture& target : targets) {
                std::size_t rangeStart    = target.targetStart;
                std::size_t rangeEnd      = target.targetEnd;
                bool        haveStatement = false;
                for (const auto& [statementStart, statementEnd] : statements) {
                    if (statementStart <= target.targetStart && target.targetEnd <= statementEnd &&
                        (!haveStatement || (statementEnd - statementStart) < (rangeEnd - rangeStart))) {
                        rangeStart    = statementStart;
                        rangeEnd      = statementEnd;
                        haveStatement = true;
                    }
                }
                if (rangeStart <= point && point <= rangeEnd && (!best || (rangeEnd - rangeStart) < (bestEnd - bestStart))) {
                    best      = &target;
                    bestStart = rangeStart;
                    bestEnd   = rangeEnd;
                }
            }
            if (!best) {
                return std::nullopt;
            }

            std::string text(bufferText.substr(best->targetStart, best->targetEnd - best->targetStart));
            if (!best->isModule) {
                text = std::string(link::StripDelimiters(text));
            }
            return ImportTarget{
                .target = std::move(text), .isModulePath = best->isModule, .startByte = bestStart, .endByte = bestEnd};
        };
    }

    return Mode{.name            = std::move(name),
                .keymap          = Keymap(),
                .highlight       = std::move(highlight),
                .fold            = std::move(fold),
                .expandSelection = std::move(expandSelection),
                .sexpMotion      = std::move(sexpMotion),
                .importTarget    = std::move(importTarget)};
}

Mode TreeSitterMode(std::string name, std::string_view languageName, const char* querySource,
                    const char* foldQuerySource, const char* importQuerySource) {
    const auto language = treesitter::LanguageByName(languageName);
    // Every languageName this is called with (below) names a grammar
    // Languages.cpp always bundles -- if this ever fires it's a build-time
    // bundling regression (a Mode function calling this with a typo'd or
    // no-longer-bundled name), not a runtime condition to recover from
    // gracefully.
    return TreeSitterModeFromLanguage(std::move(name), *language, querySource,
                                      foldQuerySource != nullptr ? std::string_view(foldQuerySource) : std::string_view(),
                                      importQuerySource != nullptr ? std::string_view(importQuerySource) : std::string_view());
}

Mode JanetMode() {
    Mode mode = TreeSitterMode("janet-mode", "janet", treesitter::queries::kJanet, nullptr, treesitter::queries::kJanetImports);
    mode.lineCommentPrefix = ";"; // Lisp-family convention
    return mode;
}

Mode JsonMode() {
    // No lineCommentPrefix -- JSON has no comment syntax at all, real or
    // otherwise; toggle-line-comment correctly reports nothing configured
    // rather than inserting something that would make the file invalid JSON.
    return TreeSitterMode("json-mode", "json", treesitter::queries::kJson, treesitter::queries::kJsonFolds);
}

Mode CMode() {
    Mode mode = TreeSitterMode("c-mode", "c", treesitter::queries::kC, treesitter::queries::kCFolds, treesitter::queries::kCImports);
    mode.lineCommentPrefix = "//";
    return mode;
}

Mode CppMode() {
    Mode mode = TreeSitterMode("cpp-mode", "cpp", treesitter::queries::kCpp, treesitter::queries::kCppFolds,
                               treesitter::queries::kCImports);
    mode.lineCommentPrefix = "//";
    return mode;
}

Mode PhpMode() {
    Mode mode = TreeSitterMode("php-mode", "php", treesitter::queries::kPhp, nullptr, treesitter::queries::kPhpImports);
    mode.lineCommentPrefix = "//";
    return mode;
}

Mode JavaScriptMode() {
    Mode mode              = TreeSitterMode("javascript-mode", "javascript", treesitter::queries::kJavaScript,
                                            treesitter::queries::kJavaScriptFolds, treesitter::queries::kJavaScriptImports);
    mode.lineCommentPrefix = "//";
    return mode;
}

Mode TypeScriptMode() {
    Mode mode              = TreeSitterMode("typescript-mode", "typescript", treesitter::queries::kTypeScript,
                                            treesitter::queries::kTypeScriptFolds, treesitter::queries::kTypeScriptImports);
    mode.lineCommentPrefix = "//";
    return mode;
}

Mode TsxMode() {
    Mode mode              = TreeSitterMode("tsx-mode", "tsx", treesitter::queries::kTypeScript, treesitter::queries::kTypeScriptFolds,
                                            treesitter::queries::kTypeScriptImports);
    mode.lineCommentPrefix = "//";
    return mode;
}

Mode HtmlMode() {
    // No lineCommentPrefix -- HTML only has block comments (<!-- -->), no
    // single-line comment token to toggle per line.
    return TreeSitterMode("html-mode", "html", treesitter::queries::kHtml);
}

Mode CssMode() {
    // No lineCommentPrefix -- same reasoning as HtmlMode, CSS only has
    // block comments (/* */).
    return TreeSitterMode("css-mode", "css", treesitter::queries::kCss, nullptr, treesitter::queries::kCssImports);
}

Mode PythonMode() {
    Mode mode              = TreeSitterMode("python-mode", "python", treesitter::queries::kPython, treesitter::queries::kPythonFolds,
                                            treesitter::queries::kPythonImports);
    mode.lineCommentPrefix = "#";
    return mode;
}

Mode BashMode() {
    Mode mode = TreeSitterMode("bash-mode", "bash", treesitter::queries::kBash, nullptr, treesitter::queries::kBashImports);
    mode.lineCommentPrefix = "#";
    return mode;
}

Mode YamlMode() {
    Mode mode              = TreeSitterMode("yaml-mode", "yaml", treesitter::queries::kYaml);
    mode.lineCommentPrefix = "#";
    return mode;
}

Mode TomlMode() {
    Mode mode              = TreeSitterMode("toml-mode", "toml", treesitter::queries::kToml);
    mode.lineCommentPrefix = "#";
    return mode;
}

Mode ClojureMode() {
    Mode mode = TreeSitterMode("clojure-mode", "clojure", treesitter::queries::kClojure, treesitter::queries::kClojureFolds,
                               treesitter::queries::kClojureImports);
    mode.lineCommentPrefix = ";"; // Lisp-family convention, same as JanetMode
    return mode;
}

Mode JankMode() {
    // Same grammar and query as ClojureMode, distinct name -- see Mode.h.
    Mode mode = TreeSitterMode("jank-mode", "clojure", treesitter::queries::kClojure, treesitter::queries::kClojureFolds,
                               treesitter::queries::kClojureImports);
    mode.lineCommentPrefix = ";";
    return mode;
}

Mode MarkdownMode() {
    Mode mode = TreeSitterMode("markdown-mode", "markdown", treesitter::queries::kMarkdown);
    // Tables follow-up: the second Mode in this codebase to ever construct
    // a non-empty Keymap (OrgMode() was the first, see its own doc comment
    // below) -- no shadowing risk, Markdown has no fold-cycle or other TAB
    // use to compete with.
    mode.keymap.Bind(ParseKeySequence("TAB"), "markdown-table-align");
    // No lineCommentPrefix -- Markdown (unlike Org, see OrgMode() below)
    // has no native comment-line convention of its own to toggle.
    // line-wrap follow-up: prose benefits far more from wrapping at word
    // boundaries than from horizontal scrolling -- see WrapOverrides.h for
    // how a user overrides this per file if they'd rather not.
    mode.wrapLines = true;

    // Markdown-highlighting follow-up: mode.highlight built by
    // TreeSitterMode() above gets replaced entirely below -- everything
    // else (.keymap/.wrapLines/.expandSelection, the last built by
    // TreeSitterMode() too) is kept exactly as-is. Own separate
    // parser/query state, own tree walk -- mirrors OrgMode()'s own
    // precedent of bypassing the shared generic highlight path for logic a
    // plain query can't express, here: heading levels, GFM checkboxes, and
    // markdown's own inline-grammar "injection" (see
    // CollectMarkdownInlineSpans's own doc comment for why that's a whole
    // second grammar, not just more query patterns).
    const auto blockLanguage = treesitter::LanguageByName("markdown");
    const auto blockParser   = std::make_shared<treesitter::Parser>(*blockLanguage);
    const auto blockQuery    = std::make_shared<treesitter::Query>(*blockLanguage, treesitter::queries::kMarkdown);

    const auto inlineLanguage = treesitter::LanguageByName("markdown-inline");
    const auto inlineParser   = std::make_shared<treesitter::Parser>(*inlineLanguage);
    // Ned's own addition -- see CaptureTable()'s "text.strikethrough" doc
    // comment for why this one extra pattern is appended in C++ rather than
    // through a CMake-embedded query file.
    const std::string inlineQuerySource = std::string(treesitter::queries::kMarkdownInline) + "\n(strikethrough) @text.strikethrough\n";
    const auto        inlineQuery       = std::make_shared<treesitter::Query>(*inlineLanguage, inlineQuerySource);

    // Same cached-tree-by-text-equality idiom TreeSitterModeFromLanguage
    // uses internally (see its own doc comment) -- this closure does its
    // own full block parse plus one parse per "inline" node, so skipping
    // all of that when bufferText is unchanged since the last call matters
    // here too, not just for the generic path.
    struct SharedParse {
        std::string                     lastText;
        std::optional<treesitter::Tree> lastTree;
    };
    const auto sharedParse = std::make_shared<SharedParse>();

    mode.highlight = [blockParser, blockQuery, inlineParser, inlineQuery,
                      sharedParse](std::string_view bufferText) -> std::vector<HighlightSpan> {
        if (!sharedParse->lastTree.has_value() || sharedParse->lastText != bufferText) {
            sharedParse->lastTree = blockParser->Parse(bufferText);
            sharedParse->lastText.assign(bufferText);
        }
        const treesitter::Tree& tree = *sharedParse->lastTree;
        if (tree.IsNull()) {
            return {};
        }
        const treesitter::Node root = tree.RootNode();

        // Concatenated in this order so a later, narrower span visually
        // wins over an earlier, broader one via HighlightSpan's own
        // documented "later wins" overlap rule -- e.g. bold text inside a
        // heading, or a checkbox inside a list item.
        std::vector<HighlightSpan> spans;

        // Pass 1: block-level query captures through the shared
        // CaptureTable, except "punctuation.special" -- see CaptureTable()'s
        // own doc comment for why that one capture name is special-cased
        // directly to MarkupMarker here instead of through the shared table
        // (it's shared with other bundled grammars, where it means
        // something else).
        SpanCollector blockCollector;
        for (const treesitter::QueryCapture& capture : blockQuery->Captures(root, bufferText)) {
            if (!IsHighlightableCapture(capture.name)) {
                continue;
            }
            const SyntaxClass syntaxClass =
                (capture.name == "punctuation.special") ? SyntaxClass::MarkupMarker : SyntaxClassForCapture(capture.name);
            blockCollector.Add(capture.name, capture.startByte, capture.endByte, syntaxClass);
        }
        spans = blockCollector.Take();

        // Pass 2: heading levels + task-list checkboxes, from walking the
        // real tree rather than query captures.
        CollectMarkdownStructuralSpans(root, spans);

        // Pass 3: markdown's own inline formatting, re-parsed per "inline"
        // node with the separate inline grammar. Appended last so it wins
        // over everything above, including a heading's own whole-line span.
        CollectMarkdownInlineSpans(root, bufferText, *inlineParser, *inlineQuery, spans);

        return spans;
    };

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
    // Tables slice 2: real Org's own table-editing bindings. S-TAB is
    // unbound globally, so no shadowing; M-UP/M-DOWN deliberately shadow
    // the global move-line-up/move-line-down with org-metaup/org-metadown,
    // which fall back to the exact same line move outside a table (see
    // their registration comment in Commands.cpp). Every Meta chord gets
    // the same dual M-/ESC-prefix binding the global keymap uses ("cover
    // both real input shapes" -- see BuildDefaultGlobalKeymap's own
    // comment).
    keymap.Bind(ParseKeySequence("S-TAB"), "org-table-previous-cell");
    keymap.Bind(ParseKeySequence("M-UP"), "org-metaup");
    keymap.Bind(ParseKeySequence("ESC UP"), "org-metaup");
    keymap.Bind(ParseKeySequence("M-DOWN"), "org-metadown");
    keymap.Bind(ParseKeySequence("ESC DOWN"), "org-metadown");
    keymap.Bind(ParseKeySequence("M-S-DOWN"), "org-table-insert-row");
    keymap.Bind(ParseKeySequence("ESC S-DOWN"), "org-table-insert-row");
    keymap.Bind(ParseKeySequence("M-S-UP"), "org-table-kill-row");
    keymap.Bind(ParseKeySequence("ESC S-UP"), "org-table-kill-row");
    keymap.Bind(ParseKeySequence("M-S-RIGHT"), "org-table-insert-column");
    keymap.Bind(ParseKeySequence("ESC S-RIGHT"), "org-table-insert-column");
    keymap.Bind(ParseKeySequence("M-S-LEFT"), "org-table-delete-column");
    keymap.Bind(ParseKeySequence("ESC S-LEFT"), "org-table-delete-column");
    keymap.Bind(ParseKeySequence("M-LEFT"), "org-table-move-column-left");
    keymap.Bind(ParseKeySequence("ESC LEFT"), "org-table-move-column-left");
    keymap.Bind(ParseKeySequence("M-RIGHT"), "org-table-move-column-right");
    keymap.Bind(ParseKeySequence("ESC RIGHT"), "org-table-move-column-right");
    keymap.Bind(ParseKeySequence("C-c -"), "org-table-insert-hline");

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
        SpanCollector genericCollector;
        for (const treesitter::QueryCapture& capture : captures) {
            if (capture.name == "org.headline.stars" || capture.name == "org.keyword.candidate") {
                continue;
            }
            genericCollector.Add(capture.name, capture.startByte, capture.endByte, SyntaxClassForCapture(capture.name));
        }
        for (const HighlightSpan& span : genericCollector.Take()) {
            spans.push_back(span);
        }

        return spans;
    };

    // Real Org's own comment-line convention (org-comment-string's default).
    // line-wrap follow-up: same reasoning as MarkdownMode() -- Org files
    // are prose too.
    return Mode{.name = "org-mode", .keymap = std::move(keymap), .highlight = std::move(highlight), .lineCommentPrefix = "#", .wrapLines = true};
}

} // namespace ned::editor
