//
// Part of BufferView -- see UI/BufferView.h for the class itself and
// Docs/BufferViewDecomposition.md for why this file exists.
//
// The *acp review* buffer (Editor/Acp/TurnReview.h): one agent turn's file
// changes as a multibuffer, each hunk kept or undone from its own key.
//

#include "Editor/ModeOverrides.h"
#include "Editor/Multibuffer.h"
#include "Editor/Project/Root.h"
#include "UI/BufferView/Internal.h"

namespace ned::ui {

namespace {

    // The hunk whose excerpt holds `offset` -- excerpts are built one per
    // hunk, in order.
    std::optional<std::size_t> HunkAt(const text::Buffer& review, std::size_t offset) {
        const editor::multibuffer::MultibufferIndex* index = editor::multibuffer::MultibufferIndexFor(review);
        if (index == nullptr) {
            return std::nullopt;
        }
        const editor::multibuffer::ExcerptSpan* span = index->SpanAtOffset(offset);
        if (span == nullptr) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(span - index->Spans().data());
    }

} // namespace

void BufferView::OpenAcpTurnReview(std::string title, std::vector<editor::acp::TurnFile> files) {
    editor::acp::ReviewSession session = editor::acp::MakeReviewSession(std::move(title), editor::ProjectRoot(), std::move(files));
    if (session.hunks.empty()) {
        statusMessage_ = "That turn left no file changes to review.";
        return;
    }
    if (text::Buffer* open = bufferList_.Find(std::string(editor::acp::kReviewBufferName))) {
        editor::acp::DetachReview(*open);
        CloseBufferNow(*open);
    }
    std::size_t fileCount = session.files.size();
    std::size_t hunks     = session.hunks.size();
    std::string prompt    = session.title;
    ShowAcpReview(std::move(session), 0);
    statusMessage_ = "\"" + prompt + "\": " + std::to_string(hunks) + (hunks == 1 ? " change" : " changes") + " in " +
                     std::to_string(fileCount) + (fileCount == 1 ? " file" : " files") +
                     " -- u undo, U undo file, k keep, n/p move, RET visit, q quit";
}

void BufferView::ShowAcpReview(editor::acp::ReviewSession session, std::size_t focusHunk) {
    const std::vector<editor::multibuffer::ExcerptSource> excerpts = editor::acp::ReviewExcerpts(bufferList_, session);
    text::Buffer&                                         review   = editor::multibuffer::BuildMultibuffer(bufferList_, std::string(editor::acp::kReviewBufferName), excerpts);
    // Before the switch: the pane resolves its keymap from the buffer's mode
    // the moment it shows it.
    editor::SetChosenModeForBuffer(review, std::string(editor::acp::kReviewModeName));
    editor::acp::AttachReview(review, std::move(session));
    activeBuffer_.Set(review);
    if (const editor::multibuffer::MultibufferIndex* index = editor::multibuffer::MultibufferIndexFor(review);
        index != nullptr && !index->Spans().empty()) {
        review.SetPoint(index->Spans()[std::min(focusHunk, index->Spans().size() - 1)].bodyStartByte);
    }
    viewport_.ScrollToShowPoint();
}

void BufferView::HandleAcpReviewRequest(editor::InteractiveRequest request) {
    text::Buffer&               review  = activeBuffer_.Get();
    editor::acp::ReviewSession* session = editor::acp::ReviewFor(review);
    if (session == nullptr) {
        statusMessage_ = "Not an *acp review* buffer.";
        return;
    }
    if (request == editor::InteractiveRequest::AcpReviewQuit) {
        editor::acp::DetachReview(review);
        CloseBufferNow(review);
        return;
    }

    const std::optional<std::size_t> hunk  = HunkAt(review, review.Point());
    std::size_t                      focus = hunk.value_or(0);
    std::string                      message;
    if (request != editor::InteractiveRequest::AcpReviewRefresh) {
        if (!hunk) {
            statusMessage_ = "Move onto a change first.";
            return;
        }
        if (request == editor::InteractiveRequest::AcpReviewUndoHunk) {
            message = editor::acp::UndoHunk(bufferList_, *session, *hunk);
        }
        else if (request == editor::InteractiveRequest::AcpReviewUndoFile) {
            message = editor::acp::UndoFile(bufferList_, *session, *hunk);
        }
        else {
            session->kept[*hunk] = !session->kept[*hunk];
            if (session->kept[*hunk]) {
                focus = *hunk + 1;
            }
        }
    }

    // A fresh buffer under the same name: the multibuffer is built whole.
    editor::acp::ReviewSession rebuilt = std::move(*session);
    editor::acp::DetachReview(review);
    ShowAcpReview(std::move(rebuilt), focus);
    text::Buffer& fresh = activeBuffer_.Get();
    CloseBufferNow(review);
    fresh.Rename(std::string(editor::acp::kReviewBufferName));
    statusMessage_ = message;
}

} // namespace ned::ui
