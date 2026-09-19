//
// Positions that survive edits by construction, stored beside the document
// rather than inside it.
//
// The problem this exists for: anything that describes a span of text --
// point, a fold marker, a diagnostic, a snippet field, a secondary cursor --
// has so far meant a new field on `Buffer` plus a relocation rule repeated at
// every mutation site, which is how a ninth tracked field came to mean a
// ninth edit in five places. An anchor inverts that. The holder keeps a
// handle, the store moves it across every edit, and the document never learns
// what the position meant.
//
// **Beside the rope, deliberately not in it.** The rope is persistent with
// structural sharing -- `Clone()` is O(1) and `UndoTree` snapshots share
// nodes with the live buffer -- so a mutable annotation hanging off a shared
// node would bleed across undo snapshots. And an anchor usually falls where
// no piece boundary exists, so binding it to one means splitting pieces on
// the hot path. A sidecar leaves both properties exactly as they are.
//
// **What an anchor is not.** It is not a fix for stale data. An anchor placed
// at a wrong offset stays faithfully wrong forever. It answers "where did
// this position go", never "was this position right". Nor is it the tool for
// a result computed against an *older* version of the document: that is
// `EditJournal`'s replay, applied once at receipt. The two are consumers of
// the same edit feed and compose -- journal replay lands a foreign result on
// the present, an anchor keeps a position already placed in the present.
//
// Prior art: Emacs markers, CodeMirror's `RangeSet`, Zed's anchors.
//

#ifndef NED_TEXT_ANCHORSET_H
#define NED_TEXT_ANCHORSET_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "EditJournal.h"

namespace ned::text {

// A handle to one anchor. Default-constructed is never a live handle, so a
// holder can keep one before it has anything to point at.
//
// The version half is what makes a stale handle answer "gone" rather than
// silently naming whatever anchor reused its slot -- a use-after-destroy that
// would otherwise render someone else's annotation at this one's position.
struct AnchorId {
    std::uint32_t index   = 0;
    std::uint32_t version = 0; // 0 is reserved: no live anchor ever has it

    [[nodiscard]] bool Valid() const {
        return version != 0;
    }
    [[nodiscard]] bool operator==(const AnchorId&) const = default;
};

// The two ends of a span. Kept as a plain pair of handles rather than a
// distinct stored kind: a range's ends relocate independently (that is the
// whole reason each end carries its own gravity), so there is nothing for a
// range type to own beyond the two ids.
struct AnchorRange {
    AnchorId start;
    AnchorId end;

    [[nodiscard]] bool operator==(const AnchorRange&) const = default;
};

class AnchorSet {
  public:
    // The offset keeps naming the same byte of content across an insert at
    // its own position, and collapses to the deletion point when the bytes it
    // named are removed. That is the rule every position `Buffer` already
    // tracked by hand follows (`RelocateForInsert` shifts on `>=`), and the
    // right default for a cursor-like anchor. Spelled out here rather than
    // left to `AnchorPolicy`'s own member defaults, which are Left: that
    // struct is also the parameter type of the bare `RelocateThrough`
    // functions, where a range end is as ordinary a caller as a point.
    static constexpr AnchorPolicy kDefaultPolicy{.gravity = Gravity::Right, .insideDelete = InsideDelete::Clamp};

    [[nodiscard]] AnchorId Create(std::size_t offset, AnchorPolicy policy = kDefaultPolicy);

    // A span that grows when text lands at either of its own edges -- the
    // rule a snippet field being typed into or an excerpt body needs. Pass
    // explicit policies for anything else (an inactive snippet field, which
    // must *exclude* a boundary insert so two adjacent fields never both
    // claim it).
    [[nodiscard]] AnchorRange CreateRange(std::size_t start, std::size_t end);
    [[nodiscard]] AnchorRange CreateRange(std::size_t start, std::size_t end, AnchorPolicy startPolicy,
                                          AnchorPolicy endPolicy);

    // Releases the slot for reuse. Every holder must do this; an abandoned
    // anchor is a leak in the only sense available here, a slot that keeps
    // being relocated forever. Destroying an already-destroyed or never-valid
    // id is a no-op, so a holder's own teardown needs no liveness check.
    void Destroy(AnchorId id);
    void Destroy(AnchorRange range);

    // nullopt for an anchor that is gone: destroyed, invalidated by an edit
    // its policy said it could not survive, or dropped by a barrier. An
    // invalidated anchor keeps its slot until Destroy -- the holder finds out
    // on its next read, which is the only moment it could act on the news
    // anyway.
    [[nodiscard]] std::optional<std::size_t>                         Offset(AnchorId id) const;
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> Range(AnchorRange range) const;

    // The bytes [offset, offset + oldLength) became newLength bytes. One
    // replacement rather than a delete half plus an insert half, because the
    // two differ for any anchor the change spanned and the replacement is the
    // honest description of what happened.
    void ApplyEdit(std::size_t offset, std::size_t oldLength, std::size_t newLength);

    // A wholesale content swap -- a reload, a revert, an external merge --
    // after which no offset means anything. Every anchor is invalidated
    // rather than clamped: a position in a document that no longer exists has
    // no honest image in the one that replaced it.
    void ApplyBarrier();

    [[nodiscard]] std::size_t LiveCount() const;
    void                      Clear();

  private:
    struct Entry {
        std::size_t   offset = 0;
        AnchorPolicy  policy;
        std::uint32_t version  = 0;     // matches AnchorId::version while the slot is in use
        bool          occupied = false; // false once destroyed: the slot is reusable
        bool          alive    = false; // false once an edit invalidated it, slot still held
    };

    [[nodiscard]] const Entry* Find(AnchorId id) const;

    std::vector<Entry>         Entries_;
    std::vector<std::uint32_t> FreeSlots_;
    std::uint32_t              NextVersion_ = 1; // 0 stays reserved for an invalid handle
};

} // namespace ned::text

#endif // NED_TEXT_ANCHORSET_H
