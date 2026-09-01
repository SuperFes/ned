//
// A one-shot, process-wide "please execv() into this once the interactive
// editor has fully torn down" request -- how switch-project's replace-in-
// place fallback (ProjectSwitch.h) actually hands off from deep inside a
// running command to main()'s own tail, after RunInteractiveEditor (see
// main.cpp) has returned and every local there (windowManager/bufferList/
// eventLoop included) has already been destroyed. execv() can't safely run
// from inside RunInteractiveEditor itself -- that destruction, which is
// what actually calls ~EventLoop() (notcurses_stop) and tears down every
// child process, only happens once that function returns.
//
// executablePath is resolved and verified (exists, executable) by the
// caller *before* ever setting this and requesting quit -- see
// ProjectSwitch.h's own comment on why teardown must never start for a
// switch that can't complete.
//
// Mutex-guarded static state, mirroring ProjectRoot.h's exact shape.
//

#ifndef NED_EDITOR_PENDINGREEXEC_H
#define NED_EDITOR_PENDINGREEXEC_H

#include <filesystem>
#include <optional>

namespace ned::editor {

struct PendingReExecRequest {
    std::filesystem::path executablePath; // pre-verified: exists, executable
    std::filesystem::path root;           // new project root, passed as the sole argv

    bool operator==(const PendingReExecRequest&) const = default;
};

void SetPendingReExec(PendingReExecRequest request);

// Returns and clears the pending request, if any -- a second call before a
// new SetPendingReExec returns nullopt.
[[nodiscard]] std::optional<PendingReExecRequest> TakePendingReExec();

// Tests only.
void ResetPendingReExecForTesting();

} // namespace ned::editor

#endif // NED_EDITOR_PENDINGREEXEC_H
