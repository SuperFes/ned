//
// Mapping a byte offset computed against one version of a document onto the
// equivalent offset in another -- the single-changed-range (common prefix +
// common suffix) diff `Buffer` has always used internally to relocate its own
// tracked fields across an undo/redo restore, extracted so anything else that
// holds offsets against a stale snapshot can relocate them the same way.
//
// The consumer that forced the extraction is LSP diagnostics. A server
// computes `{line, character}` positions against the document version it was
// last told about, and answers on its own schedule -- so by the time a
// publish arrives the buffer has usually moved on. Converting those positions
// against the *current* text puts every diagnostic on the wrong bytes until
// the next publish catches up, which is visible as an underline that sits
// beside the token it flags rather than on it. Converting against the text of
// the version the server named and then remapping through this is what makes
// the offsets right on arrival.
//
// Pure and storage-only: no Buffer, no UI, no knowledge of what the offsets
// mean. Reads through `ITextStorage::Substring` in exponentially growing
// blocks and never materializes either side whole, so a localized edit costs
// O(edit size) no matter how large the documents are.
//
// The model is deliberately one contiguous changed region, not a real diff.
// That is exact for what an editing session actually produces between two
// nearby versions -- a burst of typing, a paste, a deletion -- and degrades
// predictably rather than wrongly when it is not: two edits far apart are
// reported as one span covering both, so an offset between them relocates as
// if it sat inside the change. A caller that cannot tolerate that should
// compare versions more often rather than ask this for more than it offers.
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
