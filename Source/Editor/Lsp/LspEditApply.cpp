#include "LspEditApply.h"

#include <algorithm>

#include "LspPosition.h"
#include "Text/Buffer.h"
#include "Text/ITextStorage.h"

namespace ned::editor::lsp {

void ApplyWorkspaceTextEdits(text::Buffer& buffer, const std::vector<WorkspaceTextEdit>& edits) {
    (void)ApplyWorkspaceTextEditsAndRelocate(buffer, edits, 0);
}

std::size_t ApplyWorkspaceTextEditsAndRelocate(text::Buffer& buffer, const std::vector<WorkspaceTextEdit>& edits, std::size_t anchorByte) {
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
        // Tracked against the *original* offsets, which stay comparable
        // because the descending sort means no already-applied edit has
        // moved anything at or before the one being applied now. See this
        // function's own doc comment for the straddling case.
        if (edit.endByte <= anchorByte) {
            anchorByte = (anchorByte - (edit.endByte - edit.startByte)) + edit.newText.size();
        }
        else if (edit.startByte < anchorByte) {
            anchorByte = edit.startByte;
        }
    }
    buffer.EndUndoGroup();
    return anchorByte;
}

} // namespace ned::editor::lsp
