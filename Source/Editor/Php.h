//
// go-to-file-at-point resolver gaps follow-up: PHP namespace `use` resolution
// via Composer's PSR-4 autoload convention -- its own small file rather than
// folded into Editor/Link.cpp, since PSR-4's prefix-rewrite model (replace
// the longest-matching namespace prefix with its mapped directory) is a
// fundamentally different search shape than ResolveFileLink's
// baseDirectory/ProjectRoot/includePaths widening, not a variation of it.
//

#ifndef NED_EDITOR_PHP_H
#define NED_EDITOR_PHP_H

#include <filesystem>
#include <optional>
#include <string>

namespace ned::editor::php {

// Resolves a PHP namespace path (backslash-separated, e.g. "App\Models\User"
// -- the raw text captured from a `use` statement's own namespace_name/
// qualified_name node, see php-imports.scm's @import.namespace rule) to a
// real source file via projectRoot/composer.json's "autoload"/
// "autoload-dev" "psr-4" prefix -> directory map (PSR-4's own spec: the
// longest-matching namespace prefix wins; its mapped directory replaces the
// prefix, remaining backslash segments become path segments, ".php" is
// appended). A prefix's directory list from composer.json's own multi-value
// form is tried in order. Looks for composer.json at projectRoot only -- no
// upward/recursive search, the same single-location precedent
// Editor/NodeModules.h's own outward walk stops at projectRoot rather than
// searching past it. nullopt if there's no composer.json, no matching
// prefix, or the mapped file doesn't exist on disk -- ResolveFileLink's own
// "a dead link is reported as such, never guessed into a new empty buffer"
// contract, mirrored here even though this bypasses ResolveFileLink itself.
[[nodiscard]] std::optional<std::filesystem::path> ResolvePsr4Namespace(const std::string&           namespacePath,
                                                                        const std::filesystem::path& projectRoot);

} // namespace ned::editor::php

#endif // NED_EDITOR_PHP_H
