//
// A language, declared once. Everything a Mode is built from that is not a
// query pattern -- its grammar, the files it claims, its comment syntax, its
// keymap, which query sources it carries, and the named escapes that supply
// whatever those cannot express -- lives in one LanguageDefinition, and
// ModeFromDefinition is the single path from that declaration to a Mode.
//
// This is the C++ shape of a language's `language.janet`; the bundled
// definitions (BundledLanguages.h) are the literals that file will replace.
// Two rules keep the shape honest:
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

#include "Mode.h"

namespace ned::editor {

// Which bracket/quote set self-insert pairs -- see AutoPair.h. A named choice
// rather than a pair list because that is the whole variation today: the
// Lisp set drops `'` (the reader's quote macro, not a delimiter).
enum class AutoPairSet {
    Default,
    Lisp,
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
    // The query sources, one per kind, every one optional. string_views over
    // text the definition does not own -- compile-time embedded constants
    // for the bundled set (TreeSitter/Queries.h).
    TreeSitterQuerySources queries;
    // Names in the escape registry, applied in order after the generic
    // build. An unknown name is a build error (ModeFromDefinition throws),
    // never a silent no-op: a definition that names an escape means it.
    std::vector<std::string> escapes;
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
// condition), runs the generic tree-sitter build, applies the definition's
// own fields, then each escape in order.
[[nodiscard]] Mode ModeFromDefinition(const LanguageDefinition& definition);

// The same, for a grammar the caller already resolved -- a runtime-loaded
// one (TreeSitter/DynamicGrammar.h) that LanguageByName cannot see.
[[nodiscard]] Mode ModeFromDefinition(const LanguageDefinition& definition, const treesitter::Language& language);

} // namespace ned::editor

#endif // NED_EDITOR_LANGUAGEDEFINITION_H
