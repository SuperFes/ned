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

// Emacs-keymap-round-2 follow-up: forward-sexp/backward-sexp support. Given
// a buffer's full text, point, and a direction (true = forward, false =
// backward), returns the new point after moving over one balanced
// syntactic unit at the sibling level point sits in -- or std::nullopt at
// the buffer's start/end, or when there's no parse tree at all. Unlike
// ExpandSelectionFunction this also walks *sideways* (next/previous named
// sibling), not just up -- "next sexp" means "next sibling form," not
// "next enclosing node."
using SexpMotionFunction = std::function<std::optional<std::size_t>(std::string_view bufferText, std::size_t point, bool forward)>;

// import-target-tree-sitter follow-up: go-to-file-at-point for import/include
// statements (open-link-at-point, C-c C-l), generalized across every
// language via a per-mode tree-sitter query rather than per-language C++
// text scanning -- see Editor/Link.h's own doc comment for the generic,
// language-agnostic bare-URL/path detection this sits in front of. `target`
// is the raw captured text; `isModulePath` distinguishes a dotted module
// name (Python's `import foo.bar`, no delimiters -- caller converts '.' to
// '/' before resolving) from a literal, possibly still quote/angle-bracket-
// delimited path (`isModulePath == false`, e.g. C's #include or JS's
// import). [startByte, endByte) is the resolvable range: the query's own
// "@import.statement" capture when present (so point anywhere in the whole
// statement resolves, not just on the target's own bytes -- e.g. point on
// an imported *name* in Python's "from foo.bar import baz" still resolves
// to "foo.bar"), else the target/module capture's own range.
//
// go-to-file-at-point resolver gaps follow-up: two more orthogonal fields,
// each defaulting to "not this kind" so every existing @import.target/
// @import.module producer (every query file except python-imports.scm's new
// @import.relative rule and php-imports.scm's new @import.namespace rule)
// is unaffected. `relativeLevel` (isModulePath == true, > 0) is Python's own
// leading-dot relative-import count ("from . import x" == 1, "from ..foo
// import x" == 2, ...) -- BufferView ascends that many-minus-one parent
// directories from the importing file's own directory before resolving
// `target` (already dot-to-slash converted, empty for a bare "from . import
// x"), rather than searching baseDirectory/ProjectRoot()/includePaths the
// way an ordinary module path does. `isNamespacePath` is PHP's own
// backslash-separated `use` namespace (never dot-to-slash converted --
// resolved by a dedicated PSR-4 lookup, Editor/Php.h's ResolvePsr4Namespace,
// not ResolveFileLink's generic search at all). `isModDeclaration` is
// Rust's own bodyless `mod foo;` file-per-module declaration -- `target` is
// the bare module identifier (no delimiters, isModulePath stays false); the
// real per-language wrinkle is *where* it resolves from, not its own text:
// a submodule of any file other than a crate root/mod.rs (main.rs, lib.rs,
// or a directory's own mod.rs) lives one directory level *below* the
// importing file, under a subdirectory named after that file's own stem
// (rust-imports.scm's own header comment has the full "why not `use`"
// reasoning) -- BufferView prepends that stem to baseDirectory before
// resolving, the same "per-language baseDirectory adjustment" shape
// relativeLevel above already establishes for Python.
struct ImportTarget {
    std::string target;
    bool        isModulePath;
    std::size_t startByte;
    std::size_t endByte;
    int         relativeLevel    = 0;
    bool        isNamespacePath  = false;
    bool        isModDeclaration = false;
};
using ImportTargetFunction =
    std::function<std::optional<ImportTarget>(std::string_view bufferText, std::size_t point)>;

// gutter-symbol-kind follow-up: a coarse landmark kind for the gutter's
// symbol-kind column -- deliberately not a reuse of SyntaxClass's own finer
// Function/FunctionBuiltin/Type/TypeBuiltin/Constant/ConstantBuiltin split
// (those exist for syntax *coloring*; this is a single-glyph gutter
// indicator, three buckets is plenty). SyntaxClassFor below is what ties a
// SymbolKind back to a real theme color, so a custom theme needs no new
// fields of its own for this.
enum class SymbolKind {
    Callable,  // a function or method definition
    TypeLike,  // a class/interface/type-alias/enum/struct/module definition
    Data,      // a constant/variable-like definition
    Namespace, // main-editor-sticky-scroll follow-up: a namespace definition
               // -- distinct from TypeLike so a breadcrumb (or gutter glyph)
               // doesn't conflate "namespace foo" with a class/struct
};

// The SyntaxClass a SymbolKind's gutter glyph borrows its color from --
// Callable renders like a function name would in the buffer, TypeLike like a
// type name, Data like a constant.
[[nodiscard]] SyntaxClass SyntaxClassFor(SymbolKind kind);

