//
// Part of BufferView -- see UI/BufferView.h for the class itself and
// Docs/BufferViewDecomposition.md for why this file exists.
//
// The ACP compose buffer (Editor/Acp/Compose.h): opening it for the panel
// and handing its text back.
//

#include "Editor/ModeOverrides.h"
#include "UI/BufferView/Internal.h"

namespace ned::ui {

void BufferView::BeginAcpCompose(std::string seed, editor::acp::ComposeCallbacks callbacks) {
    const std::string name   = std::string(editor::acp::kComposeBufferName);
    text::Buffer*     buffer = bufferList_.Find(name);
    if (buffer == nullptr) {
        buffer = &bufferList_.CreateBuffer(name);
        buffer->InsertAtPoint(seed);
        // Before the switch: the pane resolves its keymap from the buffer's
        // mode the moment it shows it.
        editor::SetChosenModeForBuffer(*buffer, std::string(editor::acp::kComposeModeName));
    }
    editor::acp::AttachCompose(*buffer, std::move(callbacks));
    activeBuffer_.Set(*buffer);
    statusMessage_ = "Write the prompt -- C-c C-c sends it, C-c C-k cancels";
}

void BufferView::FinishAcpCompose(bool send) {
    text::Buffer&                                buffer    = activeBuffer_.Get();
    std::optional<editor::acp::ComposeCallbacks> callbacks = editor::acp::DetachCompose(buffer);
    if (!callbacks) {
        statusMessage_ = "Not an ACP compose buffer.";
        return;
    }
    std::string text = buffer.Text();
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }
    CloseBufferNow(buffer);
    if (send && callbacks->onSend) {
        callbacks->onSend(std::move(text));
    }
    else if (!send && callbacks->onCancel) {
        callbacks->onCancel();
    }
}

} // namespace ned::ui
