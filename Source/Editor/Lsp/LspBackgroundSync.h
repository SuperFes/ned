//
// LSP-deliberate-cuts follow-up: widens LspManager::SyncBuffer -- previously
// only ever called for the single active/visible buffer, once per frame from
// BufferView::Paint() -- to every open buffer, via a periodic background
// tick instead of a per-frame one. Deliberately a widening of the existing
// mechanism (SyncBuffer's own generation-gated didOpen/didChange logic is
// untouched and does all the real work), not a new sync path: a buffer
// that's also the active one just gets synced twice as often, and the
// second call is a cheap no-op (ContentGeneration() unchanged since the
// Paint()-driven sync already ran).
//
// Deliberately lives beside LspManager rather than inside it: resolving a
// background buffer's language needs its Mode (LanguageKeyForMode), and
// LspManager itself is kept Mode/tree-sitter-agnostic by design (see
// EmbeddedDocumentSync's own doc comment in LspManager.h for the same
// split). BufferView.cpp already pays this same Mode-resolution cost for
// the active buffer inline; this file is that same translation step
// widened to every buffer in the list, using the cached (not raw) mode
// resolution since it runs on every timer tick, not just on a buffer
// switch.
//

#ifndef NED_EDITOR_LSP_LSPBACKGROUNDSYNC_H
#define NED_EDITOR_LSP_LSPBACKGROUNDSYNC_H

namespace ned::text {
class BufferList;
} // namespace ned::text

namespace ned::editor::lsp {

class LspManager;

// Process-wide toggle (mutex-guarded static state, TabWidth.h/AutoRevert.h's
// exact pattern), default on. Configured from Janet via
// ned/set-lsp-sync-background-buffers.
void               SetLspBackgroundSyncEnabled(bool enabled);
[[nodiscard]] bool LspBackgroundSyncEnabled();

// Calls manager.SyncBuffer for every buffer in bufferList that has a path
// and isn't mid-async-load (the same two guards AutoRevertBuffers/
// AutoMergeBuffers already use) -- not just the pane-active one. A no-op
// entirely when the toggle above is off. Per-buffer language is resolved via
// editor::CachedModeForBuffer + LanguageKeyForMode, the same pair
// BufferView::Paint() resolves for the active buffer, so a buffer with no
// mode override/bundled mode for its extension still routes through
// SyncBuffer's own "nothing configured for this language" no-op guard
// exactly as it would if it were the active buffer.
void SyncBackgroundBuffers(text::BufferList& bufferList, LspManager& manager);

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPBACKGROUNDSYNC_H
