//
// The disk-write half of a save, reduced to data that no longer refers to
// the Buffer it came from: a plan owns the content it writes (a storage
// snapshot, not a reference to live storage) and every filesystem decision
// already resolved, so executing one depends on nothing that another thread
// could be mutating meanwhile.
//
// Buffer::SaveToFile is the only thing that builds a plan. The
// buffer-level preconditions stay with it -- a still-loading buffer is
// refused, and the huge-file disk-space backstop runs -- before a plan is
// ever built; what lands here is the part that depends on the plan alone.
//

#ifndef NED_TEXT_SAVEPLAN_H
#define NED_TEXT_SAVEPLAN_H

#include <filesystem>
#include <memory>

#include "FilePreservation.h"
#include "ITextStorage.h"
#include "LineEnding.h"

namespace ned::text {

struct SavePlan {
    // The *resolved* target (ResolveSaveTarget's result), not the path the
    // buffer was opened under -- saving through a symlink has to update
    // what the link points at, not replace the link.
    std::filesystem::path target;

    // The content to write. Owned outright so the write can outlive any
    // particular state of the buffer it was captured from.
    std::unique_ptr<ITextStorage> snapshot;

    // Read off the target before it is replaced -- see FilePreservation.h
    // for what a rename silently discards and why each piece has to be put
    // back by hand.
    PreservedFileAttributes attributes;

    LineEnding lineEnding             = LineEnding::LF;
    bool       trimTrailingWhitespace = false;
    bool       ensureFinalNewline     = false;
};

// Writes plan.target: atomically via a sibling temp file and a rename
// where that is available, in place where a hard-linked file or an
// uncreatable sibling forces it. Throws std::runtime_error on failure,
// matching Buffer::Save's own contract -- callers catch and report.
void ExecuteSavePlan(const SavePlan& plan);

} // namespace ned::text

#endif // NED_TEXT_SAVEPLAN_H
