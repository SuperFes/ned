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

#ifndef NED_EDITOR_LSP_SERVERCONFIG_H
#define NED_EDITOR_LSP_SERVERCONFIG_H

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
// callers (Manager) treat this as "no LSP support configured for this
// language," the same way Mode::expandSelection being an empty function
// means "not configured" rather than a failure.
[[nodiscard]] std::optional<std::vector<std::string>> ServerCommand(const std::string& language);

// hover/completion follow-up. Mutex-guarded process-wide scalars, mirroring
// Editor/TabWidth.h's exact shape -- unlike the per-language map above,
// automatic-completion behavior is a single, editor-wide preference, not a
// per-language one.
void               SetLspAutoCompleteEnabled(bool enabled); // default true
[[nodiscard]] bool AutoCompleteEnabled();

// Non-positive values are clamped to 1ms rather than rejected -- same
// "don't throw over a config value, just make it sane" convention
// TabWidth::SetTabWidth already established.
void              SetLspCompletionDebounceMs(int milliseconds); // default 500
[[nodiscard]] int CompletionDebounceMs();

// diagnostics-debounce follow-up: how long Manager waits, after the most
// recently received publishDiagnostics for a buffer, before actually
// applying the merged result to it (see Manager::HandlePublishDiagnostics).
// A server re-analyzes and republishes after every didChange -- which
// SyncBuffer sends on every keystroke's content generation bump -- so
// without this, inline diagnostic squiggles/callouts churn on essentially
// every character typed rather than settling in once typing actually
// pauses. Same non-positive-clamped-to-1ms convention as the completion
// debounce above.
void              SetLspDiagnosticsDebounceMs(int milliseconds); // default 500
[[nodiscard]] int DiagnosticsDebounceMs();

// sync-debounce follow-up: how long Manager::SyncTextToServer waits, after
// a buffer's most recent edit, before actually sending the resulting
// textDocument/didChange -- a real, reproduced live freeze (gdb-confirmed:
// the main thread blocked inside ChildProcess::WriteAll's WaitWritable,
// stuck writing a full-document didChange to a server whose stdin pipe
// couldn't drain fast enough) traced directly to SyncBuffer sending one
// full-document sync per keystroke, with no debounce at all, to *two*
// servers (the primary language server and the prose checker) every single
// Paint(). Deliberately kept shorter than CompletionDebounceMs() (and so
// than signature-help/document-highlight, which reuse that same value) --
// this must land server-side *before* those feature debounces elapse and
// fire their own requests, or they'd race ahead of a server that doesn't
// have the latest content yet. Widening this past that value without
// re-checking that invariant would reintroduce staleness races on
// completion/hover/etc. Same non-positive-clamped-to-1ms convention as the
// other debounces above.
void              SetLspSyncDebounceMs(int milliseconds); // default 150
[[nodiscard]] int SyncDebounceMs();

// The shortest gap Manager::RequestViewportFeatures allows between two
// rounds of the semanticTokens/inlayHint/codeLens requests a buffer's
// (content generation, viewport) pair implies. These three are the only
// recurring background requests driven straight off Paint(), and a viewport
// that moves every frame (a held scroll, a wheel spin) made each frame its
// own round trip, of which only the last one's answer was ever looked at.
// Collapsing them is only safe because a late or missing response costs
// nothing visible any more: every one of the three carries its result
// forward on the buffer's own edit journal (Manager::CarryForward), so the
// last good set stays correct under typing instead of going stale within one
// keystroke.
//
// A window, not a debounce delay: the first pair change after a quiet window
// is sent immediately, and only a pair that changes again inside the window
// waits (see RequestViewportFeatures' own doc comment for why a discrete
// jump must not pay it). Deliberately independent of SyncDebounceMs() rather
// than derived from it -- the requests gate on the sync having landed anyway
// (each checks lastSyncedGeneration itself), so this governs how often they
// may repeat, not how long they wait for content to settle. Same
// non-positive-clamped-to-1ms convention as the other debounces above.
void              SetLspRequestIdleMs(int milliseconds); // default 150
[[nodiscard]] int RequestIdleMs();

// signature-help-auto-trigger follow-up. Same shape as
// SetLspAutoCompleteEnabled/AutoCompleteEnabled above -- a single
// editor-wide toggle, not per-language. Reuses CompletionDebounceMs()
// rather than a separate debounce value: both fire off the same
// "typing/motion just settled" heuristic.
void               SetLspSignatureHelpAutoTriggerEnabled(bool enabled); // default true
[[nodiscard]] bool SignatureHelpAutoTriggerEnabled();

// completion-trigger-characters follow-up. Whether typing a character a
// server declared as one of a completion item's commitCharacters accepts
// that item (and then inserts the character) while the popup is up. Same
// editor-wide-toggle shape as the two above.
//
// Default true -- it's what VS Code and every mainstream LSP client do, and
// it's inert against a server that declares no commit characters at all
// (this client never substitutes a default set of its own). The toggle
// exists because the servers that DO declare them are not shy about it:
// typescript-language-server sends {".", ",", ";", "("} on every single
// item, so with it on, typing ";" to end a statement while a popup happens
// to be up accepts the highlighted suggestion instead of dismissing it.
// That's the standard behavior, and also the exact thing someone may want
// off (VS Code exposes the same switch as editor.acceptSuggestionOnCommitCharacter).
void               SetLspCommitCharactersEnabled(bool enabled); // default true
[[nodiscard]] bool CommitCharactersEnabled();

