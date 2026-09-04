//
// linked-editing-range follow-up. A live session mirroring edits across
// several already-existing buffer ranges that LSP's textDocument/
// linkedEditingRange reported as textually linked (typically a markup
// element's matching opening and closing tag name) -- structurally the same
// "one mirror group" shape Snippet.h's own SnippetSession::SyncMirrors
// already established over Buffer::SnippetRange, but built directly here
// rather than routed through SnippetSession: there's no insertion step (the
// ranges already exist in the document), no tabstop navigation, no
// placeholder/pristine handling -- just one group of ranges kept textually
// identical while the active one is edited.
//
// Entered explicitly (BufferView::RequestLinkedEditingRangeAtPoint, bound to
// lsp-linked-editing-range) rather than automatically on every cursor move --
// unlike DocumentHighlight's own passive, redraw-only highlighting, this is
// a real live-editing session (Buffer mutation on every keystroke), and this
// codebase's existing precedent for that shape (SnippetSession itself) is
// always entered by an explicit action (a trigger word, an LSP completion
// accept), never a background poll.
//
// Reuses Buffer::SnippetRange as its storage -- the same generic,
// relocated-across-every-edit primitive Snippet.h already established
// (Buffer has no idea these ranges mean "linked tag names" rather than
// "snippet tabstops," the same split FoldMarker's own multiple owners
// establish for fold state). Every range shares tabstopIndex 0, so a
// LinkedEditingSession and a real SnippetSession must never be concurrently
// active -- BufferView enforces that mutual exclusion at the call site
// (refusing to start one while the other is live), the same one-session-at-
// a-time precedent Dap/Acp already establish for their own reasons.
//
// wordPattern (the response's own rarely-sent override of the client's
// word-boundary regex for "has this edit moved outside the linked region")
// is deliberately not honored: PointStillInside below ends the session
// purely on whether point has left every tracked range, which needs no
// regex at all and matches this class's only real use so far (a tag name,
// where "left the range" already means "typed past the name" for any
// language's own regex).
//

#ifndef NED_EDITOR_LINKEDEDITINGSESSION_H
#define NED_EDITOR_LINKEDEDITINGSESSION_H

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Text/Buffer.h"

namespace ned::editor {

class LinkedEditingSession {
  public:
    // ranges: byte [start,end) pairs already resolved against buffer's
    // current content (LspPositionToByte -- BufferView's own job, this class
    // stays LSP-agnostic the same way SnippetSession stays snippet-syntax-
    // agnostic once past ParseSnippet). Returns nullopt when there are fewer
    // than 2 ranges (nothing to mirror) or none of them contains point
    // (nothing to make active) -- the caller's cue that there's nothing
    // worth starting.
    [[nodiscard]] static std::optional<LinkedEditingSession>
    Start(text::Buffer& buffer, std::string bufferName, const std::vector<std::pair<std::size_t, std::size_t>>& ranges);

    // Same contract as SnippetSession::SyncMirrors: propagates the active
    // range's current content to every other range in the group, a no-op if
    // nothing changed since the last sync. Caller owns undo grouping (one
    // group per keystroke wraps the dispatched edit and this call together).
    void SyncMirrors(text::Buffer& buffer);

    // False once undo/redo has cleared the buffer's snippet ranges -- same
    // end-of-session cue SnippetSession::RangesValid gives.
    [[nodiscard]] bool RangesValid(const text::Buffer& buffer) const;
    // False once point has moved outside every linked range -- this
    // session's own additional end condition: unlike a real snippet field
    // (which stays live until Tab/Escape regardless of where point wanders),
    // linked editing only makes sense while point is still inside one of the
    // ranges it's mirroring.
    [[nodiscard]] bool PointStillInside(const text::Buffer& buffer) const;
    // Clears the buffer's snippet ranges; the mirrored text stays exactly as
    // it is.
    void Finish(text::Buffer& buffer);

    [[nodiscard]] const std::string& BufferName() const;
    [[nodiscard]] std::string        StatusText() const;

  private:
    LinkedEditingSession() = default;
    [[nodiscard]] const text::Buffer::SnippetRange* FindRange(const text::Buffer& buffer, std::size_t id) const;

    std::string bufferName_;
    std::size_t activeRangeId_ = 0;
    std::string lastSyncedText_;
};

} // namespace ned::editor

#endif // NED_EDITOR_LINKEDEDITINGSESSION_H
