#include "Mode.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "AutoPair.h"
#include "Indent.h"
#include "Injection.h"
#include "Key.h"
#include "Link.h"
#include "ModeOverrides.h"
#include "Org.h"
#include "SyntaxTheme.h"
#include "TreeSitter/IncrementalParse.h"
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
            // above (mapped to the generic Punctuation class), and
            // markdown's own use of it (list markers/thematic
            // break/heading markers/blockquote marker) wants the distinct,
            // dimmed MarkupMarker treatment instead. Repointing it here
            // would silently change every other language's punctuation.special
            // color too -- markdown's own default lives in this file's
            // LanguageCaptureTable instead (language-scoped-capture-rules
            // follow-up), consulted only when SyntaxClassForCapture is
            // called with language == "markdown".
            //
            // Ned's own addition -- the grammar has a real "strikethrough"
            // node (GFM extension) but the vendored inline query doesn't
            // capture it; MarkdownMode() appends a small supplemental
            // pattern of its own using this capture name (see Mode.cpp).
            {"text.strikethrough", SyntaxClass::Strikethrough},

            // tree-sitter-xml's own queries/xml/highlights.scm uses the
            // newer nvim-treesitter "markup.*" naming (this grammar's only
            // bundled query to do so -- every other one above still uses the
            // older "text.*" spelling markdown's own query predates). Bare
            // "markup" (plain character data) and "markup.heading" (CDATA's
            // "<![CDATA["/"]]>" delimiters) both fall through the
            // ancestor-stripping walk to Default correctly on their own, no
            // entry needed; only these two want a distinct class instead of
            // that fallthrough, mirroring "text.uri"/"text.literal" above.
            {"markup.link", SyntaxClass::Link},
            {"markup.raw", SyntaxClass::String},
        };
        return table;
    }

    // language-scoped-capture-rules follow-up: built-in defaults for a
    // capture name that means something different in one specific bundled
    // grammar than CaptureTable()'s one shared mapping gives it -- keyed by
    // (language key, capture name), the language key being the same string
    // TreeSitterMode()'s own "-mode"-suffixed convention resolves to
    // (LanguageKeyForMode's exact logic; SyntaxClassForCapture's own callers
    // below pass it explicitly, since each already knows its own language
    // at closure-construction time). Consulted only for the grammar(s) each
    // entry names -- adding an entry here has zero effect on how any other
    // language's use of the same capture name renders, unlike a change to
    // CaptureTable() itself.
    const std::vector<std::tuple<std::string_view, std::string_view, SyntaxClass>>& LanguageCaptureTable() {
        static const std::vector<std::tuple<std::string_view, std::string_view, SyntaxClass>> table = {
            // Markdown's own use of "punctuation.special" (list markers,
            // thematic breaks, heading markers, the blockquote marker) wants
            // the distinct, dimmed MarkupMarker treatment -- every other
            // bundled grammar using the same capture name for ordinary
            // punctuation keeps CaptureTable()'s shared Punctuation mapping
            // above untouched. Used to be a ternary special-case directly in
            // MarkdownMode()'s own highlight closure, bypassing
            // SyntaxClassForCapture (and any user remap) entirely; now a
            // real, user-remappable default like everything else.
            {"markdown", "punctuation.special", SyntaxClass::MarkupMarker},
        };
        return table;
    }

    std::optional<SyntaxClass> BuiltinLanguageClassForCapture(std::string_view language, std::string_view captureName) {
        if (language.empty()) {
            return std::nullopt;
        }
        for (const auto& [tableLanguage, tableCapture, cls] : LanguageCaptureTable()) {
            if (tableLanguage == language && tableCapture == captureName) {
                return cls;
            }
        }
        return std::nullopt;
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
    //
    // language-scoped-capture-rules follow-up: `language` (empty for a
    // caller that doesn't have one, e.g. tests constructing a bare Query
    // directly) is consulted at every dotted level, ahead of the unscoped
    // built-in table -- both the user-remap tier (SyntaxTheme.h's own
    // "<language>/<name>" overload) and the built-in tier
    // (LanguageCaptureTable above) -- so a language-scoped remap re-bases
    // only that one grammar's use of a shared capture name.
    SyntaxClass SyntaxClassForCapture(std::string_view captureName, std::string_view language = {}) {
        const auto& table = CaptureTable();

        while (true) {
            // A user remap (ned/set-capture-class, SyntaxTheme.h) wins over
            // the built-in tables at every dotted level, so remapping a broad
            // name ("keyword") also re-bases every unlisted specific name
            // that would have fallen back to it. Parse-time path (per
            // capture per reparse), not the per-codepoint render path, so
            // the store's mutex lookup per level is fine here.
            if (const auto remapped = SyntaxClassOverrideForCapture(captureName, language)) {
                return *remapped;
            }
            if (const auto builtinLanguageClass = BuiltinLanguageClassForCapture(language, captureName)) {
                return *builtinLanguageClass;
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

    // main-editor-sticky-scroll-markdown follow-up: unlike a tags.scm-driven
    // SymbolKindFunction (which reads its containment range straight off one
    // real AST node), a heading's "belongs under" relationship might look
    // like it needs synthesizing by hand -- ATX/setext headings themselves
    // are flat block-level nodes, no different level from each other in the
    // tree. But tree-sitter-markdown's own grammar (see grammar.js's
    // _section1.._section6 rules) already wraps each heading and everything
    // through the next equal-or-shallower heading in a real "section" node
    // that nests by level (a section's `repeat` only ever admits STRICTLY
    // DEEPER sub-sections as children, never an equal-or-shallower one) --
    // confirmed against the vendored grammar source, not assumed. So a
    // section's own [StartByte, EndByte) is already exactly the synthesized
    // range this would otherwise have to compute by hand from heading
    // levels, and its first token is always its own heading -- no separate
    // level bookkeeping needed at all. Pre-order (parent before children,
    // siblings left-to-right) walk order is what keeps the result already
    // sorted by startByte, Mode::symbolKind's own contract.
    void CollectMarkdownSectionMarkers(const treesitter::Node& node, std::vector<SymbolMarker>& markers) {
        if (node.Type() == "section") {
            markers.push_back(SymbolMarker{
                .startByte = node.StartByte(), .endByte = node.EndByte(), .kind = SymbolKind::Namespace, .name = std::string()});
        }
        for (std::size_t i = 0; i < node.ChildCount(); ++i) {
            CollectMarkdownSectionMarkers(node.Child(i), markers);
        }
    }

    // The shared engine behind Mode::lineInspect's two tiers (see Mode.h's
    // own doc comment): descends from the smallest named node containing the
    // whole line (skips unrelated top-level siblings before ever reaching
    // it), pruning any subtree whose own byte range doesn't overlap the line
    // at all, and collects the byte range of every *named* node overlapping
    // the line whose Type() satisfies `matches` -- natural DFS pre-order,
    // already source order (outermost-first for a node sharing a start byte
    // with its own child, left-to-right across siblings), so no separate
    // sort is needed. Stops recursing (but keeps whatever was already
    // collected) once the cap is hit.
    void CollectLineInspectCandidates(const treesitter::Node& node, std::size_t lineStart, std::size_t lineEnd,
                                      const std::function<bool(std::string_view)>& matches,
                                      std::vector<std::pair<std::size_t, std::size_t>>& out) {
        if (out.size() >= kMaxLineInspectExpressions || node.IsNull() || node.EndByte() <= lineStart || node.StartByte() >= lineEnd) {
            return; // no overlap with the line at all, or already at the cap
        }
        if (node.IsNamed() && node.StartByte() >= lineStart && node.EndByte() <= lineEnd && matches(node.Type())) {
            out.emplace_back(node.StartByte(), node.EndByte());
        }
        for (std::size_t i = 0; i < node.ChildCount() && out.size() < kMaxLineInspectExpressions; ++i) {
            CollectLineInspectCandidates(node.Child(i), lineStart, lineEnd, matches, out);
        }
    }

    // Debugging wishlist (line-inspect follow-up), Tier 2: real, grammar-
    // verified compound-expression node types C and C++ share (confirmed
    // against both vendored grammars' own node-types.json directly, not
    // guessed) -- CMode()/CppMode() override the generic Tier-1
    // identifier-only default with this richer predicate.
    bool IsCLikeExpressionNodeType(std::string_view type) {
        static constexpr std::array<std::string_view, 11> kTypes = {
            "identifier",          "call_expression",     "field_expression",  "subscript_expression",  "binary_expression",
            "unary_expression",    "pointer_expression",   "cast_expression",   "conditional_expression",
            "assignment_expression", "parenthesized_expression",
        };
        return std::find(kTypes.begin(), kTypes.end(), type) != kTypes.end();
    }

    // Builds a Mode::lineInspect closure over its own independent Parser/
    // IncrementalParseCache pair (this runs only on an explicit, on-demand
    // dap-line-inspect invocation, never per-Paint(), so a second parse of
    // the same buffer text alongside highlight/fold/symbolKind's own shared
    // cache costs nothing worth avoiding). `matches` decides which named
    // node types count as candidate sub-expressions -- see the Tier 1
    // (identifier-only) and Tier 2 (CMode/CppMode's richer set) call sites.
    LineInspectFunction BuildLineInspectFunction(const treesitter::Language& language, std::function<bool(std::string_view)> matches) {
        const auto parser     = std::make_shared<treesitter::Parser>(language);
        const auto sharedParse = std::make_shared<treesitter::IncrementalParseCache>();
        return [parser, sharedParse, matches = std::move(matches)](
                   std::string_view bufferText, std::size_t lineStart,
                   std::size_t lineEnd) -> std::vector<std::pair<std::size_t, std::size_t>> {
            const treesitter::Tree& tree = sharedParse->Update(*parser, bufferText);
            if (tree.IsNull()) {
                return {};
            }
            const treesitter::Node entry = tree.RootNode().NamedDescendantForByteRange(lineStart, lineEnd);
            std::vector<std::pair<std::size_t, std::size_t>> candidates;
            CollectLineInspectCandidates(entry.IsNull() ? tree.RootNode() : entry, lineStart, lineEnd, matches, candidates);
            return candidates;
        };
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
    return Mode{
        .name = "fundamental-mode", .keymap = Keymap(), .highlight = HighlightFunction(), .autoPairs = DefaultAutoPairs()};
}

std::string LanguageKeyForMode(const Mode& mode) {
    constexpr std::string_view kSuffix = "-mode";
    if (mode.name.size() > kSuffix.size() && mode.name.ends_with(kSuffix)) {
        return mode.name.substr(0, mode.name.size() - kSuffix.size());
    }
    return mode.name;
}

SyntaxClass SyntaxClassFor(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::Callable:
            return SyntaxClass::Function;
        case SymbolKind::TypeLike:
            return SyntaxClass::Type;
        case SymbolKind::Data:
            return SyntaxClass::Constant;
        case SymbolKind::Namespace:
            return SyntaxClass::Namespace;
    }
    return SyntaxClass::Default; // unreachable, same convention as SyntaxClassForCapture's own default
}

std::optional<SymbolKind> SymbolKindFromCaptureName(std::string_view captureName) {
    // The ctags/nvim-treesitter tags.scm convention -- checked directly
    // against every bundled grammar that ships one (C/C++/PHP/JavaScript/
    // TypeScript/Python), not assumed; a few extra plausible names
    // (struct/enum/variable/property) are included defensively for a future
    // language's own tags.scm, which may spell these slightly differently
    // than the ones actually observed. Anything not starting with
    // "definition." (a "@reference.*" capture, or a nested "@name"/"@doc"/
    // "@local.scope" from the same pattern match) is deliberately not a
    // match here -- see this function's own doc comment in Mode.h.
    if (captureName == "definition.function" || captureName == "definition.method") {
        return SymbolKind::Callable;
    }
    if (captureName == "definition.class" || captureName == "definition.interface" ||
        captureName == "definition.type" || captureName == "definition.module" ||
        captureName == "definition.struct" || captureName == "definition.enum") {
        return SymbolKind::TypeLike;
    }
    if (captureName == "definition.constant" || captureName == "definition.var" ||
        captureName == "definition.variable" || captureName == "definition.field" ||
        captureName == "definition.property") {
        return SymbolKind::Data;
    }
    // main-editor-sticky-scroll follow-up: a distinct capture name, not
    // folded into "definition.module" above -- see SymbolKind::Namespace's
    // own doc comment for why namespace stays its own bucket.
    if (captureName == "definition.namespace") {
        return SymbolKind::Namespace;
    }
    return std::nullopt;
}

Mode TreeSitterModeFromLanguage(std::string name, const treesitter::Language& language, std::string_view querySource,
                                std::string_view foldQuerySource, std::string_view importQuerySource,
                                std::string_view symbolKindQuerySource, std::string_view testQuerySource,
                                std::string_view indentQuerySource) {
    const auto parser = std::make_shared<treesitter::Parser>(language);

    // language-scoped-capture-rules follow-up: LanguageKeyForMode's own
    // "-mode"-suffix strip, computed here (rather than after the fact via
    // LanguageKeyForMode(mode)) since `name` is moved into the returned Mode
    // further down -- this is what lets the highlight closure below pass a
    // real language key to SyntaxClassForCapture.
    std::string                languageKey = name;
    constexpr std::string_view kModeSuffix = "-mode";
    if (languageKey.size() > kModeSuffix.size() && languageKey.ends_with(kModeSuffix)) {
        languageKey.resize(languageKey.size() - kModeSuffix.size());
    }

    // Shared between highlight and fold below (generic-code-folding
    // follow-up) so that a single Paint() cycle calling both against the
    // exact same buffer text -- the common case, since BufferView recomputes
    // each independently but only when content actually changed -- parses
    // only once, not twice. Not a preemptive optimization: two genuinely
    // independent full reparses regressed a real [Performance] test
    // (JsonMode's highlighting-stays-fast test) once JsonMode also gained a
    // real fold query, under -DNED_ENABLE_SANITIZERS=ON's heavier
    // instrumentation -- caught by that test, not assumed. Also what makes
    // every reparse here incremental (incremental-tree-sitter-reparse
    // follow-up) rather than full, per IncrementalParseCache's own doc
    // comment -- one cache, reused by every closure below, since they all
    // parse the exact same buffer text on the exact same cycle.
    const auto sharedParse = std::make_shared<treesitter::IncrementalParseCache>();

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
        highlight        = [parser, query, sharedParse, languageKey](std::string_view bufferText) -> std::vector<HighlightSpan> {
            const treesitter::Tree& tree = sharedParse->Update(*parser, bufferText);
            if (tree.IsNull()) {
                return {};
            }

            SpanCollector collector;
            for (const treesitter::QueryCapture& capture : query->Captures(tree.RootNode(), bufferText)) {
                if (!IsHighlightableCapture(capture.name)) {
                    continue;
                }
                collector.Add(capture.name, capture.startByte, capture.endByte, SyntaxClassForCapture(capture.name, languageKey));
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
            const treesitter::Tree& tree = sharedParse->Update(*parser, bufferText);
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

    // gutter-symbol-kind follow-up: a query against the same parser -- shares
    // sharedParse's cached Tree with highlight/fold above. Only built when a
    // tags query source was actually given; otherwise mode.symbolKind stays
    // a default-constructed, empty std::function, the same "no support"
    // signal fold/highlight above already use. tags.scm mixes
    // "@definition.*" captures (what this wants) with "@reference.*"/
    // "@name"/"@doc"/"@local.scope" ones from the same pattern match --
    // SymbolKindFromCaptureName is what filters to only the former.
    //
    // main-editor-sticky-scroll follow-up: switched from Captures() (flat,
    // loses which "@name" capture belongs to which "@definition.*" one) to
    // Matches() (match-grouped -- same mechanism CollectRawInjectionMatches
    // in Injection.cpp uses to pair "@injection.language" with
    // "@injection.content" from one pattern instance) so SymbolMarker can
    // carry the definition's own full range and name, not just its kind.
    SymbolKindFunction symbolKind;
    if (!symbolKindQuerySource.empty()) {
        const auto symbolKindQuery = std::make_shared<treesitter::Query>(language, symbolKindQuerySource);
        symbolKind                 = [parser, symbolKindQuery, sharedParse](std::string_view bufferText) -> std::vector<SymbolMarker> {
            const treesitter::Tree& tree = sharedParse->Update(*parser, bufferText);
            if (tree.IsNull()) {
                return {};
            }

            std::vector<SymbolMarker> markers;
            for (const treesitter::QueryMatch& match : symbolKindQuery->Matches(tree.RootNode(), bufferText)) {
                std::optional<SymbolKind>                    kind;
                std::optional<treesitter::QueryMatchCapture> definitionCapture;
                std::optional<treesitter::QueryMatchCapture> nameCapture;
                for (const treesitter::QueryMatchCapture& capture : match.captures) {
                    if (!kind) {
                        if (const std::optional<SymbolKind> capturedKind = SymbolKindFromCaptureName(capture.name)) {
                            kind              = capturedKind;
                            definitionCapture = capture;
                        }
                    }
                    if (!nameCapture && capture.name == "name") {
                        nameCapture = capture;
                    }
                }
                if (!kind || !definitionCapture) {
                    continue;
                }
                std::string name;
                if (nameCapture) {
                    name = std::string(bufferText.substr(nameCapture->startByte, nameCapture->endByte - nameCapture->startByte));
                }
                markers.push_back({.startByte = definitionCapture->startByte,
                                   .endByte   = definitionCapture->endByte,
                                   .kind      = *kind,
                                   .name      = std::move(name)});
            }
            // resolver-gaps follow-up (Rust bundling): a query can also
            // double-match one definition onto the exact same range, not
            // just a nested one -- tree-sitter-rust's own upstream tags.scm
            // has no distinct node type for "a method" the way JS/TS/PHP's
            // own method_definition/method_declaration do; it tags any
            // function_item as both "@definition.function" (unconditional)
            // and, separately, "@definition.method" whenever it happens to
            // sit inside a declaration_list (an ancestor check on the SAME
            // node, not a different one) -- confirmed live via a direct
            // Mode::symbolKind probe against an ordinary `impl` method.
            // Collapse exact duplicates (identical range/name/kind) down to
            // one marker before the nested-range collapse below even runs,
            // since that pass's own last clause deliberately excludes an
            // exact-range pair (by design, for the cpp-tags.scm case right
            // below) and would otherwise keep both.
            std::sort(markers.begin(), markers.end(), [](const SymbolMarker& a, const SymbolMarker& b) {
                return std::tie(a.startByte, a.endByte, a.kind, a.name) < std::tie(b.startByte, b.endByte, b.kind, b.name);
            });
            markers.erase(std::unique(markers.begin(), markers.end(),
                                      [](const SymbolMarker& a, const SymbolMarker& b) {
                                          return a.startByte == b.startByte && a.endByte == b.endByte &&
                                                 a.kind == b.kind && a.name == b.name;
                                      }),
                          markers.end());

            // main-editor-sticky-scroll follow-up: a query can legitimately
            // double-match one definition -- cpp-tags.scm's own bodyless-
            // prototype pattern (the only range a real prototype can ever
            // get, since it has no enclosing function_definition to widen
            // into) also matches a with-body definition's own declarator,
            // producing a second, narrower marker alongside the wider
            // function_definition-anchored one for the same symbol. Collapse
            // any marker whose range is fully nested inside another
            // same-name/same-kind marker's own range down to just the wider
            // one -- the gutter (startByte only) can't tell the difference
            // either way, but EnclosingSymbolChain's containment check needs
            // the real (wider) range, not a truncated duplicate sitting
            // alongside it.
            std::erase_if(markers, [&markers](const SymbolMarker& marker) {
                return std::any_of(markers.begin(), markers.end(), [&marker](const SymbolMarker& other) {
                    return &other != &marker && other.name == marker.name && other.kind == marker.kind &&
                           other.startByte <= marker.startByte && marker.endByte <= other.endByte &&
                           (other.startByte != marker.startByte || other.endByte != marker.endByte);
                });
            });
            std::sort(markers.begin(), markers.end(),
                      [](const SymbolMarker& a, const SymbolMarker& b) { return a.startByte < b.startByte; });
            return markers;
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
        const treesitter::Tree& tree = sharedParse->Update(*parser, bufferText);
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
        const treesitter::Tree& tree = sharedParse->Update(*parser, bufferText);
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
        importTarget           = [parser, importQuery, sharedParse](std::string_view bufferText,
                                                                    std::size_t      point) -> std::optional<ImportTarget> {
            const treesitter::Tree& tree = sharedParse->Update(*parser, bufferText);
            if (tree.IsNull()) {
                return std::nullopt;
            }

            enum class TargetKind { Literal,
                                    Module,
                                    Relative,
                                    Namespace,
                                    ModDeclaration };
            struct TargetCapture {
                std::size_t targetStart, targetEnd;
                TargetKind  kind;
            };
            std::vector<TargetCapture>                       targets;
            std::vector<std::pair<std::size_t, std::size_t>> statements;
            for (const treesitter::QueryCapture& capture : importQuery->Captures(tree.RootNode(), bufferText)) {
                if (capture.name == "import.statement") {
                    statements.emplace_back(capture.startByte, capture.endByte);
                }
                else if (capture.name == "import.target") {
                    targets.push_back({capture.startByte, capture.endByte, TargetKind::Literal});
                }
                else if (capture.name == "import.module") {
                    targets.push_back({capture.startByte, capture.endByte, TargetKind::Module});
                }
                else if (capture.name == "import.relative") {
                    // resolver-gaps follow-up: Python's own leading-dot
                    // relative import ("from . import x", "from ..foo
                    // import x") -- the captured node is the whole
                    // relative_import, e.g. ".foo.bar"/"..", dots included.
                    targets.push_back({capture.startByte, capture.endByte, TargetKind::Relative});
                }
                else if (capture.name == "import.namespace") {
                    // resolver-gaps follow-up: PHP's own backslash-separated
                    // `use` namespace -- resolved via PSR-4, not a dotted
                    // module path.
                    targets.push_back({capture.startByte, capture.endByte, TargetKind::Namespace});
                }
                else if (capture.name == "import.moddecl") {
                    // resolver-gaps follow-up: Rust's own bodyless "mod
                    // foo;" file-per-module declaration -- resolved via a
                    // baseDirectory adjustment (the importing file's own
                    // stem), not a dotted module path.
                    targets.push_back({capture.startByte, capture.endByte, TargetKind::ModDeclaration});
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
            int         relativeLevel = 0;
            switch (best->kind) {
                case TargetKind::Literal:
                    text = std::string(link::StripDelimiters(text));
                    break;
                case TargetKind::Module:
                case TargetKind::Namespace:
                case TargetKind::ModDeclaration:
                    break; // kept as raw captured text
                case TargetKind::Relative:
                    // "from . import x" / "from ..foo import x" -- the
                    // captured relative_import node's own text starts with
                    // one-or-more literal '.' characters (import_prefix);
                    // strip them into relativeLevel, leaving just the
                    // remaining dotted module suffix, if any ("" for a bare
                    // "from . import x").
                    while (relativeLevel < static_cast<int>(text.size()) && text[relativeLevel] == '.') {
                        ++relativeLevel;
                    }
                    text.erase(0, relativeLevel);
                    break;
            }
            const bool isModulePath = best->kind == TargetKind::Module || best->kind == TargetKind::Relative;
            return ImportTarget{.target           = std::move(text),
                                .isModulePath     = isModulePath,
                                .startByte        = bestStart,
                                .endByte          = bestEnd,
                                .relativeLevel    = relativeLevel,
                                .isNamespacePath  = best->kind == TargetKind::Namespace,
                                .isModDeclaration = best->kind == TargetKind::ModDeclaration};
        };
    }

    // test-runner integration: a seventh closure sharing the same
    // parser/sharedParse. Captures pair a "@test.definition" (the whole
    // definition node) with a "@test.name" nested inside it -- paired here
    // by smallest-enclosing-definition rather than by match grouping, since
    // Captures() returns a flat, tree-ordered list (and nesting is real:
    // a describe() block contains its it() blocks, a PHPUnit class its
    // methods -- each name must land on its own innermost definition).
    TestDiscoveryFunction testDiscovery;
    if (!testQuerySource.empty()) {
        const auto testQuery = std::make_shared<treesitter::Query>(language, testQuerySource);
        testDiscovery        = [parser, testQuery, sharedParse](std::string_view bufferText) -> std::vector<TestMarker> {
            const treesitter::Tree& tree = sharedParse->Update(*parser, bufferText);
            if (tree.IsNull()) {
                return {};
            }

            struct Definition {
                std::size_t start, end;
                std::string name;
            };
            std::vector<Definition>                          definitions;
            std::vector<std::pair<std::size_t, std::size_t>> names;
            for (const treesitter::QueryCapture& capture : testQuery->Captures(tree.RootNode(), bufferText)) {
                if (capture.name == "test.definition") {
                    definitions.push_back({capture.startByte, capture.endByte, {}});
                }
                else if (capture.name == "test.name") {
                    names.emplace_back(capture.startByte, capture.endByte);
                }
            }

            for (const auto& [nameStart, nameEnd] : names) {
                Definition* best = nullptr;
                for (Definition& definition : definitions) {
                    if (definition.start <= nameStart && nameEnd <= definition.end &&
                        (best == nullptr || (definition.end - definition.start) < (best->end - best->start))) {
                        best = &definition;
                    }
                }
                // A definition matched by several patterns (a PHPUnit method
                // both test*-named and #[Test]-attributed) appears once per
                // pattern with an identical range -- only the first gets the
                // name, so the duplicates below fall away nameless.
                if (best != nullptr && best->name.empty()) {
                    const std::string raw(bufferText.substr(nameStart, nameEnd - nameStart));
                    best->name = std::string(link::StripDelimiters(raw));
                }
            }

            std::vector<TestMarker> markers;
            for (Definition& definition : definitions) {
                if (definition.name.empty()) {
                    continue; // nameless duplicate (see above) or a pattern that matched without its name
                }
                // tree-sitter-cpp parses an unexpanded TEST_CASE("x") { ... }
                // as a call_expression statement with the body left as a
                // *sibling* compound_statement (the macro isn't valid C++
                // unexpanded) -- extend the marker over an immediately
                // adjacent compound_statement sibling so point-inside-the-
                // body still resolves to this test for run-test-at-point.
                std::size_t      endByte = definition.end;
                treesitter::Node node    = tree.RootNode().NamedDescendantForByteRange(
                    definition.start, definition.end > definition.start ? definition.end - 1 : definition.start);
                while (!node.IsNull() && node.StartByte() >= definition.start) {
                    const treesitter::Node sibling = node.NextNamedSibling();
                    if (!sibling.IsNull()) {
                        if (sibling.Type() == "compound_statement" && sibling.StartByte() >= endByte &&
                            sibling.StartByte() <= endByte + 2) {
                            endByte = sibling.EndByte();
                        }
                        break;
                    }
                    node = node.Parent();
                }
                markers.push_back({.startByte = definition.start, .endByte = endByte, .name = std::move(definition.name)});
            }
            std::sort(markers.begin(), markers.end(),
                      [](const TestMarker& a, const TestMarker& b) { return a.startByte < b.startByte; });
            return markers;
        };
    }

    // smart-indentation follow-up: an eighth closure sharing the same
    // parser/sharedParse as everything above, for the same "don't trigger a
    // redundant full reparse on the same Paint() cycle" reason. Only built
    // when an indent query source was actually given; otherwise
    // mode.indentColumn stays a default-constructed, empty std::function,
    // the same "no support" signal every other capability above uses.
    // Captures `name` (the mode's own full name, e.g. "python-mode") by
    // value into BuildIndentFunction BEFORE it's moved into the returned
    // Mode below -- see BuildIndentFunction's own doc comment (Indent.h) for
    // why the indent style lookup needs this rather than languageKey.
    IndentFunction indentColumn;
    if (!indentQuerySource.empty()) {
        const auto indentQuery = std::make_shared<treesitter::Query>(language, indentQuerySource);
        indentColumn            = BuildIndentFunction(parser, indentQuery, sharedParse, name);
    }

    // Debugging wishlist (line-inspect follow-up): Tier 1 -- unconditional,
    // every TreeSitterModeFromLanguage-built mode gets this generic default
    // (bare identifiers only, no per-language query authoring). Its own
    // independent Parser/IncrementalParseCache pair (BuildLineInspectFunction),
    // not sharedParse above -- this only ever runs on an explicit,
    // infrequent dap-line-inspect invocation, never per-Paint(), so sharing
    // would buy nothing.
    LineInspectFunction lineInspect =
        BuildLineInspectFunction(language, [](std::string_view type) { return type == "identifier"; });

    return Mode{.name            = std::move(name),
                .keymap          = Keymap(),
                .highlight       = std::move(highlight),
                .fold            = std::move(fold),
                .expandSelection = std::move(expandSelection),
                .sexpMotion      = std::move(sexpMotion),
                .autoPairs       = DefaultAutoPairs(),
                .symbolKind      = std::move(symbolKind),
                .importTarget    = std::move(importTarget),
                .testDiscovery   = std::move(testDiscovery),
                .indentColumn    = std::move(indentColumn),
                .lineInspect     = std::move(lineInspect)};
}

Mode TreeSitterMode(std::string name, std::string_view languageName, const char* querySource,
                    const char* foldQuerySource, const char* importQuerySource, const char* symbolKindQuerySource,
                    const char* testQuerySource, const char* indentQuerySource) {
    const auto language = treesitter::LanguageByName(languageName);
    // Every languageName this is called with (below) names a grammar
    // Languages.cpp always bundles -- if this ever fires it's a build-time
    // bundling regression (a Mode function calling this with a typo'd or
    // no-longer-bundled name), not a runtime condition to recover from
    // gracefully.
    return TreeSitterModeFromLanguage(std::move(name), *language, querySource,
                                      foldQuerySource != nullptr ? std::string_view(foldQuerySource) : std::string_view(),
                                      importQuerySource != nullptr ? std::string_view(importQuerySource) : std::string_view(),
                                      symbolKindQuerySource != nullptr ? std::string_view(symbolKindQuerySource) : std::string_view(),
                                      testQuerySource != nullptr ? std::string_view(testQuerySource) : std::string_view(),
                                      indentQuerySource != nullptr ? std::string_view(indentQuerySource) : std::string_view());
}

Mode JanetMode() {
    Mode mode              = TreeSitterMode("janet-mode", "janet", treesitter::queries::kJanet, nullptr, treesitter::queries::kJanetImports,
                                            nullptr, nullptr, treesitter::queries::kJanetIndents);
    mode.lineCommentPrefix = ";";             // Lisp-family convention
    mode.autoPairs         = LispAutoPairs(); // '(...) is the reader's quote macro, not a paired delimiter
    return mode;
}

Mode JsonMode() {
    // No lineCommentPrefix -- JSON has no comment syntax at all, real or
    // otherwise; toggle-line-comment correctly reports nothing configured
    // rather than inserting something that would make the file invalid JSON.
    return TreeSitterMode("json-mode", "json", treesitter::queries::kJson, treesitter::queries::kJsonFolds, nullptr,
                          nullptr, nullptr, treesitter::queries::kJsonIndents);
}

Mode CMode() {
    Mode mode              = TreeSitterMode("c-mode", "c", treesitter::queries::kC, treesitter::queries::kCFolds,
                                            treesitter::queries::kCImports, treesitter::queries::kCTags, nullptr,
                                            treesitter::queries::kCIndents);
    mode.lineCommentPrefix = "//";
    // Debugging wishlist (line-inspect follow-up), Tier 2: overrides the
    // generic identifier-only default with IsCLikeExpressionNodeType's
    // richer, grammar-verified set.
    if (const auto language = treesitter::LanguageByName("c")) {
        mode.lineInspect = BuildLineInspectFunction(*language, IsCLikeExpressionNodeType);
    }
    return mode;
}

Mode CppMode() {
    Mode mode              = TreeSitterMode("cpp-mode", "cpp", treesitter::queries::kCpp, treesitter::queries::kCppFolds,
                                            treesitter::queries::kCImports, treesitter::queries::kCppTags,
                                            treesitter::queries::kCppTests, treesitter::queries::kCppIndents);
    mode.lineCommentPrefix = "//";
    // Debugging wishlist (line-inspect follow-up), Tier 2 -- see CMode's own.
    if (const auto language = treesitter::LanguageByName("cpp")) {
        mode.lineInspect = BuildLineInspectFunction(*language, IsCLikeExpressionNodeType);
    }
    return mode;
}

Mode PhpMode() {
    Mode mode              = TreeSitterMode("php-mode", "php", treesitter::queries::kPhp, nullptr, treesitter::queries::kPhpImports,
                                            treesitter::queries::kPhpTags, treesitter::queries::kPhpTests,
                                            treesitter::queries::kPhpIndents);
    mode.lineCommentPrefix = "//";
    return mode;
}

Mode JavaScriptMode() {
    Mode mode              = TreeSitterMode("javascript-mode", "javascript", treesitter::queries::kJavaScript,
                                            treesitter::queries::kJavaScriptFolds, treesitter::queries::kJavaScriptImports,
                                            treesitter::queries::kJavaScriptTags, treesitter::queries::kJavaScriptTests,
                                            treesitter::queries::kJavaScriptIndents);
    mode.lineCommentPrefix = "//";
    return mode;
}

Mode TypeScriptMode() {
    Mode mode              = TreeSitterMode("typescript-mode", "typescript", treesitter::queries::kTypeScript,
                                            treesitter::queries::kTypeScriptFolds, treesitter::queries::kTypeScriptImports,
                                            treesitter::queries::kTypeScriptTags, treesitter::queries::kTypeScriptTests,
                                            treesitter::queries::kTypeScriptIndents);
    mode.lineCommentPrefix = "//";
    return mode;
}

Mode TsxMode() {
    Mode mode              = TreeSitterMode("tsx-mode", "tsx", treesitter::queries::kTypeScript, treesitter::queries::kTypeScriptFolds,
                                            treesitter::queries::kTypeScriptImports, treesitter::queries::kTypeScriptTags,
                                            treesitter::queries::kTypeScriptTests, treesitter::queries::kTypeScriptIndents);
    mode.lineCommentPrefix = "//";
    return mode;
}

Mode HtmlMode() {
    Mode mode = TreeSitterMode("html-mode", "html", treesitter::queries::kHtml, nullptr, nullptr, nullptr, nullptr,
                               treesitter::queries::kHtmlIndents);
    // No lineCommentPrefix -- HTML only has block comments (<!-- -->), no
    // single-line comment token to toggle per line.

    // embedded-language-injection follow-up: mode.highlight built by
    // TreeSitterMode() above gets replaced below to also run
    // <script>/<style> content through the real injections.scm-driven
    // generic engine (Injection.h) -- javascript/css highlighting inside
    // them, instead of plain unhighlighted markup text. Everything else
    // built by TreeSitterMode() (.keymap/.fold/.expandSelection/...) is kept
    // exactly as-is.
    const auto language              = treesitter::LanguageByName("html");
    const auto parser                = std::make_shared<treesitter::Parser>(*language);
    const auto query                 = std::make_shared<treesitter::Query>(*language, treesitter::queries::kHtml);
    const auto injectionQuery        = std::make_shared<treesitter::Query>(*language, treesitter::queries::kHtmlInjections);
    const auto sharedParse           = std::make_shared<treesitter::IncrementalParseCache>();
    const auto embeddedLanguageCache = std::make_shared<EmbeddedLanguageCache>();

    mode.highlight = [parser, query, injectionQuery, sharedParse,
                      embeddedLanguageCache](std::string_view bufferText) -> std::vector<HighlightSpan> {
        const treesitter::Tree& tree = sharedParse->Update(*parser, bufferText);
        if (tree.IsNull()) {
            return {};
        }
        const treesitter::Node root = tree.RootNode();

        SpanCollector collector;
        for (const treesitter::QueryCapture& capture : query->Captures(root, bufferText)) {
            if (!IsHighlightableCapture(capture.name)) {
                continue;
            }
            collector.Add(capture.name, capture.startByte, capture.endByte, SyntaxClassForCapture(capture.name, "html"));
        }
        std::vector<HighlightSpan> spans = collector.Take();

        // <script>/<style> content, appended last so it wins over anything
        // the base query above captured in that range.
        CollectInjectedHighlightSpans(root, bufferText, *injectionQuery, *embeddedLanguageCache, spans);

        return spans;
    };

    // embedded-language-documents follow-up: reuses the exact same
    // parser/injectionQuery/sharedParse the highlight closure above already
    // captures -- a second (cheap) query-match walk over the same
    // already-parsed tree, not a second parse. This is what
    // Editor/EmbeddedDocuments.h's BuildEmbeddedDocuments consumes to sync
    // <script>/<style> content to real javascript/css LSP servers, distinct
    // from the highlighting-only path above.
    mode.embeddedRegions = [parser, injectionQuery, sharedParse](std::string_view bufferText) -> std::vector<InjectionRegion> {
        const treesitter::Tree& tree = sharedParse->Update(*parser, bufferText);
        if (tree.IsNull()) {
            return {};
        }
        return CollectInjectionRegions(tree.RootNode(), bufferText, *injectionQuery);
    };

    return mode;
}

Mode CssMode() {
    // No lineCommentPrefix -- same reasoning as HtmlMode, CSS only has
    // block comments (/* */).
    return TreeSitterMode("css-mode", "css", treesitter::queries::kCss, nullptr, treesitter::queries::kCssImports, nullptr,
                          nullptr, treesitter::queries::kCssIndents);
}

Mode PythonMode() {
    Mode mode              = TreeSitterMode("python-mode", "python", treesitter::queries::kPython, treesitter::queries::kPythonFolds,
                                            treesitter::queries::kPythonImports, treesitter::queries::kPythonTags,
                                            treesitter::queries::kPythonTests, treesitter::queries::kPythonIndents);
    mode.lineCommentPrefix = "#";
    return mode;
}

Mode BashMode() {
    Mode mode              = TreeSitterMode("bash-mode", "bash", treesitter::queries::kBash, nullptr, treesitter::queries::kBashImports,
                                            nullptr, nullptr, treesitter::queries::kBashIndents);
    mode.lineCommentPrefix = "#";
    return mode;
}

Mode FishMode() {
    Mode mode              = TreeSitterMode("fish-mode", "fish", treesitter::queries::kFish, nullptr, nullptr, nullptr, nullptr,
                                            treesitter::queries::kFishIndents);
    mode.lineCommentPrefix = "#";
    return mode;
}

Mode XmlMode() {
    // No lineCommentPrefix -- XML only has block comments (<!-- -->), same
    // reasoning as HtmlMode/CssMode above.
    return TreeSitterMode("xml-mode", "xml", treesitter::queries::kXml, nullptr, nullptr, nullptr, nullptr,
                          treesitter::queries::kXmlIndents);
}

Mode RustMode() {
    Mode mode              = TreeSitterMode("rust-mode", "rust", treesitter::queries::kRust, treesitter::queries::kRustFolds,
                                            treesitter::queries::kRustImports, treesitter::queries::kRustTags,
                                            treesitter::queries::kRustTests, treesitter::queries::kRustIndents);
    mode.lineCommentPrefix = "//";
    return mode;
}

Mode YamlMode() {
    Mode mode              = TreeSitterMode("yaml-mode", "yaml", treesitter::queries::kYaml, nullptr, nullptr, nullptr, nullptr,
                                            treesitter::queries::kYamlIndents);
    mode.lineCommentPrefix = "#";
    return mode;
}

Mode TomlMode() {
    Mode mode              = TreeSitterMode("toml-mode", "toml", treesitter::queries::kToml, nullptr, nullptr, nullptr, nullptr,
                                            treesitter::queries::kTomlIndents);
    mode.lineCommentPrefix = "#";
    return mode;
}

Mode ClojureMode() {
    Mode mode              = TreeSitterMode("clojure-mode", "clojure", treesitter::queries::kClojure, treesitter::queries::kClojureFolds,
                                            treesitter::queries::kClojureImports, nullptr, nullptr, treesitter::queries::kClojureIndents);
    mode.lineCommentPrefix = ";";             // Lisp-family convention, same as JanetMode
    mode.autoPairs         = LispAutoPairs(); // same reasoning as JanetMode
    return mode;
}

Mode JankMode() {
    // Same grammar and query as ClojureMode, distinct name -- see Mode.h.
    Mode mode              = TreeSitterMode("jank-mode", "clojure", treesitter::queries::kClojure, treesitter::queries::kClojureFolds,
                                            treesitter::queries::kClojureImports, nullptr, nullptr, treesitter::queries::kClojureIndents);
    mode.lineCommentPrefix = ";";
    mode.autoPairs         = LispAutoPairs(); // same reasoning as JanetMode
    return mode;
}

Mode MarkdownMode() {
    Mode mode = TreeSitterMode("markdown-mode", "markdown", treesitter::queries::kMarkdown);
    // Tables follow-up: the second Mode in this codebase to ever construct
    // a non-empty Keymap (OrgMode() was the first, see its own doc comment
    // below) -- no shadowing risk, Markdown has no fold-cycle or other TAB
    // use to compete with.
    mode.keymap.Bind(ParseKeySequence("TAB"), "markdown-table-align");
    // Markdown table editing surface follow-up: real Org's own
    // table-editing bindings (see OrgMode() below), mirrored here.
    // S-TAB is unbound globally, so no shadowing; M-UP/M-DOWN
    // deliberately shadow the global move-line-up/move-line-down with
    // markdown-metaup/markdown-metadown, which fall back to the exact
    // same line move outside a table. Every Meta chord gets the same
    // dual M-/ESC-prefix binding the global keymap uses.
    mode.keymap.Bind(ParseKeySequence("S-TAB"), "markdown-table-previous-cell");
    mode.keymap.Bind(ParseKeySequence("M-UP"), "markdown-metaup");
    mode.keymap.Bind(ParseKeySequence("ESC UP"), "markdown-metaup");
    mode.keymap.Bind(ParseKeySequence("M-DOWN"), "markdown-metadown");
    mode.keymap.Bind(ParseKeySequence("ESC DOWN"), "markdown-metadown");
    mode.keymap.Bind(ParseKeySequence("M-S-DOWN"), "markdown-table-insert-row");
    mode.keymap.Bind(ParseKeySequence("ESC S-DOWN"), "markdown-table-insert-row");
    mode.keymap.Bind(ParseKeySequence("M-S-UP"), "markdown-table-kill-row");
    mode.keymap.Bind(ParseKeySequence("ESC S-UP"), "markdown-table-kill-row");
    mode.keymap.Bind(ParseKeySequence("M-S-RIGHT"), "markdown-table-insert-column");
    mode.keymap.Bind(ParseKeySequence("ESC S-RIGHT"), "markdown-table-insert-column");
    mode.keymap.Bind(ParseKeySequence("M-S-LEFT"), "markdown-table-delete-column");
    mode.keymap.Bind(ParseKeySequence("ESC S-LEFT"), "markdown-table-delete-column");
    mode.keymap.Bind(ParseKeySequence("M-LEFT"), "markdown-table-move-column-left");
    mode.keymap.Bind(ParseKeySequence("ESC LEFT"), "markdown-table-move-column-left");
    mode.keymap.Bind(ParseKeySequence("M-RIGHT"), "markdown-table-move-column-right");
    mode.keymap.Bind(ParseKeySequence("ESC RIGHT"), "markdown-table-move-column-right");
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
    // plain query can't express: heading levels and GFM checkboxes (Pass 2
    // below). Markdown's own inline-grammar formatting and its fenced code
    // blocks used to be two more hand-rolled passes here
    // (embedded-language-injection follow-up); both are now the real
    // upstream injections.scm driving Injection.h's generic engine (Pass 3).
    const auto blockLanguage = treesitter::LanguageByName("markdown");
    const auto blockParser   = std::make_shared<treesitter::Parser>(*blockLanguage);
    const auto blockQuery    = std::make_shared<treesitter::Query>(*blockLanguage, treesitter::queries::kMarkdown);
    const auto injectionQuery =
        std::make_shared<treesitter::Query>(*blockLanguage, treesitter::queries::kMarkdownInjections);

    // Same cached-tree-by-text-equality idiom TreeSitterModeFromLanguage
    // uses internally (see its own doc comment) -- skipping the full block
    // parse (and every injected sub-parse) when bufferText is unchanged
    // since the last call matters here too, not just for the generic path.
    // Also what makes this an incremental reparse rather than a full one
    // (incremental-tree-sitter-reparse follow-up) -- see
    // IncrementalParseCache's own doc comment.
    const auto sharedParse = std::make_shared<treesitter::IncrementalParseCache>();

    // One HighlightFunction cache per distinct injected language actually
    // encountered (fenced-code languages, plus "markdown-inline" itself),
    // shared across every call to this closure, mirroring sharedParse's own
    // shared_ptr-captured-by-value idiom.
    const auto embeddedLanguageCache = std::make_shared<EmbeddedLanguageCache>();

    mode.highlight = [blockParser, blockQuery, injectionQuery, sharedParse,
                      embeddedLanguageCache](std::string_view bufferText) -> std::vector<HighlightSpan> {
        const treesitter::Tree& tree = sharedParse->Update(*blockParser, bufferText);
        if (tree.IsNull()) {
            return {};
        }
        const treesitter::Node root = tree.RootNode();

        // Concatenated in this order so a later, narrower span visually
        // wins over an earlier, broader one via HighlightSpan's own
        // documented "later wins" overlap rule -- e.g. bold text inside a
        // heading, or a checkbox inside a list item.
        std::vector<HighlightSpan> spans;

        // Pass 1: block-level query captures through the shared CaptureTable
        // -- "markdown" as the language means "punctuation.special" (list
        // markers/thematic break/heading markers/blockquote marker) resolves
        // through LanguageCaptureTable's own MarkupMarker default instead of
        // CaptureTable's shared Punctuation one, without a special case here
        // (language-scoped-capture-rules follow-up; see Mode.cpp's own
        // SyntaxClassForCapture doc comment).
        SpanCollector blockCollector;
        for (const treesitter::QueryCapture& capture : blockQuery->Captures(root, bufferText)) {
            if (!IsHighlightableCapture(capture.name)) {
                continue;
            }
            blockCollector.Add(capture.name, capture.startByte, capture.endByte,
                               SyntaxClassForCapture(capture.name, "markdown"));
        }
        spans = blockCollector.Take();

        // Pass 2: heading levels + task-list checkboxes, from walking the
        // real tree rather than query captures.
        CollectMarkdownStructuralSpans(root, spans);

        // Pass 3: everything the real injections.scm expresses -- inline
        // formatting (bold/italic/strikethrough/code-span/links, injected
        // into "markdown-inline"), fenced code blocks (into whatever
        // language their info string names), html_block, and frontmatter.
        // Appended last so it wins over everything above, including a
        // heading's own whole-line span and Pass 1's whole-fenced-block
        // "text.literal" (String) span.
        CollectInjectedHighlightSpans(root, bufferText, *injectionQuery, *embeddedLanguageCache, spans);

        return spans;
    };

    // smart-indentation follow-up: hand-rolled, mirroring .highlight's own
    // bypass of the generic query-driven path above -- real Markdown list
    // continuation needs a hanging indent to the bullet's own content
    // COLUMN (e.g. "1. " = 3, "- " = 2), not level * a fixed width, so this
    // doesn't fit Editor/Indent.h's generic @indent/@dedent engine at all
    // (see that file's own header comment). Shares blockParser/sharedParse
    // with .highlight above -- one more closure reusing the same cached
    // parse, not a second reparse on the same Paint()/keystroke cycle.
    mode.indentColumn = [blockParser, sharedParse](std::string_view bufferText, std::size_t lineStart,
                                                    std::size_t lineEnd) -> std::optional<int> {
        const treesitter::Tree& tree = sharedParse->Update(*blockParser, bufferText);
        if (tree.IsNull()) {
            return std::nullopt;
        }

        std::size_t contentStart = lineEnd; // default: the whole line is blank
        for (std::size_t i = lineStart; i < lineEnd; ++i) {
            if (bufferText[i] != ' ' && bufferText[i] != '\t') {
                contentStart = i;
                break;
            }
        }

        treesitter::Node node = tree.RootNode().NamedDescendantForByteRange(contentStart, contentStart);
        if (node.IsNull()) {
            return 0;
        }

        // Fenced-code passthrough: content inside a ```-fenced block is
        // opaque non-Markdown text, not reflowed/recomputed from structure
        // at all -- copy whatever the previous line's own leading
        // whitespace already is, byte-for-byte (a deliberate, explicit
        // exception to "never naive-copy-the-line-above," justified because
        // fence content genuinely isn't Markdown structure to walk). Codepoint
        // count, not display column (Fill.h's own documented v1 scope cut,
        // same reasoning -- a leading run of plain spaces/tabs essentially
        // never needs real tab-expansion math to reproduce verbatim).
        for (treesitter::Node ancestor = node; !ancestor.IsNull(); ancestor = ancestor.Parent()) {
            if (ancestor.Type() != "code_fence_content") {
                continue;
            }
            if (lineStart == 0) {
                return 0;
            }
            // lineStart - 1 is the '\n' terminating the PREVIOUS line itself
            // (lineStart is always right after a real newline here) -- the
            // search for that line's own START has to look one byte further
            // back than that, for the newline terminating the line before
            // IT, or this finds lineStart right back again instead of the
            // previous line's start.
            const std::size_t searchFrom  = (lineStart <= 1) ? 0 : lineStart - 2;
            std::size_t        prevLineStart = bufferText.rfind('\n', searchFrom);
            prevLineStart                  = (prevLineStart == std::string_view::npos) ? 0 : prevLineStart + 1;
            int column                = 0;
            for (std::size_t i = prevLineStart; i < bufferText.size() && (bufferText[i] == ' ' || bufferText[i] == '\t');
                 ++i) {
                ++column;
            }
            return column;
        }

        // Otherwise: sum each enclosing list_item's own marker width (its
        // first child's byte length, e.g. "- " = 2, "10. " = 4 -- nested
        // lists stack additively) plus 2 per enclosing block_quote ("> ").
        // A list_item/block_quote is excluded from its OWN opening/marker
        // line (StartByte() == position) -- the same self-exclusion
        // Editor/Indent.h's generic engine needs for a bracket-language
        // container's own opening line, confirmed by the same kind of real
        // parse-tree check that caught that engine's own bugs. A lambda,
        // not an inline loop -- smart-blank-line-on-newline follow-up:
        // needs calling twice, see the rescue immediately below.
        const auto sumHangColumn = [](const treesitter::Node& startNode, std::size_t position) {
            int result = 0;
            for (treesitter::Node ancestor = startNode; !ancestor.IsNull(); ancestor = ancestor.Parent()) {
                if (ancestor.Type() == "list_item") {
                    if (ancestor.StartByte() != position && ancestor.ChildCount() > 0) {
                        const treesitter::Node marker = ancestor.Child(0);
                        result += static_cast<int>(marker.EndByte() - marker.StartByte());
                    }
                }
                else if (ancestor.Type() == "block_quote") {
                    if (ancestor.StartByte() != position) {
                        result += 2;
                    }
                }
            }
            return result;
        };

        int column = sumHangColumn(node, contentStart);

        // smart-blank-line-on-newline follow-up: a freshly inserted,
        // not-yet-typed blank line (lineStart == lineEnd) at the document's
        // own tail can fall entirely outside every list_item/block_quote's
        // own byte range, the same boundary gap Editor/Indent.h's generic
        // engine hit (confirmed via a real failing test, not assumed) --
        // Markdown's own list_item has no closing delimiter either, so
        // there's nothing here to accidentally rescue onto the WRONG side
        // of (unlike that engine's own dedent-range exclusion). Re-sum from
        // the last real, non-whitespace byte instead.
        if (lineStart == lineEnd && column == 0 && contentStart == bufferText.size() && contentStart > 0) {
            const std::size_t rescuePos = bufferText.find_last_not_of(" \t\n\r", contentStart - 1);
            if (rescuePos != std::string_view::npos) {
                const treesitter::Node rescueNode = tree.RootNode().NamedDescendantForByteRange(rescuePos, rescuePos);
                if (!rescueNode.IsNull()) {
                    column = sumHangColumn(rescueNode, rescuePos);
                }
            }
        }

        // smart-blank-line-on-newline follow-up: a SECOND consecutive
        // Enter on an empty list-continuation line breaks out of the list
        // (real Markdown/Org editors' own convention) instead of hang-
        // indenting to the same column again forever. Checked only when
        // the ordinary computation above actually found a real hang column
        // (column > 0, i.e. we're genuinely inside a list/blockquote) --
        // never touches the fenced-code passthrough above (which already
        // returned early) or an ordinary column-0 result. A plain textual
        // check of the immediately preceding line, not tree-based --
        // deterministic, no parse-dump verification needed the way a
        // tree-sitter-driven rule would be.
        if (lineStart == lineEnd && column > 0 && lineStart > 0) {
            const std::size_t searchFrom    = (lineStart <= 1) ? 0 : lineStart - 2;
            std::size_t        prevLineStart = bufferText.rfind('\n', searchFrom);
            prevLineStart                  = (prevLineStart == std::string_view::npos) ? 0 : prevLineStart + 1;
            bool prevLineBlank = true;
            for (std::size_t i = prevLineStart; i < lineStart - 1; ++i) {
                if (bufferText[i] != ' ' && bufferText[i] != '\t') {
                    prevLineBlank = false;
                    break;
                }
            }
            if (prevLineBlank) {
                return 0;
            }
        }
        return column;
    };

    // main-editor-sticky-scroll-markdown follow-up: shares blockParser/
    // sharedParse with .highlight/.indentColumn above -- no extra reparse on
    // a Paint() cycle that also needs one of those. See
    // CollectMarkdownSectionMarkers' own doc comment for why a section's
    // real tree range is already the synthesized "runs until the next
    // equal-or-shallower heading" extent sticky scroll needs, with no
    // per-level bookkeeping here. Reuses SymbolKind::Namespace (the "§"
    // glyph already reads naturally as "section") rather than a new kind --
    // heading level itself doesn't need representing separately, since the
    // sticky row's own reduced-signature text (the heading's real source
    // line, "#"/"##"/... markers included) already shows it.
    mode.symbolKind = [blockParser, sharedParse](std::string_view bufferText) -> std::vector<SymbolMarker> {
        const treesitter::Tree& tree = sharedParse->Update(*blockParser, bufferText);
        if (tree.IsNull()) {
            return {};
        }
        std::vector<SymbolMarker> markers;
        CollectMarkdownSectionMarkers(tree.RootNode(), markers);
        return markers;
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
    // Property drawers follow-up: real Org's own org-set-property binding.
    // "C-c C-x" is otherwise unbound in this keymap (a plain prefix, no
    // command of its own), so no Keymap::AmbiguousBindings() risk the way
    // "C-c a" briefly had for the ACP commands.
    keymap.Bind(ParseKeySequence("C-c C-x p"), "org-set-property");
    keymap.Bind(ParseKeySequence("C-c C-x d"), "org-delete-property");
    // Clocking follow-up: real Org's own bindings are "C-c C-x C-i"/
    // "C-c C-x C-o" -- deliberately using a plain final letter here
    // instead ("i"/"o", not "C-i"/"C-o"): Ctrl-I is byte-identical to Tab
    // over a raw terminal with no Kitty keyboard protocol assumed, an
    // unnecessary reliability risk for no real gain under this same
    // "C-c C-x" prefix p/d already use safely above.
    keymap.Bind(ParseKeySequence("C-c C-x i"), "org-clock-in");
    keymap.Bind(ParseKeySequence("C-c C-x o"), "org-clock-out");
    // org-clock-display follow-up: real Org's own binding is "C-c C-x C-r"
    // (a different feature -- an inserted in-buffer table); this is a
    // synthesized report buffer instead, so a plain "r" under the same
    // "C-c C-x" prefix i/o already use, same Ctrl-vs-plain-letter
    // reasoning as those two.
    keymap.Bind(ParseKeySequence("C-c C-x r"), "org-clock-report");
    // Scheduling/recurrence follow-up: real Org's own bindings -- both
    // deliberately shadow a global command (project-search/create-directory)
    // the same way C-c C-p already does; see Mode.h's own doc comment.
    keymap.Bind(ParseKeySequence("C-c C-s"), "org-schedule");
    keymap.Bind(ParseKeySequence("C-c C-d"), "org-deadline");
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
    // embedded-language-injection follow-up: #+BEGIN_SRC/#+BEGIN_EXPORT
    // block bodies, through the same generic engine Markdown/HTML use --
    // queries::kOrgInjections is a real injections.scm from Ned's own
    // tree-sitter-ned-org fork itself (see Queries.h's own comment).
    const auto injectionQuery        = std::make_shared<treesitter::Query>(*orgLanguage, treesitter::queries::kOrgInjections);
    const auto embeddedLanguageCache = std::make_shared<EmbeddedLanguageCache>();
    // Incremental-tree-sitter-reparse follow-up: this closure previously
    // parsed unconditionally on every call, unlike every other Mode's own
    // highlight closure -- no earlier "same text as last call" cache existed
    // here at all. IncrementalParseCache closes both gaps: an unchanged-text
    // call is now free, and a genuinely changed one reparses incrementally
    // against the previous tree instead of from scratch.
    const auto sharedParse = std::make_shared<treesitter::IncrementalParseCache>();

    HighlightFunction highlight = [parser, query, injectionQuery, embeddedLanguageCache,
                                   sharedParse](std::string_view bufferText) -> std::vector<HighlightSpan> {
        const treesitter::Tree& tree = sharedParse->Update(*parser, bufferText);
        if (tree.IsNull()) {
            return {};
        }
        const treesitter::Node                      root     = tree.RootNode();
        const std::vector<treesitter::QueryCapture> captures = query->Captures(root, bufferText);

        // Four passes, concatenated in this order so a later, narrower
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
            genericCollector.Add(capture.name, capture.startByte, capture.endByte, SyntaxClassForCapture(capture.name, "org"));
        }
        for (const HighlightSpan& span : genericCollector.Take()) {
            spans.push_back(span);
        }

        // Pass 4: real per-language highlighting inside #+BEGIN_SRC/
        // #+BEGIN_EXPORT block bodies, appended last so it wins over
        // whatever Pass 3's generic capture table resolved the block's
        // "contents" node to (typically Default -- OrgHighlights.scm has no
        // pattern for it at all).
        CollectInjectedHighlightSpans(root, bufferText, *injectionQuery, *embeddedLanguageCache, spans);

        return spans;
    };

    // smart-indentation follow-up: hand-rolled, mirroring MarkdownMode()'s
    // own bespoke closure -- real Org list continuation needs a hanging
    // indent to the bullet's own content COLUMN (checked against a real
    // parse dump: "listitem"'s children are [bullet, ...body], with the
    // body's own start byte -- NOT bullet's own end byte, there's a
    // separating space in between not covered by either -- giving the real
    // hang width), so this doesn't fit Editor/Indent.h's generic
    // @indent/@dedent engine any more than Markdown's own list handling
    // does. Headline body text is deliberately NOT indented under its own
    // stars here -- real Org's own long-standing convention keeps body text
    // flush regardless of heading level, unlike list continuation. Shares
    // parser/sharedParse with highlight above.
    IndentFunction indentColumn = [parser, sharedParse](std::string_view bufferText, std::size_t lineStart,
                                                        std::size_t lineEnd) -> std::optional<int> {
        const treesitter::Tree& tree = sharedParse->Update(*parser, bufferText);
        if (tree.IsNull()) {
            return std::nullopt;
        }

        std::size_t contentStart = lineEnd; // default: the whole line is blank
        for (std::size_t i = lineStart; i < lineEnd; ++i) {
            if (bufferText[i] != ' ' && bufferText[i] != '\t') {
                contentStart = i;
                break;
            }
        }

        const treesitter::Node node = tree.RootNode().NamedDescendantForByteRange(contentStart, contentStart);
        if (node.IsNull()) {
            return 0;
        }

        // Sum each enclosing listitem's own hang width (its second child's
        // byte offset from its own start -- bullet plus whatever separates
        // it from the body, e.g. "- " = 2, "1. " = 3) -- nested lists stack
        // additively. A listitem is excluded from its OWN bullet line
        // (StartByte() == position), the same self-exclusion Editor/
        // Indent.h's generic engine needs for a container's own opening
        // line. A lambda, not an inline loop -- smart-blank-line-on-newline
        // follow-up: needs calling twice, see the rescue immediately below.
        const auto sumHangColumn = [](const treesitter::Node& startNode, std::size_t position) {
            int result = 0;
            for (treesitter::Node ancestor = startNode; !ancestor.IsNull(); ancestor = ancestor.Parent()) {
                if (ancestor.Type() != "listitem") {
                    continue;
                }
                if (ancestor.StartByte() == position || ancestor.ChildCount() < 2) {
                    continue;
                }
                const treesitter::Node body = ancestor.Child(1);
                result += static_cast<int>(body.StartByte() - ancestor.StartByte());
            }
            return result;
        };

        int column = sumHangColumn(node, contentStart);

        // smart-blank-line-on-newline follow-up: mirrors MarkdownMode()'s
        // own rescue -- a freshly inserted, not-yet-typed blank line at the
        // document's own tail can fall entirely outside every listitem's
        // own byte range. Re-sum from the last real, non-whitespace byte
        // instead when that happens; see MarkdownMode()'s own comment for
        // the full reasoning.
        if (lineStart == lineEnd && column == 0 && contentStart == bufferText.size() && contentStart > 0) {
            const std::size_t rescuePos = bufferText.find_last_not_of(" \t\n\r", contentStart - 1);
            if (rescuePos != std::string_view::npos) {
                const treesitter::Node rescueNode = tree.RootNode().NamedDescendantForByteRange(rescuePos, rescuePos);
                if (!rescueNode.IsNull()) {
                    column = sumHangColumn(rescueNode, rescuePos);
                }
            }
        }

        // smart-blank-line-on-newline follow-up: mirrors MarkdownMode()'s
        // own addition just above -- a second consecutive Enter on an
        // empty list-continuation line breaks out of the list rather than
        // hang-indenting to the same column again. See that comment for
        // the full reasoning; only duplicated here for the same reason
        // this closure's own contentStart computation already is.
        if (lineStart == lineEnd && column > 0 && lineStart > 0) {
            const std::size_t searchFrom    = (lineStart <= 1) ? 0 : lineStart - 2;
            std::size_t        prevLineStart = bufferText.rfind('\n', searchFrom);
            prevLineStart                  = (prevLineStart == std::string_view::npos) ? 0 : prevLineStart + 1;
            bool prevLineBlank = true;
            for (std::size_t i = prevLineStart; i < lineStart - 1; ++i) {
                if (bufferText[i] != ' ' && bufferText[i] != '\t') {
                    prevLineBlank = false;
                    break;
                }
            }
            if (prevLineBlank) {
                return 0;
            }
        }
        return column;
    };

    // main-editor-sticky-scroll-markdown follow-up: unlike MarkdownMode()'s
    // real tree-sitter "section" nesting, Org headlines have no equivalent
    // in Ned's own tree-sitter-ned-org grammar to lean on -- so this
    // synthesizes the same "runs until the next equal-or-shallower
    // headline" extent org::SubtreeEndLine already defines (in line terms,
    // for fold visibility), directly in byte terms from ParseOutline's
    // flat, level-tagged list. No tree-sitter parse at all: ParseOutline is
    // already a pure, tree-free line scan, so this needs neither parser nor
    // sharedParse. todoKeywords left at its default (DefaultTodoKeywords())
    // deliberately, not the live org::TodoKeywords() -- a headline's own
    // level/lineStartByte (all this reads) never depend on which words are
    // configured as TODO keywords, only title/todoKeyword do, so there's no
    // reason to take a dependency on runtime-mutable global state here.
    // Reuses SymbolKind::Namespace, MarkdownMode()'s own choice for the same
    // reason: heading level already shows up in the sticky row's own
    // reduced-signature text (the stars themselves), no separate kind
    // needed to carry it.
    SymbolKindFunction symbolKind = [](std::string_view bufferText) -> std::vector<SymbolMarker> {
        const std::vector<org::Headline> headlines = org::ParseOutline(bufferText);
        std::vector<SymbolMarker>        markers;
        markers.reserve(headlines.size());
        for (std::size_t i = 0; i < headlines.size(); ++i) {
            std::size_t endByte = bufferText.size();
            for (std::size_t j = i + 1; j < headlines.size(); ++j) {
                if (headlines[j].level <= headlines[i].level) {
                    endByte = headlines[j].lineStartByte;
                    break;
                }
            }
            markers.push_back(SymbolMarker{.startByte = headlines[i].lineStartByte,
                                           .endByte   = endByte,
                                           .kind      = SymbolKind::Namespace,
                                           .name      = std::string()});
        }
        return markers;
    };

    // Real Org's own comment-line convention (org-comment-string's default).
    // line-wrap follow-up: same reasoning as MarkdownMode() -- Org files
    // are prose too.
    return Mode{.name              = "org-mode",
                .keymap            = std::move(keymap),
                .highlight         = std::move(highlight),
                .lineCommentPrefix = "#",
                .autoPairs         = DefaultAutoPairs(),
                .symbolKind        = std::move(symbolKind),
                .indentColumn      = std::move(indentColumn),
                .wrapLines         = true};
}

} // namespace ned::editor
