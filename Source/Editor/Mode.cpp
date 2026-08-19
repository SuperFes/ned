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
            {"atx_h1_marker", 1}, {"atx_h2_marker", 2}, {"atx_h3_marker", 3},
            {"atx_h4_marker", 4}, {"atx_h5_marker", 5}, {"atx_h6_marker", 6},
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
        } else if (type == "setext_heading") {
            for (std::size_t i = 0; i < node.ChildCount(); ++i) {
                const std::string_view childType = node.Child(i).Type();
                if (childType == "setext_h1_underline" || childType == "setext_h2_underline") {
                    const std::size_t level = (childType == "setext_h1_underline") ? 1 : 2;
                    spans.push_back(HighlightSpan{
                        .startByte = node.StartByte(), .endByte = node.EndByte(), .syntaxClass = HeadlineLevelForStarCount(level)});
                    break;
                }
            }
        } else if (type == "task_list_marker_checked" || type == "task_list_marker_unchecked") {
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
                for (const treesitter::QueryCapture& capture : inlineQuery.Captures(inlineTree.RootNode(), inlineText)) {
                    if (!IsHighlightableCapture(capture.name)) {
                        continue;
                    }
                    spans.push_back(HighlightSpan{
                        .startByte   = start + capture.startByte,
                        .endByte     = start + capture.endByte,
                        .syntaxClass = SyntaxClassForCapture(capture.name),
                    });
                }
            }
            return;
        }
        for (std::size_t i = 0; i < node.ChildCount(); ++i) {
            CollectMarkdownInlineSpans(node.Child(i), bufferText, inlineParser, inlineQuery, spans);
        }
    }

} // namespace

Mode FundamentalMode() {
    return Mode{.name = "fundamental-mode", .keymap = Keymap(), .highlight = HighlightFunction()};
}

Mode TreeSitterModeFromLanguage(std::string name, const treesitter::Language& language, std::string_view querySource,
                                std::string_view foldQuerySource) {
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
        highlight = [parser, query, sharedParse](std::string_view bufferText) -> std::vector<HighlightSpan> {
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

    return Mode{.name            = std::move(name),
                .keymap          = Keymap(),
                .highlight       = std::move(highlight),
                .fold            = std::move(fold),
                .expandSelection = std::move(expandSelection)};
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
    Mode mode              = TreeSitterMode("janet-mode", "janet", treesitter::queries::kJanet);
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
    Mode mode              = TreeSitterMode("c-mode", "c", treesitter::queries::kC, treesitter::queries::kCFolds);
    mode.lineCommentPrefix = "//";
    return mode;
}

Mode CppMode() {
    Mode mode              = TreeSitterMode("cpp-mode", "cpp", treesitter::queries::kCpp, treesitter::queries::kCppFolds);
    mode.lineCommentPrefix = "//";
    return mode;
}

Mode PhpMode() {
    Mode mode              = TreeSitterMode("php-mode", "php", treesitter::queries::kPhp);
    mode.lineCommentPrefix = "//";
    return mode;
}

Mode JavaScriptMode() {
    Mode mode              = TreeSitterMode("javascript-mode", "javascript", treesitter::queries::kJavaScript,
                                            treesitter::queries::kJavaScriptFolds);
    mode.lineCommentPrefix = "//";
    return mode;
}

Mode TypeScriptMode() {
    Mode mode              = TreeSitterMode("typescript-mode", "typescript", treesitter::queries::kTypeScript,
                                            treesitter::queries::kTypeScriptFolds);
    mode.lineCommentPrefix = "//";
    return mode;
}

Mode TsxMode() {
    Mode mode              = TreeSitterMode("tsx-mode", "tsx", treesitter::queries::kTypeScript, treesitter::queries::kTypeScriptFolds);
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
    return TreeSitterMode("css-mode", "css", treesitter::queries::kCss);
}

Mode PythonMode() {
    Mode mode              = TreeSitterMode("python-mode", "python", treesitter::queries::kPython, treesitter::queries::kPythonFolds);
    mode.lineCommentPrefix = "#";
    return mode;
}

Mode BashMode() {
    Mode mode              = TreeSitterMode("bash-mode", "bash", treesitter::queries::kBash);
    mode.lineCommentPrefix = "#";
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
        for (const treesitter::QueryCapture& capture : blockQuery->Captures(root, bufferText)) {
            if (!IsHighlightableCapture(capture.name)) {
                continue;
            }
            const SyntaxClass syntaxClass =
                (capture.name == "punctuation.special") ? SyntaxClass::MarkupMarker : SyntaxClassForCapture(capture.name);
            spans.push_back(HighlightSpan{.startByte = capture.startByte, .endByte = capture.endByte, .syntaxClass = syntaxClass});
        }

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

    // Real Org's own comment-line convention (org-comment-string's default).
    // line-wrap follow-up: same reasoning as MarkdownMode() -- Org files
    // are prose too.
    return Mode{.name = "org-mode", .keymap = std::move(keymap), .highlight = std::move(highlight), .lineCommentPrefix = "#", .wrapLines = true};
}

} // namespace ned::editor
