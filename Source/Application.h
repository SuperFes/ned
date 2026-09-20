//
// Created by Fester on 5/29/25.
//
// Process-wide application state: things that belong to the run as a whole
// rather than to any buffer, window or subsystem. The title was the first;
// the exit lifecycle below is the rest of the same idea -- what the process
// has to remember in order to end correctly, collected in one place instead
// of scattered across the frames that happen to discover it.
//

#ifndef APPLICATION_H
#define APPLICATION_H

#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace Ned {

// ned's exit codes. A documented interface (see the UserGuide's "Launching
// ned"), not an implementation detail: `EDITOR=ned` means a version control
// system reads this number to decide whether to go through with a commit,
// so a save that failed has to be distinguishable from a clean quit.
// Anything scripting ned may rely on these staying put.
//
// Deliberately few. A code nothing ever returns is worse than no code at
// all -- it reads as a promise the program never keeps -- so this grows
// only when something real needs to tell the difference. Startup failure,
// for one, has no code here because it has no clean exit path to attach one
// to: a terminal ned cannot initialise aborts rather than returning.
enum class ExitStatus : int {
    // The editor ran and exited normally. Every file the user asked to save
    // is on disk.
    Success = 0,

    // Something the user asked for could not be done. The general case, and
    // what every failing subcommand (--format, --compile-language,
    // --import-language, the LSP broker controls) returns.
    Failure = 1,

    // The command line itself was wrong -- an unknown flag, a missing or
    // repeated argument. Distinct from Failure because the program never
    // got as far as attempting anything.
    UsageError = 2,

    // The editor exited with at least one file the user asked to save still
    // unwritten, because the write failed. NOT "quit with unsaved changes":
    // deciding not to save is the user's business and exits Success. This
    // is a save that was attempted and did not happen, which is the case a
    // commit-message editor must not let pass silently.
    SaveFailed = 3,
};

[[nodiscard]] constexpr int ToExitCode(ExitStatus status) {
    return static_cast<int>(status);
}

class Application {
  public:
    static std::string GetTitle();

    static void SetTitle(const std::string& title);
    static void SetTitle(const char* title);

    // --- Exit lifecycle ------------------------------------------------
    //
    // Both halves exist because the frame that discovers a problem is never
    // the frame that can act on it: a background save fails on its own
    // thread, long after the call that started it returned, and possibly
    // after the buffer it concerned was closed.

    // Queues one line to print on the way out, for a failure the UI never
    // got a chance to show -- the buffer was closed before the write
    // finished, or the user quit in the same breath. Printed by main() once
    // the terminal has been restored, which is the first moment plain
    // stderr is visible again. Messages print in the order reported and
    // nothing deduplicates them: two failed saves are two things to know
    // about. An empty message is ignored.
    static void ReportOnExit(std::string message);

    // Everything queued, clearing the queue.
    [[nodiscard]] static std::vector<std::string> TakeExitReports();

    // The one way a failed save is recorded. Latches `path` so the run
    // exits SaveFailed, and queues the line that explains why -- always
    // together, because an exit code with nothing explaining it is barely
    // better than no code, and an explanation a script can't detect is
    // barely better than silence. `reason` may carry its own "ned: "
    // prefix (exception messages in this codebase do); it is not repeated.
    static void NoteSaveFailed(const std::filesystem::path& path, const std::string& bufferName, const std::string& reason);

    // Clears the latch for a path, so failing a save and then retrying it
    // successfully exits Success: what matters at exit is whether the file
    // is on disk now, not whether the road there was bumpy. The queued
    // explanation stays -- it describes something that did happen.
    static void NoteSaveSucceeded(const std::filesystem::path& path);

    // The status an otherwise-clean run should exit with: SaveFailed while
    // any path is still in that state, Success otherwise. The single place
    // that decision is made.
    [[nodiscard]] static ExitStatus CurrentExitStatus();

    // Test-only, matching ResetBackgroundActivitiesForTesting's precedent:
    // this is process-wide state one test case must not leak into the next.
    static void ResetExitStateForTesting();

  protected:
    static std::mutex& Mutex();
    static std::string Title(const std::string& title = "");
};

} // namespace Ned

#endif // APPLICATION_H
