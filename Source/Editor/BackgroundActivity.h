//
// background-activity-spinner follow-up. A process-wide registry of named,
// in-flight background work ("LSP", later DAP/task-runner/VCS) so the mode
// line can show a spinner for state the user otherwise can't see. Same
// mutex-guarded-static pattern as TabWidth.h/ProjectRoot.h, holding a small
// map instead of one scalar.
//
// Counted, not boolean: a name is "active" while its begin count exceeds its
// end count (three in-flight LSP requests = one spinner until the last
// resolves). Every BeginBackgroundActivity must be paired with exactly one
// EndBackgroundActivity -- an owner that can drop work without a response
// (LspClient's pending_ map at destruction) is responsible for ending what
// it began. End without a matching begin clamps at zero rather than going
// negative, so a confused server (e.g. a duplicate $/progress "end") can't
// wedge the count below empty.
//

#ifndef NED_EDITOR_BACKGROUNDACTIVITY_H
#define NED_EDITOR_BACKGROUNDACTIVITY_H

#include <chrono>
#include <string>
#include <vector>

namespace ned::editor {

// One spinner frame's duration -- shared by ModeLine's frame selection and
// the composition root's animation re-arm delay, which must agree or the
// spinner visibly stutters (a re-arm slower than the frame length skips
// frames; faster wastes repaints).
inline constexpr std::chrono::milliseconds kBackgroundActivitySpinnerInterval{120};

struct BackgroundActivity {
    std::string name;
    std::string detail; // optional human-readable progress text ("indexing (45%)"); empty if none

    bool operator==(const BackgroundActivity&) const = default;
};

void BeginBackgroundActivity(const std::string& name);
void EndBackgroundActivity(const std::string& name);

// Attaches/replaces the detail text shown next to an active name's spinner
// (empty clears it). A no-op for a name that isn't currently active --
// detail describes live work, and the entry it would attach to is erased
// the moment the count reaches zero.
void SetBackgroundActivityDetail(const std::string& name, std::string detail);

// Every currently-active activity, sorted by name -- recomputed-fresh-per-
// Paint consumers (ModeLine) and the composition root's animation re-arm
// check both read this.
[[nodiscard]] std::vector<BackgroundActivity> ActiveBackgroundActivities();

} // namespace ned::editor

#endif // NED_EDITOR_BACKGROUNDACTIVITY_H
