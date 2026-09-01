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

// hover/completion follow-up. Mutex-guarded process-wide scalars, mirroring
// Editor/TabWidth.h's exact shape -- unlike the per-language map above,
// automatic-completion behavior is a single, editor-wide preference, not a
// per-language one.
void              SetLspAutoCompleteEnabled(bool enabled); // default true
[[nodiscard]] bool LspAutoCompleteEnabled();

// Non-positive values are clamped to 1ms rather than rejected -- same
// "don't throw over a config value, just make it sane" convention
// TabWidth::SetTabWidth already established.
void             SetLspCompletionDebounceMs(int milliseconds); // default 500
[[nodiscard]] int LspCompletionDebounceMs();

// diagnostics-debounce follow-up: how long LspManager waits, after the most
// recently received publishDiagnostics for a buffer, before actually
// applying the merged result to it (see LspManager::HandlePublishDiagnostics).
// A server re-analyzes and republishes after every didChange -- which
// SyncBuffer sends on every keystroke's content generation bump -- so
// without this, inline diagnostic squiggles/callouts churn on essentially
// every character typed rather than settling in once typing actually
// pauses. Same non-positive-clamped-to-1ms convention as the completion
// debounce above.
void             SetLspDiagnosticsDebounceMs(int milliseconds); // default 500
[[nodiscard]] int LspDiagnosticsDebounceMs();

// signature-help-auto-trigger follow-up. Same shape as
// SetLspAutoCompleteEnabled/LspAutoCompleteEnabled above -- a single
// editor-wide toggle, not per-language. Reuses LspCompletionDebounceMs()
// rather than a separate debounce value: both fire off the same
// "typing/motion just settled" heuristic.
void              SetLspSignatureHelpAutoTriggerEnabled(bool enabled); // default true
[[nodiscard]] bool LspSignatureHelpAutoTriggerEnabled();

// lsp-format-on-save follow-up. Opt-in (default false): turning this on
// silently for every existing installation would be a surprise behavior
// change, unlike the two toggles above (which only add a passive UI cue).
// When both this and Editor/FormatOnSave.h's external FormatCommand() are
// configured, the external command wins unconditionally -- it's the more
// specific, deliberately hand-configured choice; see save-buffer's own
// shouldDeferToLspFormat helper in Commands.cpp.
void              SetLspFormatOnSaveEnabled(bool enabled); // default false
[[nodiscard]] bool LspFormatOnSaveEnabled();

// on-type-formatting follow-up. Same shape/reasoning as
// SetLspFormatOnSaveEnabled above -- opt-in (default false), since this
// mutates buffer content as you type, not just a passive UI cue. Gated
// separately from format-on-save: a user may want one without the other.
void              SetLspOnTypeFormattingEnabled(bool enabled); // default false
[[nodiscard]] bool LspOnTypeFormattingEnabled();

// pull-diagnostics follow-up. Opt-in (default false), same reasoning as
// SetLspFormatOnSaveEnabled above: enabled, this sends an extra
// textDocument/diagnostic request on every content sync (every server,
// every keystroke-driven didChange) for the rest of that connection's
// lifetime, or until it proves unsupported -- a real recurring side effect,
// not passive UI, even though a supporting server would otherwise get no
// diagnostics at all without it (see RequestPullDiagnostics' own doc
// comment in LspManager.h).
void              SetLspPullDiagnosticsEnabled(bool enabled); // default false
[[nodiscard]] bool LspPullDiagnosticsEnabled();

// semanticTokens follow-up. Same shape as SetLspSignatureHelpAutoTriggerEnabled
// above -- default true, since this is read-only decoration (server-informed
// highlighting layered on top of tree-sitter's own, never replacing it) with
// no editing-flow risk, the same reasoning documentHighlight's own toggle
// already established.
void              SetLspSemanticHighlightingEnabled(bool enabled); // default true
[[nodiscard]] bool LspSemanticHighlightingEnabled();

// inlayHint follow-up. Same reasoning as SetLspSemanticHighlightingEnabled
// above -- default true, read-only decoration, no editing-flow risk.
void              SetLspInlayHintsEnabled(bool enabled); // default true
[[nodiscard]] bool LspInlayHintsEnabled();

// codeLens follow-up. Same reasoning as SetLspInlayHintsEnabled above --
// default true, read-only annotation until explicitly invoked
// (lsp-run-code-lens-at-point), no editing-flow risk.
void              SetLspCodeLensEnabled(bool enabled); // default true
[[nodiscard]] bool LspCodeLensEnabled();

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPSERVERCONFIG_H
