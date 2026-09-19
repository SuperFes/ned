#include "AnchorSet.h"

namespace ned::text {

AnchorId AnchorSet::Create(std::size_t offset, AnchorPolicy policy) {
    const std::uint32_t version = NextVersion_++;
    if (NextVersion_ == 0) {
        NextVersion_ = 1; // wrapped: 0 stays reserved for an invalid handle
    }

    if (!FreeSlots_.empty()) {
        const std::uint32_t index = FreeSlots_.back();
        FreeSlots_.pop_back();
        Entries_[index] = Entry{.offset = offset, .policy = policy, .version = version, .occupied = true, .alive = true};
        return AnchorId{.index = index, .version = version};
    }

    Entries_.push_back(Entry{.offset = offset, .policy = policy, .version = version, .occupied = true, .alive = true});
    return AnchorId{.index = static_cast<std::uint32_t>(Entries_.size() - 1), .version = version};
}

AnchorRange AnchorSet::CreateRange(std::size_t start, std::size_t end) {
    // Left at the start, Right at the end -- the pair that makes a span grow
    // around text typed at either edge. See CreateRange's doc comment.
    return CreateRange(start, end, AnchorPolicy{.gravity = Gravity::Left, .insideDelete = InsideDelete::Clamp},
                       AnchorPolicy{.gravity = Gravity::Right, .insideDelete = InsideDelete::Clamp});
}

AnchorRange AnchorSet::CreateRange(std::size_t start, std::size_t end, AnchorPolicy startPolicy,
                                   AnchorPolicy endPolicy) {
    return AnchorRange{.start = Create(start, startPolicy), .end = Create(end, endPolicy)};
}

void AnchorSet::Destroy(AnchorId id) {
    if (Find(id) == nullptr) {
        return;
    }
    Entries_[id.index].occupied = false;
    Entries_[id.index].alive    = false;
    Entries_[id.index].version  = 0;
    FreeSlots_.push_back(id.index);
}

void AnchorSet::Destroy(AnchorRange range) {
    Destroy(range.start);
    Destroy(range.end);
}

const AnchorSet::Entry* AnchorSet::Find(AnchorId id) const {
    if (!id.Valid() || id.index >= Entries_.size()) {
        return nullptr;
    }
    const Entry& entry = Entries_[id.index];
    if (!entry.occupied || entry.version != id.version) {
        return nullptr; // destroyed, or the slot has since been reused
    }
    return &entry;
}

std::optional<std::size_t> AnchorSet::Offset(AnchorId id) const {
    const Entry* entry = Find(id);
    if (entry == nullptr || !entry->alive) {
        return std::nullopt;
    }
    return entry->offset;
}

std::optional<std::pair<std::size_t, std::size_t>> AnchorSet::Range(AnchorRange range) const {
    const std::optional<std::size_t> start = Offset(range.start);
    const std::optional<std::size_t> end   = Offset(range.end);
    if (!start || !end) {
        return std::nullopt; // half a range is not a range
    }
    return std::pair{*start, *end};
}

void AnchorSet::ApplyEdit(std::size_t offset, std::size_t oldLength, std::size_t newLength) {
    if (oldLength == 0 && newLength == 0) {
        return; // an identity edit, published only to advance a generation
    }
    const EditOp op = EditOp::Replaced(/*generation=*/0, offset, oldLength, newLength);
    for (Entry& entry : Entries_) {
        if (!entry.occupied || !entry.alive) {
            continue;
        }
        const std::optional<std::size_t> relocated = RelocateThrough(entry.offset, op, entry.policy);
        if (!relocated) {
            entry.alive = false;
            continue;
        }
        entry.offset = *relocated;
    }
}

void AnchorSet::ApplyBarrier() {
    for (Entry& entry : Entries_) {
        entry.alive = false;
    }
}

std::size_t AnchorSet::LiveCount() const {
    std::size_t count = 0;
    for (const Entry& entry : Entries_) {
        if (entry.occupied && entry.alive) {
            ++count;
        }
    }
    return count;
}

void AnchorSet::Clear() {
    Entries_.clear();
    FreeSlots_.clear();
}

} // namespace ned::text
