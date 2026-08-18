//
// LSP client follow-up. A user-configurable table pointing a language name
// (the same name ModeForPath/ModeByName resolve to, e.g. "c", "python") at
// the command+arguments used to launch that language's LSP server.
//
// Mutex-guarded static state, mirroring Editor/ModeOverrides.h's exact
// shape (a map, not TabWidth.h/FormatOnSave.h's single-scalar shape -- an
// LSP server command is inherently per-language, not one process-wide
// choice).
//
// Nothing is bundled and nothing is ever installed/updated by ned itself --
// same "you install the tool, we shell out to it" trust boundary
// FormatOnSave.h/ned/set-format-command already established, not a new one.
// Deliberately not auto-detected/auto-fetched either, for the same
// non-portable-system-layout reason dynamic-grammar-loading's own
// ned-setup-style tooling already rejected that for tree-sitter grammars
// (see ROADMAP.md) -- if a future companion tool ever wants to *detect*
// what's already on $PATH and generate the matching ned/set-lsp-command
// calls, that's an additive, separate concern layered on top of this, not a
// reason to change this file's own scope.
//

#ifndef NED_EDITOR_LSP_LSPSERVERCONFIG_H
#define NED_EDITOR_LSP_LSPSERVERCONFIG_H

#include <optional>
#include <string>
#include <vector>

namespace ned::editor::lsp {

// Registers argv (argv[0] the executable, remaining elements its arguments,
// e.g. {"clangd"} or {"pyright-langserver", "--stdio"}) as the command used
// to launch language's LSP server. Re-registering overwrites, mirroring
// CommandRegistry::Register's own "expected use, not an error" convention.
// An empty argv clears any existing registration for language.
void SetLspServerCommand(const std::string& language, std::vector<std::string> argv);

// std::nullopt if nothing is registered for language -- not an error;
// callers (LspManager) treat this as "no LSP support configured for this
// language," the same way Mode::expandSelection being an empty function
// means "not configured" rather than a failure.
[[nodiscard]] std::optional<std::vector<std::string>> LspServerCommand(const std::string& language);

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPSERVERCONFIG_H
