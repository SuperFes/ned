#include "EditJournal.h"

#include <cassert>

namespace ned::text {

std::optional<std::size_t> RelocateThrough(std::size_t offset, const EditOp& op, AnchorPolicy policy) {
    if (op.barrier) {
        return std::nullopt;
    }
    const std::size_t oldEnd = op.offset + op.oldLength;
    if (offset < op.offset) {
        return offset; // strictly before the change -- unaffected content
    }
    if (offset == op.offset) {
        // The one boundary gravity exists for. A replacement is read here as
        // a deletion followed by an insertion at the same point, so an offset
        // sitting on it lands either before the new text (Left) or after it
        // (Right) -- never inside, and never invalidated: the position is
        // still a real position in the new document whatever happened to the
        // bytes that used to follow it.
        return policy.gravity == Gravity::Right ? op.offset + op.newLength : op.offset;
    }
    if (offset >= oldEnd) {
        return offset + op.newLength - op.oldLength; // after the change -- shifted by its own length delta
    }
    if (policy.insideDelete == InsideDelete::Invalidate) {
        return std::nullopt; // the bytes this named are gone; see InsideDelete's own doc comment
    }
    return op.offset;
}

std::optional<std::size_t> RelocateThroughAll(std::size_t offset, const std::vector<EditOp>& ops, AnchorPolicy policy) {
    std::size_t current = offset;
    for (const EditOp& op : ops) {
        const std::optional<std::size_t> next = RelocateThrough(current, op, policy);
        if (!next) {
            return std::nullopt;
        }
        current = *next;
    }
    return current;
}

void EditJournal::Record(const EditOp& op) {
    // Generations are what OpsSince slices on, so they must arrive
    // monotonically. Buffer stamps each op with the generation its own
    // mutation just produced, which is monotonic by construction.
    assert(ops_.empty() || op.generation > ops_.back().generation);
    ops_.push_back(op);
    while (ops_.size() > kCapacity) {
        // A holder stamped at the discarded op's own generation no longer
        // needs it, so that generation stays reachable -- anything older does
        // not.
        oldestReachable_ = ops_.front().generation;
        ops_.pop_front();
    }
}

std::optional<std::vector<EditOp>> EditJournal::OpsSince(std::size_t generation) const {
    if (generation < oldestReachable_) {
        return std::nullopt;
    }
    std::vector<EditOp> since;
    for (const EditOp& op : ops_) {
        if (op.generation > generation) {
            since.push_back(op);
        }
    }
    return since;
}

std::optional<std::size_t> EditJournal::Relocate(std::size_t offset, std::size_t generation, AnchorPolicy policy) const {
    const std::optional<std::vector<EditOp>> ops = OpsSince(generation);
    if (!ops) {
        return std::nullopt;
    }
    return RelocateThroughAll(offset, *ops, policy);
}

std::size_t EditJournal::OldestReachableGeneration() const {
    return oldestReachable_;
}

} // namespace ned::text
