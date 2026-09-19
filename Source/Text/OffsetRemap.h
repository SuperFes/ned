//
// Mapping a byte offset computed against one version of a document onto the
// equivalent offset in another -- the single-changed-range (common prefix +
// common suffix) diff `Buffer` has always used internally to relocate its own
// tracked fields across an undo/redo restore, extracted so anything else that
// holds offsets against a stale snapshot can relocate them the same way.
//
// The consumer that forced the extraction was LSP diagnostics, and it has
// since moved off this: a server result is carried forward by replaying the
// buffer's own edits (`EditJournal.h`), which is exact for any number of them
// rather than only for one. What is left here is the case this model is
// actually right for -- a restore, where two whole versions are swapped and
// there are no edits to replay, only two documents to compare.
//
// Pure and storage-only: no Buffer, no UI, no knowledge of what the offsets
// mean. Reads through `ITextStorage::Substring` in exponentially growing
// blocks and never materializes either side whole, so a localized edit costs
// O(edit size) no matter how large the documents are.
//
// The model is deliberately one contiguous changed region, not a real diff,
// and that is exact only for a single hop between two versions. Two edits far
// apart are reported as one span covering both, so an offset between them
// relocates as if it sat inside the change -- which is why anything holding
// offsets across a *run* of ordinary edits uses `EditJournal.h` instead.
// Nothing here should grow a second such caller.
//

#ifndef NED_TEXT_OFFSETREMAP_H
#define NED_TEXT_OFFSETREMAP_H

#include <cstddef>
#include <optional>

#include "ITextStorage.h"

namespace ned::text {

// The old-text span that was effectively deleted and the new-text span that
// was effectively inserted -- together the standard way to express any text
// replacement, and the two parameter shapes Buffer's own
// MarkUnsavedRangeDeleted/MarkUnsavedRangeInserted already take.
struct ChangedSpan {
    std::size_t oldStart, oldEnd;
    std::size_t newStart, newEnd;
};

// Bounded byte-for-byte equality. A length mismatch is O(1); otherwise this
// walks in doubling blocks, so two documents differing near the start are
// cheap to tell apart even at huge size. A genuine full match is the one case
// that must read everything -- unavoidable for an exact-equality question,
// but still streamed rather than materialized.
[[nodiscard]] bool StorageContentEquals(const ITextStorage& a, const ITextStorage& b);

// The one changed region between two versions, or nullopt when they are
// byte-identical (which a restore can legitimately be: undoing back to a state
// reached by pure point/mark motion changes no content at all).
[[nodiscard]] std::optional<ChangedSpan> ChangedByteRange(const ITextStorage& oldStorage, const ITextStorage& newStorage);

// `offset`, valid against the old text, expressed against the new one.
//
// Three cases, and the middle one is the whole reason this is a named
// function rather than arithmetic at each call site:
//   - before the change  -> unmoved;
//   - after it           -> shifted by the change's own length delta;
//   - *inside* it        -> clamped to the start of the new span, because the
//     bytes it named are gone and there is no honest place to put it. A
//     caller that needs to know this happened should compare against
//     `span.newStart` rather than have this invent a position.
[[nodiscard]] std::size_t RemapOffset(std::size_t offset, const ChangedSpan& span);

// Convenience: the two calls above together, for a caller holding both
// versions. Returns `offset` unchanged when the versions are identical.
[[nodiscard]] std::size_t RemapOffsetBetween(std::size_t offset, const ITextStorage& oldStorage,
                                             const ITextStorage& newStorage);

} // namespace ned::text

#endif // NED_TEXT_OFFSETREMAP_H
