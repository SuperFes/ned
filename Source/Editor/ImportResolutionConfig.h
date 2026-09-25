//
// import-target-tree-sitter follow-up: per-language parameters for resolving
// an import/include target that Editor/Link.cpp's ResolveFileLink can't find
// as a literal path -- file extensions and package/directory-import index-
// file basenames. Deliberately just data (a small lookup table), not control
// flow: the tree-sitter query that extracts a target (Mode::importTarget,
// Mode.cpp) and the generic ResolveFileLink widening that consumes this
// config are both fully language-agnostic; a file extension is a genuine
// per-language fact with nowhere else to live, the same reasoning
// Lsp/ServerConfig.h's per-language argv table and
// Editor/ProjectSettings.h's own per-mode includePaths already established.
//

#ifndef NED_EDITOR_IMPORTRESOLUTIONCONFIG_H
#define NED_EDITOR_IMPORTRESOLUTIONCONFIG_H

#include <string>
#include <utility>
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
    // How a module path (an "@import.module" capture) spells one directory
    // step: Python's "pkg.mod", Perl's "Foo::Bar".
    std::string moduleSeparator = ".";
    // Rewrites applied to each step of a module path to get its on-disk
    // spelling, as {module spelling, path spelling}: Clojure's
    // "my-app.core" lives at "my_app/core.clj". Reversed when a moved file's
    // path is turned back into a module path.
    std::vector<std::pair<std::string, std::string>> moduleSubstitutions;
    // Directories a module or path is also looked up under, relative to the
    // language's package root (the nearest ancestor holding one of its LSP
    // root markers) and then the project root: Maven's "src/main/java",
    // Perl's "lib", Elm's "src".
    std::vector<std::string> sourceRoots;
    // Prepended to a target's file name as a further candidate: Sass's
    // `@use "base/vars"` is the partial "base/_vars.scss".
    std::string partialPrefix;
    // A target starting with one of these prefixes names a file under the
    // language's package root (as sourceRoots finds it) plus a directory,
    // and nowhere else: Godot's "res://", as {prefix, directory}.
    std::vector<std::pair<std::string, std::string>> rootPrefixes;
    // Dart's package URI scheme ("package:"): "package:name/path" is path
    // under package `name`'s library directory, as the nearest
    // .dart_tool/package_config.json maps it, or as the enclosing
    // pubspec.yaml names its own package when there is none.
    std::string packageScheme;
    // A `~/` path is counted from $HOME (ssh_config, gitconfig). Opt-in: to
    // JS tooling `~/` is a project alias.
    bool homePrefix = false;
    // Go's import paths (Editor/GoModules.h): a path is found under the
    // nearest go.mod's module, a required module or the standard library,
    // and nowhere else.
    bool goModules = false;
    // How an import is written, `{}` standing for the module path
    // ("import {};"): what a move inserts where a file newly needs one.
    std::string importStatement;
    // Odin's `collection:path` imports (Editor/OdinCollections.h).
    bool odinCollections = false;
    // An import names a directory, and go-to-file opens the file that stands
    // for it: doc.<ext>, else <directory name>.<ext>, else the first file
    // with one of `extensions` that isn't a `_test` file. A fixup follows
    // the directory only when every such file in it moved to one place.
    bool packageDirectories = false;
};

// Bundled defaults, keyed by Editor/Mode.h's LanguageKeyForMode (e.g.
// "python", "javascript" -- no "-mode" suffix, the same key
// Lsp/ServerConfig.h/ProjectSettings.h's lspInitializationOptionsByLanguage
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
                                                                   const std::string&     languageKey);

} // namespace ned::editor

#endif // NED_EDITOR_IMPORTRESOLUTIONCONFIG_H
