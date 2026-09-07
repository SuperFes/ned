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

} // namespace ned::editor::lsp

#endif // NED_EDITOR_LSP_LSPEDITAPPLY_H
