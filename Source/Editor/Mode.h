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
#include <functional>
#include <string>
#include <string_view>
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
};

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
};

// Given a buffer's full text (UTF-8), returns every highlighted span in it.
// Called once per BufferView::paint() call, not once per visible line --
// see that function for how the returned spans get sliced per line.
using HighlightFunction = std::function<std::vector<HighlightSpan>(std::string_view bufferText)>;

struct Mode {
    std::string       name;
    Keymap            keymap;
    HighlightFunction highlight; // empty function = no highlighting
};

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
[[nodiscard]] Mode TreeSitterMode(std::string name, std::string_view languageName, const char* querySource);

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
// that call.
[[nodiscard]] Mode TreeSitterModeFromLanguage(std::string name, const treesitter::Language& language,
                                              std::string_view querySource);

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
[[nodiscard]] Mode MarkdownMode();

} // namespace ned::editor

#endif // NED_EDITOR_MODE_H
