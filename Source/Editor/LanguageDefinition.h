//
// A language, declared once. Everything a Mode is built from that is not a
// query pattern -- its grammar, the files it claims, its comment syntax, its
// keymap, which query sources it carries, and the named escapes that supply
// whatever those cannot express -- lives in one LanguageDefinition, and
// ModeFromDefinition is the single path from that declaration to a Mode.
//
// This is the C++ shape of a language's `language.janet`
// (Editor/LanguageParse.h reads one; BundledLanguages.h holds the bundled
// set). Two rules keep the shape honest:
//
//   - A definition is data. It names a grammar, query sources and escapes; it
//     holds no closures. Anything a language needs that the generic build
//     cannot derive is an ESCAPE: a name, resolved through RegisterModeEscape
//     at build time, whose implementation may be C++ today and Janet later
//     without the definition changing.
//   - The generic build comes first and an escape decorates it. An escape
//     receives the ModeBuildContext the generic capabilities were built over,
//     so a closure it installs rides the same parser and incremental cache
//     rather than parsing again -- Org's and Markdown's own highlight and
//     indent, C++'s test-body widening.
//

#ifndef NED_EDITOR_LANGUAGEDEFINITION_H
#define NED_EDITOR_LANGUAGEDEFINITION_H

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ImportResolutionConfig.h"
#include "Mode.h"