// One "definition site" landmark -- startByte is the definition capture's
// own start (BufferView maps it to a line via ByteOffsetToLine the same way
// diagnostics/diff hunks already do; a definition spanning multiple lines,
// e.g. a multi-line function signature, only ever marks its first line).
// endByte and name (main-editor-sticky-scroll follow-up) are the whole
// definition node's own end and its captured "@name" text -- both were
// unused by the original consumer (the symbol-kind gutter glyph, which only
// ever needed a kind and a line to put it on) and so weren't captured at
// all; sticky scroll's enclosing-chain resolution needs the full range for
// containment and the name for the breadcrumb label. name is empty if the
// query's match had no "name" capture for some reason (shouldn't happen per
// the tags.scm convention every bundled query follows, but never assumed).
struct SymbolMarker {
    std::size_t startByte;
    std::size_t endByte = 0;
    SymbolKind  kind;
    std::string name;
};

// Given a buffer's full text, returns every definition-site landmark in it
// -- one entry per matched "@definition.*" capture from the language's own
// queries/tags.scm (the ctags/nvim-treesitter convention; see
// SymbolKindFromCaptureName below). Empty function (the default) means "no
// symbol-kind support configured for this mode," same "empty means not
// configured" convention as HighlightFunction/FoldFunction above -- most
// bundled grammars have no tags.scm at all (JSON/HTML/CSS/... have no
// meaningful function/class-definition concept), so this stays empty for
// them rather than guessing.
using SymbolKindFunction = std::function<std::vector<SymbolMarker>(std::string_view bufferText)>;

// Maps a tags.scm capture name (without the leading '@', e.g.
// "definition.function") onto a SymbolKind -- nullopt for anything that
// isn't itself a *definition* capture: a nested "@name"/"@doc"/
// "@local.scope" capture from the same pattern match, or a "@reference.*"
// one (a *use*, not a definition -- tags.scm files mix both in the same
// query). Shared by every TreeSitterModeFromLanguage-built symbolKind
// closure (Mode.cpp) rather than duplicated per language, since this naming
// vocabulary is the same ctags convention across every grammar that ships a
// tags.scm, not something each language's query invents independently.
[[nodiscard]] std::optional<SymbolKind> SymbolKindFromCaptureName(std::string_view captureName);

// test-runner integration: one discovered test definition. [startByte,
// endByte) covers the whole definition including its body where the parse
// gives one (run-test-at-point's innermost-containing-point resolution
// needs the body; the gutter only ever reads startByte's line); name is
// the test's own name as the framework would report it (string-literal
// delimiters already stripped), what the gutter matches against a
// TestRunOutcome's result names.
struct TestMarker {
    std::size_t startByte;
    std::size_t endByte;
    std::string name;
};

// Given a buffer's full text, returns every test definition in it, in tree
// order -- one entry per "@test.definition"/"@test.name" capture pair from
// the language's own *-tests.scm query (a ned-local capture convention,
// mirroring importTarget's fixed "import.*" trio -- there is no upstream
// tests.scm convention to borrow). Empty function (the default) means "no
// test discovery configured for this mode," the standing convention.
using TestDiscoveryFunction = std::function<std::vector<TestMarker>(std::string_view bufferText)>;

// embedded-language-documents follow-up: one tree-sitter injection match's
// resolved (host-buffer byte range, canonical target language) pair --
// Injection.h's CollectInjectionRegions is what produces these. Lives here
// (not Injection.h) so Mode::embeddedRegions below can name the type without
// Mode.h depending on Injection.h -- the same reasoning ImportTarget/
// SymbolMarker/TestMarker above are defined here rather than in whatever
// consumes them.
struct InjectionRegion {
    std::size_t startByte;
    std::size_t endByte;
    std::string language; // canonical name, e.g. "javascript" -- see Injection.cpp's CanonicalEmbeddedLanguageName
};

// embedded-language-documents follow-up: given a buffer's full text, returns
// every LSP-syncable embedded-language region in it (an HTML <script>/
// <style> block's own content range). Distinct from HighlightFunction's own
// injection handling (Injection.h's CollectInjectedHighlightSpans), which
// produces colored spans, not byte ranges + language identity -- this is what
// Editor/EmbeddedDocuments.h's BuildEmbeddedDocuments consumes to sync each
// embedded language to its own real LSP server. Empty function (the default)
// means this mode has no LSP-syncable embedded regions at all, same "empty
// means not configured" convention as fold/importTarget/symbolKind/
// testDiscovery above -- every bundled mode except html-mode leaves this
// unset.
using EmbeddedRegionFunction = std::function<std::vector<InjectionRegion>(std::string_view bufferText)>;

