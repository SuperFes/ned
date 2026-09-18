//
// Runtime-registered languages -- the same LanguageDefinition the bundled
// set uses, loaded from a directory outside the bundled data tree
// (`DataDir()/languages`, Editor/DataDir.h). One loader for every source: a
// user's own `$XDG_CONFIG_HOME/ned/languages/<name>/`, a project's
// `.ned/languages/<name>/` (trust-gated in main.cpp exactly like
// `.ned/init.janet` -- a definition may dlopen a scanner library, which is
// code by any measure) and the `ned/register-language` Janet binding; the
// bundled set itself is enumerated with this file's LanguageDirectories.
//
// A directory is the bundled layout: `<name>/language.janet` plus query
// files discovered by the same convention (LanguageParse.h), and -- when
// the language brings its own grammar rather than borrowing a bundled one
// through `:grammar` -- its `grammar.janet`/`tables`, loaded as a language
// package (Grammar/LanguagePackage.h). Two extra definition keys only make
// sense here and are parsed for every definition: `:scanner-library` (the
// shared library holding the grammar's external scanner, dlopen'd at
// registration) and `:queries-dir` (a foreign tree-sitter-layout directory,
// e.g. /usr/share/tree-sitter/queries/<lang>, scanned for `<kind>.janet` or
// `<kind>.scm` per kind -- what lets a grammar's own upstream queries work
// unconverted).
//
// A registered name SHADOWS a bundled one everywhere a lookup goes through
// FindLanguageDefinition -- re-registering replaces, the CommandRegistry
// convention. Modes are still rebuilt fresh per ModeByName lookup (a shared
// parser across threads is the coredump ModeOverrides.cpp documents); the
// loaded package itself is shared, which is safe -- its tables are
// immutable.
//

#ifndef NED_EDITOR_LANGUAGEREGISTRY_H
#define NED_EDITOR_LANGUAGEREGISTRY_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "LanguageDefinition.h"
#include "Grammar/Parser.h"

namespace ned::editor {

struct RegisteredLanguage {
    LanguageDefinition definition;
    // Set when the directory holds its own grammar (grammar.janet or
    // tables); absent means the grammar resolves through the bundled
    // registry (or is :none).
    std::optional<grammar::Language> language;
};

// Loads `<directory>/language.janet` (the directory's basename is the
// language name), discovers its query files, loads its own grammar package
// if it has one (with :scanner-library, if named), and registers the result. Throws std::runtime_error with a
// path-qualified message on any failure -- a caller surfaces it, nothing is
// half-registered.
void LoadLanguageDirectory(const std::filesystem::path& directory);

void RegisterLanguage(RegisteredLanguage language);

// The `<root>/<name>/` subdirectories that contain a language.janet, sorted
// by name -- what main.cpp iterates for the user/project language roots
// (each project entry then goes through its own trust check).
[[nodiscard]] std::vector<std::filesystem::path> LanguageDirectories(const std::filesystem::path& root);

[[nodiscard]] std::optional<RegisteredLanguage> FindRegisteredLanguage(std::string_view name);

// Registered first (a user redefining a bundled language is expected use),
// then bundled. The one lookup every derived per-language fact goes
// through -- root markers, import resolution, injection aliases.
[[nodiscard]] std::optional<LanguageDefinition> FindLanguageDefinition(std::string_view name);

// Snapshot of every registered definition, for table-shaped consumers
// (extension resolution, the injection alias map).
[[nodiscard]] std::vector<RegisteredLanguage> RegisteredLanguages();

// Test hygiene: registration is process-wide state.
void ClearRegisteredLanguages();

} // namespace ned::editor

#endif // NED_EDITOR_LANGUAGEREGISTRY_H
