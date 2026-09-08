//
// LSP client follow-up (extracted for acp-mcp-tool-bridge's format_buffer
// tool). The single-buffer half of applying a server-returned edit list --
// shared by BufferView's ApplyCodeAction/ApplyRename/lsp-format-buffer call
// sites and Editor/Mcp/McpToolRegistry.h's format_buffer tool, so both go
// through the exact same tested sort/apply/undo-grouping logic rather than
// forking a second copy. The multi-file transaction wrapper
// (BufferView::ApplyProjectEdit, ProjectUndoManager-recording, file create/
// rename/delete via DocumentChangeOp) stays BufferView-only -- that half
// genuinely needs a live window/pane and isn't duplicated here.
//

#ifndef NED_EDITOR_LSP_LSPEDITAPPLY_H
#define NED_EDITOR_LSP_LSPEDITAPPLY_H

#include <vector>

#include "LspContent.h"

namespace ned::text {
class Buffer;
} // namespace ned::text

namespace ned::editor::lsp {

// Resolves each edit's LspPositions to byte offsets against buffer's CURRENT
// content, sorts descending by start byte (keeps an edit not yet applied
// valid as an earlier-in-the-buffer one shifts positions -- LSP guarantees
// edits within one WorkspaceEdit/formatting response don't overlap, so a
// plain sort suffices), and applies each via Buffer::DeleteRange +
// Buffer::InsertAt as one undo group -- a formatting response can carry
// dozens/hundreds of edits, and without grouping, undoing would take one
// press per edit instead of one for the whole operation.
void ApplyWorkspaceTextEdits(text::Buffer& buffer, const std::vector<WorkspaceTextEdit>& edits);

// completion-additional-edits follow-up. ApplyWorkspaceTextEdits above, plus
// the one thing an accepted completion needs from it: where `anchorByte`
// ended up once every edit had been applied. The accept path applies an
// item's additionalTextEdits (the "#include <vector>" a std::vector needs)
// *before* its own insertion -- they were computed against the pre-insert
// document, so applying them first is what keeps their positions honest --
// and an edit landing earlier in the buffer shifts the range that insertion
// is about to replace.
//
// Relocation rule: an edit ending at or before the anchor moves it by its
// own length delta; an edit starting after it leaves it alone. LSP
// guarantees additionalTextEdits never overlap the item's own edit, so the
// straddling case shouldn't arise -- if a server sends one anyway the anchor
// collapses to that edit's start rather than landing inside replacement text
// it knows nothing about.
//
// ApplyWorkspaceTextEdits is this with the result discarded, so both share
// one copy of the resolve/sort/apply/undo-group logic.
[[nodiscard]] std::size_t ApplyWorkspaceTextEditsAndRelocate(text::Buffer& buffer, const std::vector<WorkspaceTextEdit>& edits,
                                                             std::size_t anchorByte);

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPEDITAPPLY_H