// smart-indentation follow-up. bufferText is the buffer's full text;
// [lineStart, lineEnd) is the byte range of the line indentation is being
// computed FOR (lineEnd excludes that line's own trailing newline; a
// not-yet-typed new line passes lineStart == lineEnd, its own future
// insertion point). Returns the target visual column that line's leading
// whitespace should occupy, or std::nullopt for "no opinion" (e.g. no parse
// tree at all) -- callers fall back to whatever they'd do with this
// capability unset entirely, same "empty means not configured" convention
// as every other Mode field above, just resolved per-call rather than by
// leaving the whole std::function empty, since a query-driven mode can have
// a real opinion on most lines and none on a pathological one.
//
// Unlike HighlightFunction/FoldFunction (whole-buffer, no line parameter),
// this is inherently a per-line query -- indentation is never a property of
// the whole buffer at once. Two implementation shapes share this one type
// (see Editor/Indent.h's own doc comment): most modes get one built by the
// generic "@indent"/"@dedent" tree-walk engine off a per-language
// indents.scm (TreeSitterModeFromLanguage, mirroring FoldFunction/
// importTarget/testDiscovery's own construction); Markdown's own closure is
// hand-rolled directly in MarkdownMode(), mirroring how that Mode's
// .highlight already bypasses the generic query path for logic a flat
// capture list can't express (list-item hanging indent needs the bullet's
// own content column, not a multiple of one fixed indent width).
using IndentFunction = std::function<std::optional<int>(std::string_view bufferText, std::size_t lineStart, std::size_t lineEnd)>;

// Debugging wishlist (line-inspect follow-up): same 3-arg per-line shape as
// IndentFunction above, returning byte ranges of candidate sub-expressions
// on [lineStart, lineEnd) worth evaluating in a stopped debug session
// (dap-line-inspect), in source order. Two implementation tiers share this
// one type (see Mode.cpp's shared tree-walk helper): every
// TreeSitterModeFromLanguage-built mode gets a generic, universal default
// (bare identifiers only -- "identifier" is a near-universal tree-sitter
// node-type name, needing no per-language query authoring at all); CMode/
// CppMode override it with a richer version additionally matching real,
// grammar-verified compound-expression node types (member access, calls,
// indexing, ...). Empty function (JanetMode/OrgMode, the two hand-built
// exceptions) means the same "not configured" signal as everything above.
using LineInspectFunction =
    std::function<std::vector<std::pair<std::size_t, std::size_t>>(std::string_view bufferText, std::size_t lineStart, std::size_t lineEnd)>;

