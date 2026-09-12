//
// The bundled escapes -- what a bundled language needs beyond what its
// definition's queries and the generic build can state, each registered
// under the dotted name its definition refers to (RegisterModeEscape). One
// file per language family; BundledLanguages() calls RegisterBundledEscapes
// once before any definition is built.
//

#ifndef NED_EDITOR_LANGUAGES_ESCAPES_H
#define NED_EDITOR_LANGUAGES_ESCAPES_H

namespace ned::editor::languages {

// "c.line-inspect", "cpp.test-body"
void RegisterCLikeEscapes();
// "markdown.indent" -- highlighting and section breadcrumbs moved to query
// patterns (Source/Languages/markdown/), hanging list indent is the one
// genuine tree walk left
void RegisterMarkdownEscapes();
// "org.indent", "org.symbols" -- highlighting moved to queries + the
// bundled capture classifiers (Plugins/languages.janet)
void RegisterOrgEscapes();

void RegisterBundledEscapes();

} // namespace ned::editor::languages

#endif // NED_EDITOR_LANGUAGES_ESCAPES_H
