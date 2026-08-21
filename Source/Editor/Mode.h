//
// Major modes: a name, a keymap layer (composed into a KeymapStack alongside
// the global/Janet layers), and an optional syntax-highlighting hook.
//
// Minor modes deliberately don't get a distinct type -- a minor mode is just
// another Keymap layer added to the same KeymapStack, which already
// generalizes over any number of layers; no new infrastructure needed there.
//

#ifndef NED_EDITOR_MODE_H
#define NED_EDITOR_MODE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Keymap.h"

namespace ned::editor::treesitter {
class Language;
} // namespace ned::editor::treesitter

namespace ned::editor {

// Classification for a single codepoint's rendering, independent of any UI
// toolkit -- BufferView (Source/UI/) is what translates this to an actual
// ox::Brush. Extended well beyond the original 5-member set (tree-sitter
// foundation follow-up, bundle-remaining-grammars follow-up), deliberately
// aiming for JetBrains-IDE-level granularity per an explicit user request,
// not just "enough to prove the mechanism works" -- Default/Comment/String/
// Keyword/Number was fine for a hand-written toy query, but collapsing every
// real grammar's much richer capture set down to just those 5 would waste
// most of what a real highlights.scm is designed to distinguish (a builtin
// function looks the same as a user one, an HTML tag name looks the same as
// plain text, etc.). Every member here maps to a real, standard tree-sitter/
// Neovim capture name (checked against tree-sitter-c's and
// tree-sitter-json's own query files, not invented) -- this still isn't a
// 1:1 mirror of every capture name that exists across every grammar (there
// are dozens more, highly specific ones, e.g. "function.builtin.static");
// SyntaxClassForCapture (Mode.cpp) resolves an unrecognized specific name to
// its nearest recognized ancestor via tree-sitter/Neovim's own dotted
// capture-name convention, not straight to Default.
enum class SyntaxClass {
    Default,
    Comment,
    DocComment, // doc/block comments (e.g. /** ... */, docstrings) -- "comment.documentation"
    String,
    StringEscape, // an escape sequence inside a string literal (e.g. "\n") -- "string.escape"/"escape"
    Number,
    Keyword,
    ControlKeyword, // if/else/for/while/return/... -- distinct from e.g. "class"/"function" -- "keyword.control*"/"keyword.return"/"keyword.conditional"/"keyword.repeat"
    Function,
    FunctionBuiltin, // a language/standard-library-provided function (e.g. Python's print) -- "function.builtin"
    Type,
    TypeBuiltin, // a primitive/standard-library type (e.g. int, string) -- "type.builtin"
    Constant,
    ConstantBuiltin, // true/false/null/nil/... -- "constant.builtin"
    Variable,
    VariableBuiltin, // self/this/... -- "variable.builtin"
    Parameter,       // a function parameter, distinct from a general local variable -- "variable.parameter"
    Property,        // a struct/object field or member access -- "property"/"variable.member"
    Operator,
    Punctuation,
    Tag,       // an HTML/XML/JSX element name -- "tag"
    Attribute, // an HTML/XML attribute name, or a decorator/annotation -- "attribute"
    Namespace, // a module/namespace/package name -- "module"/"namespace"

    // generic-tree-sitter-highlighting follow-up: split out of the coarser
    // classes above once a real, richer query (Source/Editor/TreeSitter/
    // queries/c.scm, cpp.scm) actually reached them -- each still maps to a
    // real, standard tree-sitter/Neovim capture name, not invented (checked
    // against the vendored nvim-treesitter query files themselves).
    KeywordModifier, // an access specifier or storage/type qualifier -- public/private/protected/static/const/... -- "keyword.modifier"
    Method,          // a method (function bound to a type), distinct from a free function -- "function.method"/"function.method.call"
    Constructor,     // "constructor"
    Label,           // a goto target -- "label"
    // Ned's own additions to the vendored query (not a generic tree-sitter/
    // Neovim capture name either upstream splits out -- see cpp.scm's own
    // header comment for why both needed a custom pattern).
    ReturnType,  // a function/method's own return type, distinct from every other @type usage -- "type.return"
    IncludePath, // a "<system/header>" #include path, distinct from a "\"local/header\"" one -- "string.special.include"