namespace ned::editor {

// How a capture's span is adjusted before it becomes a HighlightSpan --
// see LanguageDefinition::captureSpans.
enum class CaptureSpanRule {
    LineEnd, // extend through the end of the line the capture ends on
};

// Which bracket/quote set self-insert pairs -- see AutoPair.h. A named choice
// rather than a pair list because that is the whole variation today: the
// Lisp set drops `'` (the reader's quote macro, not a delimiter).
enum class AutoPairSet {
    Default,
    Lisp,
};

// A language's query files, one list per kind, each entry a path
// Editor/LanguageFiles.h can read -- "cpp/highlights.janet" for a bundled
// file, an absolute path for a user's. Several files concatenate in order:
// an upstream query ned consumes unmodified (`<name>/upstream/<kind>.janet`)
// with ned's own delta after it. Empty = the language has no query of that
// kind, and the corresponding Mode capability stays unset.
struct QueryFiles {
    std::vector<std::string> highlights;
    std::vector<std::string> folds;
    std::vector<std::string> imports;
    std::vector<std::string> tags;
    std::vector<std::string> tests;
    std::vector<std::string> indents;
    std::vector<std::string> locals;
    std::vector<std::string> injections;
};

struct LanguageDefinition {
    // The language key -- "cpp", "python" -- which is also the LSP/DAP/task
    // config key and the imprint table key. The Mode is named "<name>-mode"
    // (ModeNameFor); LanguageKeyForMode is the inverse.
    std::string name;
    // The bundled grammar (treesitter::LanguageByName). Defaults to `name`
    // when empty and a grammar exists; jank names "clojure", tsx names "tsx".
    // A definition with no grammar at all sets `grammarless` instead.
    std::string grammar;
    bool        grammarless = false; // fundamental-mode: no parser, no capabilities
    // File-name matching: extensions carry their leading dot (".cpp"), the
    // form std::filesystem::path::extension() hands back; filenames match
    // the whole basename and are checked first (Emacs' auto-mode-alist).
    std::vector<std::string> extensions;
    std::vector<std::string> filenames;
    // Empty = no line-comment syntax (JSON, HTML, CSS, XML, Markdown).
    std::string lineCommentPrefix;
    AutoPairSet autoPairs = AutoPairSet::Default;
    bool        wrapLines = false;
    // Whether the injections query's regions are exposed as
    // Mode::embeddedRegions for LSP sync. Off by default: a Markdown fenced
    // block is highlighted as its language but not sent to that language's
    // server; HTML's <script>/<style> are.
    bool embeddedDocuments = false;
    // (key sequence in ParseKeySequence's syntax, command name).
    std::vector<std::pair<std::string, std::string>> keymap;
    // Per-language capture -> SyntaxClass defaults, consulted ahead of the
    // shared CaptureTable for this language only (Markdown's
    // "punctuation.special" is a MarkupMarker, everyone else's is
    // Punctuation). User remaps (SyntaxTheme.h) still win over these.
    std::vector<std::pair<std::string, SyntaxClass>> captureClasses;
    // Captures whose spans this language drops outright (":suppress" in
    // :capture-classes) -- the one legitimate negative: markdown's upstream
    // @text.title would re-class a heading's title out of its own
    // whole-line wash, and "contribute nothing" is not sayable by mapping.
    std::vector<std::string> suppressedCaptures;
    // Declarative span adjustment per capture name, applied by the generic
    // highlight before the span joins the collection: LineEnd extends the
    // span through its last line's remaining text (Org's headline wash
    // covers the whole line, not just the stars). Data rather than a hook:
    // "how far the span reaches" has a small closed set of useful answers.
    std::vector<std::pair<std::string, CaptureSpanRule>> captureSpans;
    QueryFiles                                       queries;
    // Query discovery borrows another language's directory (jank reads
    // clojure's, tsx typescript's); explicit `queries` entries still win per
    // kind. See LanguageParse.h.
    std::string queriesFrom;
    // Runtime-registered languages only (LanguageRegistry.h; both rejected
    // at load for a bundled definition): a shared library exporting
    // `tree_sitter_<grammar>` to dlopen, and a foreign tree-sitter-layout
    // queries directory scanned per kind as `<kind>.janet` or `<kind>.scm`.
    std::string grammarLibrary;
    std::string queriesDir;
    // Names in the escape registry, applied in order after the generic
    // build. An unknown name is a build error (ModeFromDefinition throws),
    // never a silent no-op: a definition that names an escape means it.
    std::vector<std::string> escapes;
    // Root-marker filenames for per-subpackage LSP roots
    // (Lsp/RootResolver.h; "*.csproj" means any file with that extension).
    // Empty is meaningful: the marker tier never matches and the buffer's
    // root falls through to ProjectRoot().
    std::vector<std::string> lspRootMarkers;
    // How this language's import specifiers resolve to files
    // (Editor/ImportResolutionConfig.h); unset = default-constructed config.
    std::optional<ImportResolutionConfig> importResolution;
    // Shorthand tags (a Markdown fence tag, an injections.scm #set! value)
    // that mean this language -- "js" on javascript, "yml" on yaml.
    // Injection.cpp builds the alias -> canonical map from these.
    std::vector<std::string> injectionAliases;
    // Bundled snippet (trigger, body) pairs, TextMate syntax
    // (Editor/Snippet.h) -- installed into Editor/SnippetRegistry.h under
    // this language's key at startup (Editor/BundledSnippets.h).
    std::vector<std::pair<std::string, std::string>> snippets;
};

// The Mode name a definition builds under: "<name>-mode".
[[nodiscard]] std::string ModeNameFor(const LanguageDefinition& definition);

// An escape decorates the generically built Mode. `context` is what that
// build used (see ModeBuildContext in Mode.h); for a grammarless definition
// its pointers are null.
using ModeEscape = std::function<void(Mode& mode, const LanguageDefinition& definition, const ModeBuildContext& context)>;

// Registering the same name twice replaces the earlier escape -- a Janet
// redefinition of a bundled escape is expected use, the same rule
// CommandRegistry applies to commands.
void               RegisterModeEscape(std::string name, ModeEscape escape);
[[nodiscard]] bool HasModeEscape(std::string_view name);

// The one path from a definition to a Mode. Resolves the grammar by name
// (throws std::runtime_error for a grammar that is not bundled -- a
// definition naming one is a build-time regression, not a runtime
// condition), compiles each query kind's files (Editor/LanguageFiles.h;
// a malformed one throws naming `path:line`), runs the generic tree-sitter
// build, applies the definition's own fields, then each escape in order.
[[nodiscard]] Mode ModeFromDefinition(const LanguageDefinition& definition);

// The same, for a grammar the caller already resolved -- a runtime-loaded
// one (TreeSitter/DynamicGrammar.h) that LanguageByName cannot see.
[[nodiscard]] Mode ModeFromDefinition(const LanguageDefinition& definition, const treesitter::Language& language);

} // namespace ned::editor

#endif // NED_EDITOR_LANGUAGEDEFINITION_H
