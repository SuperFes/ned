//
// The model behind a side-by-side merge view: read-only side buffers derived
// from a merged buffer, kept in step with it, and the blank-row alignment of
// every pane. UI-free -- WindowManager lays the buffers out and asks this for
// rows, scroll correspondence and "take this side".
//
// The merged buffer stays an ordinary buffer: every conflict command works on
// it unchanged, and the side buffers simply re-derive whenever its content
// generation moves. Buffers are held by name, so one closed out from under
// the session just stops being part of it.
//

#ifndef NED_EDITOR_MERGEVIEW_H
#define NED_EDITOR_MERGEVIEW_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Text/Buffer.h"
#include "Text/BufferList.h"
#include "Text/MergeAlignment.h"

namespace ned::editor {

class MergeViewSession {
  public:
    // Creates one read-only side buffer per kind, named after `merged`. A
    // kind the markers can't supply (Base without diff3 sections) is skipped.
    MergeViewSession(text::BufferList& bufferList, text::Buffer& merged, const std::vector<text::MergeSideKind>& kinds);
    // Closes whichever side buffers are still open.
    ~MergeViewSession();

    MergeViewSession(const MergeViewSession&)            = delete;
    MergeViewSession& operator=(const MergeViewSession&) = delete;

    // nullptr once the merged buffer is gone.
    [[nodiscard]] text::Buffer* Merged() const;
    // Open side buffers, in the order their kinds were given.
    [[nodiscard]] std::vector<text::Buffer*>         Sides() const;
    [[nodiscard]] std::optional<text::MergeSideKind> KindOf(const text::Buffer& buffer) const;
    [[nodiscard]] bool                               Contains(const text::Buffer& buffer) const;
    [[nodiscard]] std::size_t                        ConflictCount();

    // Re-derives the sides and alignment if the merged buffer changed, or a
    // side buffer was edited out from under the session. Rewrites side
    // buffers, so never call it while a pane is painting; the queries below
    // read the last refresh and are safe there.
    void Refresh();

    // The alignment of `buffer`'s pane, or nullptr if it isn't part of this
    // session.
    [[nodiscard]] const text::PaneAlignment* AlignmentFor(const text::Buffer& buffer) const;

    // The line of `to` sharing a screen row with `line` of `from`.
    [[nodiscard]] std::optional<std::size_t> CorrespondingLine(const text::Buffer& from, std::size_t line,
                                                               const text::Buffer& to) const;

    // Which side `line` of a side buffer shows a changed chunk of, or nullopt
    // for an unchanged line or a buffer that isn't a side.
    [[nodiscard]] std::optional<text::MergeSideKind> ChangedSideAt(const text::Buffer& buffer, std::size_t line) const;

    // Line ranges [first, second) of `buffer` inside a changed chunk.
    [[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> ChangedLines(const text::Buffer& buffer) const;

    // Replaces the merged lines of the changed chunk containing `line` of
    // side buffer `side` with that chunk's side lines, as one undo step, and
    // leaves the merged point at the replacement. False when `line` is not in
    // a changed chunk.
    bool TakeSide(const text::Buffer& side, std::size_t line);

    // Merged line of the first changed chunk starting after / before `line`
    // of the merged buffer, wrapping around.
    [[nodiscard]] std::optional<std::size_t> NextChangedLine(std::size_t line);
    [[nodiscard]] std::optional<std::size_t> PreviousChangedLine(std::size_t line);

  private:
    struct Side {
        text::MergeSideKind kind;
        std::string         name;
        text::MergeSide     model;
        std::size_t         writtenGeneration = 0;
    };

    [[nodiscard]] text::Buffer*            BufferOf(const Side& side) const;
    [[nodiscard]] const Side*              SideFor(const text::Buffer& buffer) const;
    [[nodiscard]] std::vector<std::size_t> MergedChangedStarts() const;

    text::BufferList&                bufferList_;
    std::string                      mergedName_;
    std::vector<Side>                sides_;
    std::vector<text::PaneAlignment> alignments_; // [0] merged, [1 + i] sides_[i]
    std::size_t                      conflictCount_ = 0;
    std::optional<std::size_t>       mergedGeneration_;
};

// "*merge ours: main.cpp*" and so on.
[[nodiscard]] std::string MergeSideBufferName(text::MergeSideKind kind, const std::string& mergedName);

// Replaces `buffer`'s text with `text` by rewriting only the lines that
// differ, keeping it read-only afterwards.
void RewriteReadOnlyBuffer(text::Buffer& buffer, std::string_view text);

} // namespace ned::editor

#endif // NED_EDITOR_MERGEVIEW_H
