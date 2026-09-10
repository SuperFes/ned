//
// Part of BufferView -- see UI/BufferView.h for the class itself and
// Docs/BufferViewDecomposition.md for why this file exists.
//
// rename-symbol's scope-aware tier: resolving the name at point against the
// mode's own locals.scm (Editor/LocalScopes.h) and rewriting every
// occurrence of that one binding in this buffer, with no language server
// involved. The other tier -- textDocument/prepareRename plus
// textDocument/rename, which this falls through to -- lives in Lsp.cpp
// beside the rest of the LSP broker.
//

#include "UI/BufferView/Internal.h"

#include "Editor/LocalScopes.h"

namespace ned::ui {

using namespace detail;

void BufferView::RequestRenameSymbolAtPoint() {
    pendingLocalRename_.reset();

    if (const std::optional<editor::locals::LocalBinding> binding = ResolveLocalBindingAtPoint()) {
        // A file-level binding can be referenced from other files, so
        // rewriting it here alone would be a silent partial rename -- that
        // is a language server's question, not this buffer's. Same for a
        // binding whose own scope contains a use the resolver could not
        // attribute (see LocalBinding::usedBeforeDefinition): renaming would
        // leave that use behind.
        if (!binding->scopeIsFile && !binding->usedBeforeDefinition) {
            pendingLocalRename_ = binding;
            inputMode_          = InputMode::RenameLocalNewName;
            prompt_.emplace("New name: ");
            prompt_->SetText(binding->name);
            statusMessage_ = prompt_->StatusText();
            return;
        }
    }
    RequestPrepareRenameAtPoint();
}

std::optional<editor::locals::LocalBinding> BufferView::ResolveLocalBindingAtPoint() {
    if (!mode_.localScopes) {
        return std::nullopt; // no locals query for this mode
    }
    text::Buffer& buffer = activeBuffer_.Get();
    // A huge buffer is never handed to a whole-document tree-sitter query --
    // the same rule the fold/symbol/test gutters follow, except that those
    // can window and this cannot: a binding's occurrences are only complete
    // if the whole file was seen, so a windowed answer would be a partial
    // rename rather than a partial display.
    if (buffer.Content().IsHuge()) {
        return std::nullopt;
    }
    const std::string text = buffer.Content().Substring(0, buffer.Content().ByteLength());
    return editor::locals::ResolveBindingAt(mode_.localScopes(text), text, buffer.Point());
}

void BufferView::ApplyLocalRename(const std::string& newName) {
    const std::optional<editor::locals::LocalBinding> binding = std::move(pendingLocalRename_);
    pendingLocalRename_.reset();
    if (!binding) {
        statusMessage_ = "No local binding to rename.";
        return;
    }
    if (newName.empty() || newName == binding->name) {
        statusMessage_.clear();
        return;
    }

    text::Buffer& buffer = activeBuffer_.Get();
    if (buffer.ReadOnly()) {
        statusMessage_ = "Buffer is read-only.";
        return;
    }
    // The binding was resolved against the buffer as it stood when the
    // prompt opened. Nothing should have edited it since -- the prompt owns
    // every keystroke while it is up -- but a re-resolve is cheap next to
    // rewriting ranges that have moved, and this is the one place where
    // being wrong corrupts the user's file rather than showing them
    // something stale.
    const std::optional<editor::locals::LocalBinding> fresh = ResolveLocalBindingAtPoint();
    if (!fresh || fresh->name != binding->name || fresh->definition != binding->definition ||
        fresh->occurrences != binding->occurrences) {
        statusMessage_ = "Buffer changed since the rename started -- nothing renamed.";
        return;
    }

    const std::size_t point     = buffer.Point();
    std::size_t       newPoint  = point;
    const std::size_t oldLength = binding->name.size();
    const std::size_t replaced  = binding->occurrences.size();

    // Back to front, so an earlier range's offsets are still valid when it
    // is reached; point is adjusted per range for the same reason.
    buffer.BeginUndoGroup();
    for (auto it = binding->occurrences.rbegin(); it != binding->occurrences.rend(); ++it) {
        buffer.DeleteRange(it->first, it->second - it->first); // (offset, LENGTH)
        buffer.SetPoint(it->first);
        buffer.InsertAtPoint(newName);
        if (point >= it->second) {
            newPoint = newPoint - oldLength + newName.size();
        }
        else if (point > it->first) {
            newPoint = it->first + newName.size(); // point was inside the old name
        }
    }
    buffer.SetPoint(std::min(newPoint, buffer.Content().ByteLength()));
    buffer.EndUndoGroup();

    const std::string what = binding->qualifier.empty() ? std::string("binding") : binding->qualifier;
    statusMessage_         = "Renamed local " + what + " \"" + binding->name + "\" to \"" + newName + "\" (" +
                             std::to_string(replaced) + " occurrence" + (replaced == 1 ? "" : "s") + ").";
    viewport_.ScrollToShowPoint();
}

} // namespace ned::ui
