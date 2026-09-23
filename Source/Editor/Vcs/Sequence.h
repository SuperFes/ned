//
// The editor-side half of continuing/aborting/skipping an in-progress
// rebase, merge, cherry-pick or revert: what the mode line calls it, and
// whether a continue is safe to hand to the VCS yet. The VCS-side half (how
// to probe and drive the operation) lives in Provider's Sequence* vocabulary.
//

#ifndef NED_EDITOR_VCS_SEQUENCE_H
#define NED_EDITOR_VCS_SEQUENCE_H

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "Provider.h"

namespace ned::editor::vcs {

// Whether continuing stages unmerged files that no longer carry conflict
// markers, the way `git add` would by hand. Default off: staging is a
// deliberate "this file is resolved" statement, and a file can be free of
// markers yet still wrong. Process-wide, mutex-guarded; configured via
// ned/set-vcs-sequence-auto-stage.
void               SetSequenceAutoStage(bool enabled);
[[nodiscard]] bool SequenceAutoStageEnabled();

// "Rebasing 3/7", "Merging", "Cherry-picking"... -- an unknown kind from a
// third-party provider is shown verbatim rather than guessed at. Empty for
// a state with no kind.
[[nodiscard]] std::string SequenceLabel(const SequenceState& state);

// Whether a continue can go ahead, and what has to be staged first. Every
// path is absolute. unsaved wins over everything else: the on-disk file is
// what the VCS will read, so a resolution that only exists in a buffer
// hasn't happened yet as far as continue is concerned.
struct SequenceContinuePlan {
    std::vector<std::filesystem::path> unsaved;    // open buffer has unsaved edits
    std::vector<std::filesystem::path> conflicted; // markers still on disk
    std::vector<std::filesystem::path> toStage;    // resolved on disk, not yet staged
    bool                               autoStage = false;

    [[nodiscard]] bool Ready() const {
        return unsaved.empty() && conflicted.empty() && (toStage.empty() || autoStage);
    }
};

struct SequenceFileProbe {
    std::function<bool(const std::filesystem::path&)> hasUnsavedBuffer;
    std::function<bool(const std::filesystem::path&)> hasConflictMarkers;
};

[[nodiscard]] SequenceContinuePlan PlanSequenceContinue(const std::vector<StatusEntry>& status,
                                                        const std::filesystem::path& root, const SequenceFileProbe& probe,
                                                        bool autoStage);

// The status-line explanation for a plan that isn't Ready(); empty when it
// is. Paths are shown relative to root.
[[nodiscard]] std::string SequenceContinueBlockedMessage(const SequenceContinuePlan&  plan,
                                                         const std::filesystem::path& root);

// The default on-disk marker probe: false for a missing or unreadable file,
// which is how a delete/modify conflict resolved by deleting looks.
[[nodiscard]] bool FileHasConflictMarkers(const std::filesystem::path& path);

} // namespace ned::editor::vcs

#endif // NED_EDITOR_VCS_SEQUENCE_H