// hover-tooltips follow-up. Same shape as SetLspSignatureHelpAutoTriggerEnabled
// immediately above (a single editor-wide toggle, not per-language) --
// BufferView::MaybeScheduleHover checks this before ever arming its debounce
// timer, so turning it off is a true no-op, not just a hidden popup (no
// wasted textDocument/hover requests either). Keyboard-triggered lsp-hover
// (C-c C-j) is unaffected either way -- this only gates the mouse-move path.
void               SetLspHoverOnMouseMoveEnabled(bool enabled); // default true
[[nodiscard]] bool HoverOnMouseMoveEnabled();

// lsp-format-on-save follow-up. Opt-in (default false): turning this on
// silently for every existing installation would be a surprise behavior
// change, unlike the two toggles above (which only add a passive UI cue).
// When both this and Editor/FormatOnSave.h's external FormatCommand() are
// configured, the external command wins unconditionally -- it's the more
// specific, deliberately hand-configured choice; see save-buffer's own
// shouldDeferToLspFormat helper in Commands.cpp.
void               SetLspFormatOnSaveEnabled(bool enabled); // default false
[[nodiscard]] bool FormatOnSaveEnabled();

// format-buffer-tier follow-up. Which formatter an explicit format-buffer
// hands the buffer to, per language -- the one tier decision that was
// previously unstateable: a running server claimed format-buffer
// unconditionally, so a user who had configured ned's own Space/Break/Blank/
// Wrap rules (Docs/FormattingRules.md) could not reach them for any language
// they also ran a server for, and those rules exist precisely because a
// server's own formatter has no opinion to offer about them.
//
// Keyed the same way SetLspCommand is, with the empty key as the
// process-wide default (ned/set-indent-style's own convention): a language
// with no entry falls back to that, which is true unless set otherwise --
// today's behavior, unchanged for anyone who doesn't ask. nullopt clears,
// the same "nil clears" convention every ned/set-format-* setter uses --
// clearing a language restores it to the default, clearing the default
// restores the built-in true.
void               SetLspFormatBufferEnabled(const std::string& language, std::optional<bool> enabled);
[[nodiscard]] bool FormatBufferEnabled(const std::string& language);

// on-type-formatting follow-up. Same shape/reasoning as
// SetLspFormatOnSaveEnabled above -- opt-in (default false), since this
// mutates buffer content as you type, not just a passive UI cue. Gated
// separately from format-on-save: a user may want one without the other.
void               SetLspOnTypeFormattingEnabled(bool enabled); // default false
[[nodiscard]] bool OnTypeFormattingEnabled();

// pull-diagnostics follow-up. Opt-in (default false), same reasoning as
// SetLspFormatOnSaveEnabled above: enabled, this sends an extra
// textDocument/diagnostic request on every content sync (every server,
// every keystroke-driven didChange) for the rest of that connection's
// lifetime, or until it proves unsupported -- a real recurring side effect,
// not passive UI, even though a supporting server would otherwise get no
// diagnostics at all without it (see RequestPullDiagnostics' own doc
// comment in Manager.h).
void               SetLspPullDiagnosticsEnabled(bool enabled); // default false
[[nodiscard]] bool PullDiagnosticsEnabled();

// semanticTokens follow-up. Same shape as SetLspSignatureHelpAutoTriggerEnabled
// above -- default true, since this is read-only decoration (server-informed
// highlighting layered on top of tree-sitter's own, never replacing it) with
// no editing-flow risk, the same reasoning documentHighlight's own toggle
// already established.
void               SetLspSemanticHighlightingEnabled(bool enabled); // default true
[[nodiscard]] bool SemanticHighlightingEnabled();

// lsp-workspace-folders follow-up. Whether a buffer whose resolved LSP root
// differs from an already-running same-language connection's may *join* that
// connection (one server process, several folders, via
// workspace/didChangeWorkspaceFolders) instead of spawning its own separate
// process. Default true: for a monorepo this is the whole point -- one
// clangd index shared across subpackages rather than N independent ones --
// and a server that doesn't advertise the capability is never asked to,
// so the fallback is exactly the pre-existing process-per-root behavior.
//
// Worth turning off when isolation matters more than footprint: joined
// folders share one process, so one server crash takes every joined root
// down with it (they all respawn, but together), and a server with sloppy
// cross-folder scoping can leak completions/symbols between roots that
// separate processes would keep apart.
void               SetLspWorkspaceFoldersEnabled(bool enabled); // default true
[[nodiscard]] bool WorkspaceFoldersEnabled();

// inlayHint follow-up. Same reasoning as SetLspSemanticHighlightingEnabled
// above -- default true, read-only decoration, no editing-flow risk.
void               SetLspInlayHintsEnabled(bool enabled); // default true
[[nodiscard]] bool InlayHintsEnabled();

// codeLens follow-up. Same reasoning as SetLspInlayHintsEnabled above --
// default true, read-only annotation until explicitly invoked
// (lsp-run-code-lens-at-point), no editing-flow risk.
void               SetLspCodeLensEnabled(bool enabled); // default true
[[nodiscard]] bool CodeLensEnabled();

// code-action-hints follow-up. The gutter marker saying a diagnostic on
// this line has a server-supplied quick fix. Same reasoning as
// SetLspCodeLensEnabled above -- default true, read-only annotation until
// the fix is explicitly invoked (lsp-quick-fix / lsp-code-action). Turning
// it off also stops the recurring viewport-scoped textDocument/codeAction
// request that feeds it, which is the reason to turn it off: a server that
// answers that request slowly pays for it on every viewport settle.
void               SetLspCodeActionHintsEnabled(bool enabled); // default true
[[nodiscard]] bool CodeActionHintsEnabled();

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_SERVERCONFIG_H