    // Org-mode syntax-highlighting follow-up: genuinely Org-specific
    // categories, no cross-language generic capture maps to any of these --
    // the same "real semantic category, not a hue tweak" bar
    // ControlKeyword/FunctionBuiltin already cleared. HeadlineLevel1/2/3
    // cycle every 3 stars (real Org itself cycles through 8 level faces;
    // curated down to 3 here, matching this project's own repeated
    // curated-v1-subset precedent, e.g. priorities capped at A-C).
    // TodoKeyword/DoneKeyword are resolved from org::TodoKeywords()'s own
    // configured list, not hardcoded "TODO"/"DONE" text -- see Mode.cpp's
    // OrgMode(). Strong/Emphasis/Underline/Strikethrough back Org's own
    // *bold*//italic//_underline_/+strikethrough+ inline markup (verbatim
    // and code reuse the existing String class instead -- raw, unformatted
    // text is close in spirit to their real Org default face anyway).
    HeadlineLevel1,
    HeadlineLevel2,
    HeadlineLevel3,
    TodoKeyword,
    DoneKeyword,
    Checkbox,
    Strong,
    Emphasis,
    Underline,
    Strikethrough,

    // Markdown-highlighting follow-up. MarkupMarker is deliberately its own
    // class rather than reusing Punctuation -- Punctuation is tuned for real
    // code punctuation, but markdown's own structural syntax (#, list
    // bullets, >, ---, fenced-code-block delimiters) reads better dimmed/
    // muted so the actual content pops, the same visual idea real markdown
    // renderers (Obsidian, Typora, GitHub) use; a "this is a kind of
    // punctuation" hue tweak wouldn't get that. Link backs both a
    // destination URL and a link/image's own label/description text --
    // reuses Theme's existing linkForeground field, the same one
    // BufferView's Org-descriptive-link rendering already paints with, so
    // both read as the same visual concept.
    MarkupMarker,
    Link,
};

// Interned identity for a tree-sitter capture name (exhaustive-highlighting
// follow-up) -- the raw dotted name (e.g. "function.builtin") turned into a
// small stable id by SyntaxTheme.h's InternCaptureName, so a HighlightSpan
// can carry *which capture* produced it (not just the resolved SyntaxClass)
// without storing a string per span. 0 is reserved for "no capture name" --
// spans synthesized in C++ rather than from a query capture (Org headline
// levels, markdown structural passes) carry 0 and are stylable only at the
// SyntaxClass level, which is exactly the granularity they already resolve
// at. uint16 rather than an enum: the id space is open-ended (any grammar,
// bundled or dlopen'd, can introduce names), unlike the deliberately curated
// SyntaxClass set below.
using CaptureId                       = std::uint16_t;
inline constexpr CaptureId kNoCapture = 0;

// A single highlighted byte range [startByte, endByte) within a buffer's
// full text, tagged with the class it should render as. Replaces the
// original per-line HighlightLineFunction (tree-sitter foundation follow-up):
// a per-line function fundamentally can't handle constructs that span lines
// (block comments, multi-line strings), since it's called once per line with
// no cross-line context at all -- see JanetMode's own doc comment below for
// why that limitation was an accepted, explicit scope cut even before
// tree-sitter existed in this codebase. Spans are not guaranteed
// non-overlapping; where two spans cover the same byte, whichever appears
// *later* in the returned vector wins -- BufferView applies them in order,
// matching how a more specific/nested capture naturally sorts after a less
// specific enclosing one when collected from a tree-sitter query cursor.
struct HighlightSpan {
    std::size_t startByte;
    std::size_t endByte;
    SyntaxClass syntaxClass;
    // Which query capture produced this span (kNoCapture for C++-synthesized
    // spans) -- what makes per-capture-name styling (SyntaxTheme.h's
    // ResolvedCaptureOverride) reachable from the render path; syntaxClass
    // above stays the always-valid base every consumer that doesn't care
    // about capture granularity (Minimap, gutter classification) keeps
    // using unchanged.
    CaptureId captureId = kNoCapture;
};

// Every capture name Mode.cpp's built-in CaptureTable() maps to a
// SyntaxClass, sorted (exhaustive-highlighting follow-up) -- the known
// universe of names the bundled defaults cover, for introspection
// (ned/capture-names merges this with whatever runtime interning has seen).
[[nodiscard]] std::vector<std::string> BuiltinCaptureNames();

// Given a buffer's full text (UTF-8), returns every highlighted span in it.
// Called once per BufferView::paint() call, not once per visible line --
// see that function for how the returned spans get sliced per line.
using HighlightFunction = std::function<std::vector<HighlightSpan>(std::string_view bufferText)>;

// Byte ranges [startByte, endByte) of every foldable block in a buffer's
// full text (a function body, a class body, an object literal, ...),
// generic-code-folding follow-up. Unlike HighlightSpan there's no
// classification -- a fold region either exists or it doesn't -- so this is
// just the "@fold"-captured node's own byte range, one entry per capture,
// order unspecified (BufferView/CodeFold.h sort by startByte themselves
// where it matters). An empty function (the default) means this mode has no
// fold query at all: BufferView must show NO gutter fold affordance for
// such a mode, not merely an inert one -- see CodeFold.h.
using FoldFunction = std::function<std::vector<std::pair<std::size_t, std::size_t>>(std::string_view bufferText)>;

// structural-selection-expansion follow-up. Given a buffer's full text and
// the current selection's byte range [startByte, endByte) (a zero-width
// range at point when there's no mark), returns the byte range of the next
// enclosing named node to expand the selection to -- or std::nullopt if
// [startByte, endByte) already is (or is past) the root node, i.e. there's
// nothing bigger left to expand to. Shrinking back down is not this
// function's job -- BufferView keeps its own expansion-history stack (see
// BufferView.h) rather than asking the tree "what came before," since a
// node can have multiple children and the tree alone can't say which one
// was actually selected on the way up.
using ExpandSelectionFunction =
    std::function<std::optional<std::pair<std::size_t, std::size_t>>(std::string_view bufferText, std::size_t startByte, std::size_t endByte)>;

struct Mode {
    std::string       name;
    Keymap            keymap;
    HighlightFunction highlight; // empty function = no highlighting
    FoldFunction      fold;      // empty function = no folding
    // toggle-line-comment follow-up: the token toggle-line-comment prefixes
    // a line with (plus one following space on insert -- see that
    // command's own doc comment in Commands.cpp), e.g. "//" for C-family
    // languages, "#" for Python/Bash, ";" for Janet (a Lisp). Empty (the
    // default, matching FundamentalMode's own "no special support"
    // convention) means toggle-line-comment reports there's nothing
    // configured rather than guessing.
    std::string lineCommentPrefix;
    // structural-selection-expansion follow-up: empty function (the
    // default) means expand-selection/shrink-selection report there's no
    // structural selection support configured for this mode, same
    // "empty means not configured" convention as highlight/fold above.
    ExpandSelectionFunction expandSelection;
    // line-wrap follow-up: this mode's own default for whether BufferView
    // should soft-wrap long lines at word boundaries instead of scrolling
    // horizontally -- false (matching every bundled mode except the two
    // prose ones below) unless a *Mode() factory sets it, same "plain
    // scalar, most factories leave it alone" convention lineCommentPrefix
    // already established. A per-file override (Editor/WrapOverrides.h)
    // takes precedence over this default when one is configured.
    bool wrapLines = false;
};

// LSP/DAP client follow-up: LspServerConfig.h/DapConfig.h's language keys
// ("c", "python", ...) are Mode's own name minus its "-mode" suffix -- every
// bundled *Mode() factory names itself exactly that way (see
// ModeOverrides.cpp's BundledModeFactories table, e.g. "c-mode"/
// "python-mode"), so this is a free derivation rather than a second naming
// table to keep in sync. Shared by BufferView (LSP sync, DAP
// start-or-continue) and ModeLine (the mode-line-lsp-indicator follow-up) --
// lives here, not in Editor/Lsp/, since it's a property of Mode's own naming
// convention, not anything LSP-specific. Modes with no "-mode" suffix (there
// are none among the bundled ones, but a dynamically-registered one --
// Editor/ModeOverrides.h -- could in principle be named anything) are
// returned unchanged.
[[nodiscard]] std::string LanguageKeyForMode(const Mode& mode);

// The default mode: no special keybindings, no highlighting.
[[nodiscard]] Mode FundamentalMode();

// Builds a Mode backed by a real tree-sitter grammar and a real (embedded,
// see Source/Editor/TreeSitter/Queries.h) queries/highlights.scm query --
// bundle-remaining-grammars follow-up. `languageName` is the name
// treesitter::LanguageByName expects (e.g. "python"); `querySource` is the
// embedded query text for that grammar. Every *Mode() function below is a
// one-line call to this -- factored out once all thirteen turned out to be
// otherwise identical, rather than hand-duplicating the same
// Parser/Query/HighlightFunction-construction logic JsonMode originally
// wrote out in full during the tree-sitter foundation phase.
// foldQuerySource: same embedded-static-storage-duration contract as
// querySource, but for a "@fold"-capture query (generic-code-folding
// follow-up) -- nullptr (the default) means this language has no fold query
// yet, leaving the returned Mode's .fold empty. Not every bundled language
// has one; see Mode.cpp's own *Mode() functions for which do. querySource
// itself is also optional -- nullptr/empty leaves .highlight empty too, for
// a grammar with no highlights.scm at all (e.g. one that only ships a fold
// or locals query).
[[nodiscard]] Mode TreeSitterMode(std::string name, std::string_view languageName, const char* querySource,
                                  const char* foldQuerySource = nullptr);

// The shared construction logic TreeSitterMode above delegates to, split out
// (dynamic-grammar-loading follow-up) so a caller that already has a
// resolved Language -- a dynamically dlopen'd grammar (see
// TreeSitter/DynamicGrammar.h), which by definition isn't in the bundled
// registry TreeSitterMode's own languageName lookup searches -- doesn't need
// to hand-duplicate the Parser/Query/HighlightFunction-construction logic a
// second time. querySource is read here (not stored), unlike TreeSitterMode
// above whose const char* comes from a compile-time-embedded string with
// static storage duration -- safe for the same reason: Query's constructor
// compiles the pattern immediately and doesn't retain the source text past
// that call. querySource is also optional (empty leaves .highlight empty,
// same as an empty foldQuerySource leaves .fold empty) -- some real grammars
// have no highlights.scm at all.
[[nodiscard]] Mode TreeSitterModeFromLanguage(std::string name, const treesitter::Language& language,
                                              std::string_view querySource = {}, std::string_view foldQuerySource = {});

// A real tree-sitter-backed Janet mode (bundle-remaining-grammars
// follow-up), replacing the original hand-rolled per-line #-comment/
// "string" scanner from the tree-sitter foundation phase -- that scanner's
// only job was proving the highlighting hook point worked at all, which the
// JSON mode built alongside it already did more thoroughly; now that a real
// Janet grammar (sogaiu/tree-sitter-janet-simple) is bundled, there's no
// reason to keep the hand-rolled version around as anything but a strictly
// worse duplicate.
[[nodiscard]] Mode JanetMode();

// The first real tree-sitter-backed Mode (tree-sitter foundation follow-up),
// proving the whole Parser -> Tree -> Query -> HighlightSpan pipeline end to
// end. Originally a small, deliberately hand-written query (strings,
// numbers, true/false/null); now upgraded to tree-sitter-json's own real
// queries/highlights.scm like every other mode here, once the
// bundle-remaining-grammars follow-up's CMake resource-embedding mechanism
// existed to make that possible.
[[nodiscard]] Mode JsonMode();

// The remaining bundled grammars (bundle-remaining-grammars follow-up) --
// see Languages.h for the full bundled-vs-Perl-skipped story. TsxMode shares
// TypeScriptMode's query text (queries::kTypeScript) -- tree-sitter-
// typescript's own repo has one top-level queries/highlights.scm covering
// both the typescript/ and tsx/ grammars, confirmed by checking the actual
// repo rather than assumed.
[[nodiscard]] Mode CMode();
[[nodiscard]] Mode CppMode();
[[nodiscard]] Mode PhpMode();
[[nodiscard]] Mode JavaScriptMode();
[[nodiscard]] Mode TypeScriptMode();
[[nodiscard]] Mode TsxMode();
[[nodiscard]] Mode HtmlMode();
[[nodiscard]] Mode CssMode();
[[nodiscard]] Mode PythonMode();
[[nodiscard]] Mode BashMode();
// yaml/toml follow-up: tree-sitter-grammars/tree-sitter-yaml and
// tree-sitter-grammars/tree-sitter-toml, both community-maintained, both
// ship a pre-generated src/parser.c and a real queries/highlights.scm --
// see Languages.h/.cpp.
[[nodiscard]] Mode YamlMode();
[[nodiscard]] Mode TomlMode();
// clojure-and-jank follow-up: one grammar (sogaiu/tree-sitter-clojure) and
// one vendored query (queries::kClojure) serving two distinct mode names --
// jank is a Clojure dialect with no tree-sitter grammar of its own, so
// JankMode shares ClojureMode's grammar/query wholesale (the TsxMode/
// TypeScriptMode sharing pattern) while keeping its own name so the mode
// line reads (jank-mode) in a .jank buffer.
[[nodiscard]] Mode ClojureMode();
[[nodiscard]] Mode JankMode();
// Tables follow-up: unlike every other TreeSitterMode() call above, this
// one's returned Mode gets a real keymap binding (TAB -> markdown-table-
// align, see Editor/Markdown.h) layered on afterward -- the second Mode in
// this codebase to ever construct a non-empty Keymap, OrgMode() below was
// the first.
[[nodiscard]] Mode MarkdownMode();

// Org-like structured editing (v1 slice, see Org.h and ROADMAP.md's
// "Org-like structured editing" entry) -- a real, non-empty keymap (the
// first Mode in this codebase to actually have one; every *Mode() function
// above still constructs a plain empty Keymap()).
//
// Org-mode syntax-highlighting follow-up: also a real `.highlight`, unlike
// every other Mode above, NOT built via the shared TreeSitterMode()/
// TreeSitterModeFromLanguage() template those all use -- OrgMode() builds
// its own Parser/Query (against Ned's own forked "org" grammar and
// Source/Editor/TreeSitter/OrgHighlights.scm) plus a custom
// HighlightFunction that resolves two capture names
// ("org.headline.stars"/"org.keyword.candidate") directly in C++ rather
// than through the shared, generic CaptureTable()/SyntaxClassForCapture()
// mechanism every other capture still goes through -- see Mode.cpp's own
// implementation and OrgHighlights.scm's header comment for why (in short:
// Query::Captures never evaluates tree-sitter predicates, so headline
// level and TODO-vs-DONE, which every reference query for this grammar
// resolves via predicates, have to be resolved here instead, from the
// captured node's own text).
// Binds org-cycle-todo/org-cycle-priority/org-toggle-checkbox under real
// Org's own C-c C-t/C-c C-c bindings, plus C-c C-p for priority -- which
// deliberately SHADOWS the global toggle-project-sidebar binding while an
// Org buffer is active. That's intentional, not an oversight: KeymapStack
// was built from Phase 2 onward specifically so a mode layer can override
// the global layer per buffer (exactly how real Emacs major modes work,
// e.g. C-c C-c means something different in every major mode) -- this is
// simply the first Mode to actually exercise that with a real conflicting
// binding, rather than only ever adding new bindings the global map never
// had. toggle-project-sidebar is unaffected everywhere else. Also binds
// org-cycle (real Org's own 3-state subtree fold cycle) to TAB, and
// org-set-tags to C-c C-q (also real Org's own binding) -- neither shadows
// anything: TAB is unbound in the global keymap (self-insert only covers
// printable ASCII), and C-c C-q was never bound anywhere. Links follow-up:
// also binds open-link-at-point to real Org's own C-c C-o -- this DOES
// shadow the global find-scratch binding while an org-mode buffer is
// active, the same kind of intentional, smoke-tested mode-over-global
// shadow C-c C-p already established above (open-link-at-point is also
// reachable everywhere, Org included, via the global C-c C-l binding --
// C-c C-o is an additional, not exclusive, path to it in Org buffers).
[[nodiscard]] Mode OrgMode();

} // namespace ned::editor

#endif // NED_EDITOR_MODE_H
