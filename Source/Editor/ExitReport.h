//
// Messages held back until the terminal belongs to the shell again.
//
// Anything that fails while the alternate screen is up can only report
// through the UI, and some failures never get read there: the buffer they
// concerned was closed before the write finished, or the user quit in the
// same breath that caused them. A large save failing is the motivating
// case -- the file is the point of the whole operation, and "it didn't
// happen" must not be the one message that gets swallowed.
//
// Collected here and printed by main() once RunInteractiveEditor has
// returned, which is the first moment plain stderr is visible again:
// everything it owns, EventLoop and its notcurses_stop included, has been
// destroyed by then. Same mutex-guarded-static shape as
// BackgroundActivity.h and PendingReExec.h, and the same reason for being
// a process-wide global rather than threaded through a return value --
// the failure happens far below the only frame that can print it.
//

#ifndef NED_EDITOR_EXITREPORT_H
#define NED_EDITOR_EXITREPORT_H

#include <string>
#include <vector>

namespace ned::editor {

// Queues one line to print on the way out. Messages are printed in the
// order reported, and nothing deduplicates them -- two failed saves are two
// things the user needs to know about.
void ReportOnExit(std::string message);

// Everything queued, clearing the queue. Called once, by main(), after the
// terminal has been restored.
[[nodiscard]] std::vector<std::string> TakeExitReports();

} // namespace ned::editor

#endif // NED_EDITOR_EXITREPORT_H
