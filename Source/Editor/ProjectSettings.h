//
// Project-local, declarative (non-executable) settings -- <root>/.ned/settings.json.
// Deliberately plain JSON, not Janet: unlike .ned/init.janet/.ned/plugins/*.janet
// (arbitrary code, gated behind ProjectTrust before it's ever run), this is inert
// data a project can check in without asking anyone to trust running code just to
// open the directory -- no trust prompt, same reasoning that already lets
// GitIgnore.h read a project's .gitignore unconditionally. Read fresh on each call
// rather than cached process-wide state (unlike GitIgnore.h's own
// CachedGitIgnoreMatcher), since this is only consulted on an interactive
// link-open, not a hot path.
//
// v1 scope: four fields.
//
// includePaths -- extra directories Link.cpp's ResolveFileLink searches for a
// non-project-relative include/import target (an angle-form C/C++ #include, a
// system header, a vendored dependency fetched into build/_deps/ by CMake
// FetchContent, ...) that isn't discoverable relative to the including file or the
// project root alone. Entries are resolved relative to root if not already
// absolute. Keyed by Mode::name (e.g. "cpp-mode", "php-mode") rather than one flat
// list -- otherwise a PHP buffer's #include-style link would search C/C++ header
// directories (and vice versa) for no reason other than both being configured at
// once.
//
// lspInitializationOptions -- an arbitrary, per-language JSON blob merged into the
// "initializationOptions" field of that language's LSP "initialize" request
// (Lsp/LspManager.cpp's BuildInitializeParams) -- e.g. a project that always
// preloads a bootstrap/autoload file before any real request, and needs its
// language server told about that file so it stops reporting every function or
// require() the bootstrap defines as unresolved. Keyed by the same language string
// LspServerConfig.h/LanguageKeyForMode already use (e.g. "php", "cpp" -- no
// "-mode" suffix, unlike includePathsByMode above), since this rides the same
// per-language LSP config surface. The shape inside each entry is entirely up to
// whatever the target language server's own initializationOptions schema expects
// -- this project passes it through verbatim, unopinionated about its contents.
//
// lspWorkspaceConfiguration -- covers the two other, more common ways a real LSP
// server reads client-side config beyond initializationOptions (which some servers
// never look at, or only honor for the handful of fields they read at startup):
//   - the "pull" model (workspace/configuration): the server sends a request with
//     a dotted-path "section" per item (e.g. "phpactor", "intelephense.environment"),
//     and the client answers with whatever's configured for that section, or null
//     ("use your own defaults") -- LspManager.cpp's workspace/configuration handler
//     now resolves each requested section against this tree instead of always
//     answering null.
//   - the "push" model (workspace/didChangeConfiguration): the client proactively
//     notifies the server of its full settings tree right after the handshake --
//     LspManager.cpp sends this (once, at spawn) with {"settings": <this tree>}
//     whenever it's non-empty.
// Deliberately one flat, language-agnostic JSON object (not keyed by our own
// language string the way the other two fields are) -- a real section name is
// defined by the target server itself (e.g. "intelephense", not "php"), so there's
// no fixed schema to key by; every server just reads whatever top-level section(s)
// it recognizes and ignores the rest, the same way a real editor's settings.json
// works.
//
// importResolution -- see ImportResolutionOverride's own doc comment below.
//
// All four default a mode/language/section with no entry to "nothing configured"
// -- the same convention everywhere else in this codebase (GitIgnoreMatcher's
// missing .gitignore, Mode::fold's empty function, ...).
//

#ifndef NED_EDITOR_PROJECTSETTINGS_H
#define NED_EDITOR_PROJECTSETTINGS_H

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace ned::editor {

// import-target-tree-sitter follow-up: a project-local override of
// Editor/ImportResolutionConfig.h's bundled per-language defaults (file
// extensions/index-file basenames/package-dir search) -- keyed the same way
// includePathsByMode is, by Editor/Mode.h's LanguageKeyForMode. Lets a
// project widen a bundled language's defaults (an unusual extension a
// codebase's own build actually uses) or configure resolution for a
// language key with no bundled default at all -- a runtime-`dlopen`'d
// grammar registered via ned/register-language-grammar under its own name.
struct ImportResolutionOverride {
    std::vector<std::string> extensions;
    std::vector<std::string> indexBasenames;
    std::optional<bool>      searchPackageDirs; // nullopt = inherit the bundled default's own value
};

struct ProjectSettings {
    std::unordered_map<std::string, std::vector<std::filesystem::path>> includePathsByMode;
    std::unordered_map<std::string, nlohmann::json>                     lspInitializationOptionsByLanguage;
    nlohmann::json                                                      lspWorkspaceConfiguration = nlohmann::json::object();
    std::unordered_map<std::string, ImportResolutionOverride>           importResolutionByLanguage;
};

// Convenience accessor: settings.includePathsByMode[modeName], or an empty list if
// modeName has no entry. modeName is typically the active buffer's Mode::name.
[[nodiscard]] const std::vector<std::filesystem::path>& IncludePathsForMode(const ProjectSettings& settings,
                                                                            const std::string&      modeName);

// Convenience accessor: settings.lspInitializationOptionsByLanguage[language], or an
// empty JSON object if language has no entry. language is typically
// Editor/Mode.h's LanguageKeyForMode(mode).
[[nodiscard]] const nlohmann::json& LspInitializationOptionsForLanguage(const ProjectSettings& settings,
                                                                        const std::string&      language);

// Convenience accessor: settings.importResolutionByLanguage[language], or a
// default-constructed (empty) ImportResolutionOverride if language has no
// entry. Editor/ImportResolutionConfig.h's ResolveImportResolutionConfig is
// what actually merges this with the bundled default -- this accessor just
// mirrors the other two above, a plain lookup over ProjectSettings' own
// storage.
[[nodiscard]] const ImportResolutionOverride& ImportResolutionOverrideForLanguage(const ProjectSettings& settings,
                                                                                  const std::string&      language);

// Reads root/".ned/settings.json" if it exists and parses as valid JSON; a missing
// file, an unreadable one, or one missing/misshaping a recognized key all just
// return a default-constructed (empty) ProjectSettings, the same "absent means
// nothing configured" convention GitIgnoreMatcher uses for a missing .gitignore.
[[nodiscard]] ProjectSettings LoadProjectSettings(const std::filesystem::path& root);

} // namespace ned::editor

#endif // NED_EDITOR_PROJECTSETTINGS_H
