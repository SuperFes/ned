//
// import-target-tree-sitter follow-up: per-language parameters for resolving
// an import/include target that Editor/Link.cpp's ResolveFileLink can't find
// as a literal path -- file extensions and package/directory-import index-
// file basenames. Deliberately just data (a small lookup table), not control
// flow: the tree-sitter query that extracts a target (Mode::importTarget,
// Mode.cpp) and the generic ResolveFileLink widening that consumes this
// config are both fully language-agnostic; a file extension is a genuine
// per-language fact with nowhere else to live, the same reasoning
// Lsp/LspServerConfig.h's per-language argv table and
// Editor/ProjectSettings.h's own per-mode includePaths already established.
//

#ifndef NED_EDITOR_IMPORTRESOLUTIONCONFIG_H
#define NED_EDITOR_IMPORTRESOLUTIONCONFIG_H

#include <string>
#include <vector>

namespace ned::editor {

struct ProjectSettings; // ProjectSettings.h

struct ImportResolutionConfig {
    // Tried appended to a target with no extension of its own -- resolves
    // e.g. a JS/TS relative import ("./foo") or a Python dotted module
    // ("foo/bar" after dot-to-slash conversion) written without its real
    // on-disk suffix.
    std::vector<std::string> extensions;
    // Tried as target/basename.extension for each extension above -- the
    // package/directory-import shape ("./foo" -> "./foo/index.js", "foo/bar"
    // -> "foo/bar/__init__.py").
    std::vector<std::string> indexBasenames;
    // When set, BufferView appends Editor/NodeModules.h's
    // NodeModulesSearchPaths(...) to the includePaths handed to
    // ResolveFileLink -- a bare package specifier ("import x from 'lodash'")
    // isn't found relative to the importing file or the project root alone.
    bool searchPackageDirs = false;
};

// Bundled defaults, keyed by Editor/Mode.h's LanguageKeyForMode (e.g.
// "python", "javascript" -- no "-mode" suffix, the same key
// Lsp/LspServerConfig.h/ProjectSettings.h's lspInitializationOptionsByLanguage
// already use). A language with no entry (including every language with no
// import query configured at all) returns a default-constructed
// ImportResolutionConfig -- empty extensions/indexBasenames,
// searchPackageDirs false -- which makes ResolveFileLink's widening a no-op,
// same "absent means nothing configured" convention used throughout this
// codebase.
[[nodiscard]] ImportResolutionConfig DefaultImportResolutionConfig(const std::string& languageKey);

// DefaultImportResolutionConfig(languageKey), with settings' own
// ImportResolutionOverrideForLanguage(languageKey) layered on top: a
// non-empty override extensions/indexBasenames list replaces the bundled
// default's own list outright (not appended -- the same "a project's own
// config wins outright" precedent Editor/ProjectSettings.h's includePaths
// doc comment already establishes for a different field), and
// searchPackageDirs is overridden only when the override actually sets it
// (ProjectSettings::ImportResolutionOverride::searchPackageDirs is an
// optional<bool> for exactly this "inherit unless overridden" reason).
[[nodiscard]] ImportResolutionConfig ResolveImportResolutionConfig(const ProjectSettings& settings,
                                                                    const std::string&      languageKey);

} // namespace ned::editor

#endif // NED_EDITOR_IMPORTRESOLUTIONCONFIG_H
