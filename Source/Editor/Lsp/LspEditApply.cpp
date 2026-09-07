#include "LspEditApply.h"

#include <algorithm>

#include "LspPosition.h"
#include "Text/Buffer.h"
#include "Text/ITextStorage.h"

namespace ned::editor::lsp {

void ApplyWorkspaceTextEdits(text::Buffer& buffer, const std::vector<WorkspaceTextEdit>& edits) {
    const text::ITextStorage& content = buffer.Content();

    struct ResolvedEdit {
        std::size_t startByte;
        std::size_t endByte;
        std::string newText;
    };
    std::vector<ResolvedEdit> resolved;
    resolved.reserve(edits.size());
    for (const WorkspaceTextEdit& edit : edits) {
        resolved.push_back(ResolvedEdit{
            .startByte = LspPositionToByte(content, edit.start),
            .endByte   = LspPositionToByte(content, edit.end),
            .newText   = edit.newText,
        });
    }
    std::sort(resolved.begin(), resolved.end(), [](const ResolvedEdit& a, const ResolvedEdit& b) { return a.startByte > b.startByte; });

    buffer.BeginUndoGroup();
    for (const ResolvedEdit& edit : resolved) {
        buffer.DeleteRange(edit.startByte, edit.endByte - edit.startByte);
        buffer.InsertAt(edit.startByte, edit.newText);
    }
    buffer.EndUndoGroup();
}

} // namespace ned::editor::lsp
