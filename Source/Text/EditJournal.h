//
// A record of the content edits a document has been through, so anything
// holding byte offsets against an older version of it can be carried forward
// exactly rather than approximately.
//
// This exists because `OffsetRemap.h`'s snapshot diff is the wrong tool for
// the job it had grown into. Diffing two versions recovers *one* contiguous
// changed region, which is exact for a single edit and wrong for any number
// greater than one: two edits either side of an offset are reported as one
// span covering both, so the offset looks like it sits inside the change when
// nothing touched it. The document, unlike the diff, knows what actually
// happened -- every mutation has an offset and a length in hand at the moment
// it runs. Recording those and replaying them is exact by construction, for
// any number of edits in any order, and costs no snapshots.
//
// A holder stamps itself with the generation its offsets were resolved
// against and replays everything newer on read. The receipt of a stale
// asynchronous result and the lazy catch-up of an already-applied one are
// then the same operation, differing only in which generation they start
// from.
//
// Pure and storage-free: no Buffer, no text, no knowledge of what the offsets
// mean.
//

#ifndef NED_TEXT_EDITJOURNAL_H
#define NED_TEXT_EDITJOURNAL_H

#include <cstddef>
#include <deque>
#include <optional>
#include <vector>

namespace ned::text {

// One content change, expressed as a replacement: the bytes
// [offset, offset + oldLength) became newLength bytes. An insert is
// oldLength 0, a delete is newLength 0, and a whole-content swap that no
// offset can survive is a barrier (see Barrier below).
struct EditOp {
    // The document generation this op *produced* -- so an op is replayed by a
    // holder stamped at any generation strictly below it.
    std::size_t generation = 0;
    std::size_t offset     = 0;
    std::size_t oldLength  = 0;
    std::size_t newLength  = 0;
    // A wholesale content replacement (a reload, a revert, an external merge)
    // where offset/length describe nothing useful. Every offset replayed
    // through one is invalidated: a holder's results describe a document that
    // no longer exists in any relocatable sense.
    bool barrier = false;

    [[nodiscard]] static EditOp Inserted(std::size_t generation, std::size_t offset, std::size_t length) {
        return EditOp{.generation = generation, .offset = offset, .oldLength = 0, .newLength = length};
    }
    [[nodiscard]] static EditOp Deleted(std::size_t generation, std::size_t rangeStart, std::size_t rangeEnd) {
        return EditOp{.generation = generation, .offset = rangeStart, .oldLength = rangeEnd - rangeStart, .newLength = 0};
    }
    [[nodiscard]] static EditOp Replaced(std::size_t generation, std::size_t offset, std::size_t oldLength,
                                         std::size_t newLength) {
        return EditOp{.generation = generation, .offset = offset, .oldLength = oldLength, .newLength = newLength};
    }
    [[nodiscard]] static EditOp Barrier(std::size_t generation) {
        return EditOp{.generation = generation, .barrier = true};
    }

    [[nodiscard]] bool operator==(const EditOp&) const = default;
};

// What an insertion landing exactly on an offset does to it. The distinction
// only ever matters at that one boundary, and it is the whole difference
// between an annotation that follows the text it describes and one that
// renders two columns to the left of it.
//
// Right: the offset shifts, so it keeps naming the same byte of content. What
//   an anchor that *precedes* what it annotates needs -- an inlay hint sits
//   before the byte at its offset, so text typed at that offset belongs after
//   the hint, not before it.
// Left: the offset stays, so it keeps naming the same position. What the end
//   of a range needs, so a range does not silently swallow text typed just
//   past it.
enum class Gravity { Left,
                     Right };

// What a deletion spanning an offset does to it.
//
// Clamp: collapse to the deletion point -- honest for something anchored to a
//   whole line (a code lens owns its own row; sitting at the edit point is
//   harmless).
// Invalidate: drop the offset, and with it whatever holds it. Right for
//   anything rendered inline, where clamping means drawing it in the middle
//   of whatever token now occupies that position.
enum class InsideDelete { Clamp,
                          Invalidate };

struct AnchorPolicy {
    Gravity      gravity      = Gravity::Left;
    InsideDelete insideDelete = InsideDelete::Clamp;
};

// `offset`, valid before `op`, expressed after it -- or nullopt when it did
// not survive (a barrier, or an Invalidate policy against a deletion that
// spanned it).
[[nodiscard]] std::optional<std::size_t> RelocateThrough(std::size_t offset, const EditOp& op, AnchorPolicy policy);

// The same through a whole sequence, in order, stopping at the first op the
// offset does not survive.
[[nodiscard]] std::optional<std::size_t> RelocateThroughAll(std::size_t offset, const std::vector<EditOp>& ops,
                                                            AnchorPolicy policy);

// A document's edits, newest last, bounded so a long-lived buffer cannot
// accumulate them without limit.
//
// The bound is what makes "carry forward from generation G" a question that
// can be answered no: a holder older than the oldest op still retained has no
// exact path to the present, and is told so rather than given a guess. That
// is a rare fallback by construction -- the cap is far larger than the number
// of edits any asynchronous result waits through -- and it degrades to
// exactly the wholesale-drop behaviour that used to be the normal case.
class EditJournal {
  public:
    // Ops older than this many are discarded. Sized so an ordinary editing
    // session never reaches it; a holder that does is one that has been
    // ignored for tens of thousands of keystrokes.
    static constexpr std::size_t kCapacity = 4096;

    void Record(const EditOp& op);

    // Every op strictly newer than `generation`, or nullopt when the journal
    // no longer reaches that far back (the caller must drop what it holds).
    // An empty vector means "nothing has changed since" -- a valid answer,
    // distinct from nullopt.
    [[nodiscard]] std::optional<std::vector<EditOp>> OpsSince(std::size_t generation) const;

    // `offset`, resolved against `generation`, expressed against the
    // journal's newest generation -- nullopt when it did not survive, or when
    // the journal cannot reach back to `generation` at all.
    [[nodiscard]] std::optional<std::size_t> Relocate(std::size_t offset, std::size_t generation,
                                                      AnchorPolicy policy) const;

    [[nodiscard]] bool Empty() const {
        return ops_.empty();
    }
    [[nodiscard]] std::size_t Size() const {
        return ops_.size();
    }
    // The oldest generation a holder can still be carried forward from: the
    // generation *before* the oldest retained op. A holder stamped below this
    // cannot be carried.
    [[nodiscard]] std::size_t OldestReachableGeneration() const;

  private:
    std::deque<EditOp> ops_;
    // Bumped past a discarded op's own generation as the cap trims, so
    // OldestReachableGeneration stays right once ops_ no longer carries the
    // whole history. Starts at 0: a fresh journal can carry from the
    // beginning of the document's life.
    std::size_t oldestReachable_ = 0;
};

} // namespace ned::text

#endif // NED_TEXT_EDITJOURNAL_H