// A dense line can otherwise fan out unboundedly many DAP evaluate requests
// -- LineInspectFunction never returns more than this many candidates.
// Public (not Mode.cpp-private) so BufferView can tell "capped" apart from
// "this really was every candidate on the line" when a result hits exactly
// this count.
inline constexpr std::size_t kMaxLineInspectExpressions = 16;

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
    // Emacs-keymap-round-2 follow-up: empty function (the default) means
    // forward-sexp/backward-sexp report there's no sexp motion configured
    // for this mode, same "empty means not configured" convention as
    // highlight/fold/expandSelection above.
    SexpMotionFunction sexpMotion;
    // auto-pair-brackets-and-quotes follow-up: the (opener, closer) pairs
    // self-insert-command/backward-delete-char auto-close/skip-over/delete
    // as a unit for this mode -- empty (the default) means no pairing at
    // all, same "empty means not configured" convention as highlight/fold
    // above. Most *Mode() factories set this to Editor/AutoPair.h's
    // DefaultAutoPairs(); Lisp-family modes (Janet/Clojure/Jank) use
    // LispAutoPairs() instead, dropping the '' entry since a bare quote
    // there is the reader's own quote macro, not a paired delimiter.
    std::vector<std::pair<char, char>> autoPairs;
    // gutter-symbol-kind follow-up: empty function (the default) means no
    // symbol-kind support configured for this mode, same "empty means not
    // configured" convention as highlight/fold/expandSelection/sexpMotion/
    // importTarget above -- BufferView's symbol-kind gutter column simply
    // never reserves space for a Mode with this unset.
    SymbolKindFunction symbolKind;
    // import-target-tree-sitter follow-up: empty function (the default)
    // means open-link-at-point has no import/include query configured for
    // this mode, same "empty means not configured" convention as
    // fold/expandSelection/sexpMotion above -- BufferView falls back to
    // Editor/Link.h's generic, mode-agnostic bare-URL/path detection.
    ImportTargetFunction importTarget;
    // test-runner integration: empty function (the default) means no test
    // discovery configured for this mode, same "empty means not configured"
    // convention as everything above -- run-test-at-point reports it, and
    // BufferView's test gutter simply never activates.
    TestDiscoveryFunction testDiscovery;
    // embedded-language-documents follow-up: empty function (the default)
    // means this mode has no LSP-syncable embedded-language regions, same
    // "empty means not configured" convention as everything above -- only
    // html-mode sets this (see HtmlMode() in Mode.cpp).
    EmbeddedRegionFunction embeddedRegions;
    // smart-indentation follow-up: empty function (the default) means
    // indent-for-tab-command/newline/indent-region/indent-buffer report
    // there's no structural indent support configured for this mode, same
    // "empty means not configured" convention as everything above -- TAB/RET
    // fall back to their pre-existing literal-tab/bare-newline behavior
    // unchanged.
    IndentFunction indentColumn;
    // Debugging wishlist (line-inspect follow-up): empty function (the
    // default) means dap-line-inspect reports there's no expression
    // extraction configured for this mode, same "empty means not
    // configured" convention as everything above.
    LineInspectFunction lineInspect;
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
// importQuerySource (import-target-tree-sitter follow-up): same optional,
// embedded-static-storage-duration contract as foldQuerySource, but for an
// "@import.target"/"@import.module"/"@import.statement"-capture query (see
// Mode::importTarget's own doc comment) -- nullptr (the default) means this
// language has no import query yet, leaving the returned Mode's
// .importTarget empty.
// symbolKindQuerySource (gutter-symbol-kind follow-up): same optional
// contract, but for a "@definition.*"-capture tags.scm query (see
// Mode::symbolKind's own doc comment) -- nullptr (the default) means this
// language has no tags query yet, leaving the returned Mode's .symbolKind
// empty.
// testQuerySource (test-runner integration): same optional contract, but
// for a "@test.definition"/"@test.name"-capture query (see
// Mode::testDiscovery's own doc comment) -- nullptr (the default) means
// this language has no test-discovery query yet, leaving the returned
// Mode's .testDiscovery empty.
// indentQuerySource (smart-indentation follow-up): same optional contract,
// but for an "@indent"/"@dedent"-capture query (see Mode::indentColumn's own
// doc comment and Editor/Indent.h) -- nullptr (the default) means this
// language has no indent query yet, leaving the returned Mode's
// .indentColumn empty.
[[nodiscard]] Mode TreeSitterMode(std::string name, std::string_view languageName, const char* querySource,
                                  const char* foldQuerySource = nullptr, const char* importQuerySource = nullptr,
                                  const char* symbolKindQuerySource = nullptr, const char* testQuerySource = nullptr,
                                  const char* indentQuerySource = nullptr);

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
// have no highlights.scm at all. importQuerySource: same optional contract,
// see TreeSitterMode's own doc comment above. indentQuerySource: same
// optional contract, see TreeSitterMode's own doc comment above.
[[nodiscard]] Mode TreeSitterModeFromLanguage(std::string name, const treesitter::Language& language,
                                              std::string_view querySource = {}, std::string_view foldQuerySource = {},
                                              std::string_view importQuerySource     = {},
                                              std::string_view symbolKindQuerySource = {},
                                              std::string_view testQuerySource       = {},
                                              std::string_view indentQuerySource     = {});

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
// ram02z/tree-sitter-fish -- same "community-maintained, ships a real
// queries/highlights.scm" bar as yaml/toml below.
[[nodiscard]] Mode FishMode();
// tree-sitter-grammars/tree-sitter-xml -- same community-maintained-grammar
// bar as fish/yaml/toml above; a real generic XML/DTD grammar (elements,
// attributes, entities, CDATA, DOCTYPE, processing instructions), not a
// reuse of HtmlMode's HTML-specific one.
[[nodiscard]] Mode XmlMode();
// tree-sitter/tree-sitter-rust -- the tree-sitter org's own official
// grammar, same provenance bar as CMode/CppMode/PythonMode/
// JavaScriptMode above. Ships a real queries/highlights.scm and
// queries/tags.scm, both consumed unmodified (no c-tags.scm/cpp-tags.scm-
// style vendoring needed -- checked directly, no ambiguity found).
[[nodiscard]] Mode RustMode();
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
// Scheduling/recurrence follow-up: also binds org-schedule/org-deadline to
// real Org's own C-c C-s/C-c C-d -- another deliberate mode-over-global
// shadow (project-search/create-directory, respectively, same precedent
// C-c C-p/C-c C-o already established), not reachable by any other global
// binding while an Org buffer is focused.
[[nodiscard]] Mode OrgMode();

} // namespace ned::editor

#endif // NED_EDITOR_MODE_H
