//
// Structured test-runner integration: the parsed "*test results*" failures
// buffer -- a flat, read-only worklist of the last run's failed (then
// skipped) tests as "path:line: [FAILED] name -- message" lines under a
// summary header. A free function rather than a TestRunner method so it's
// unit-testable from a bare TestRunOutcome with no runner/process at all.
//
// Two existing mechanisms do all the UI work: each failure line carries a
// synthetic text::Buffer::Diagnostic (DiagnosticsLog.cpp's
// RebuildMessagesBuffer precedent), lighting the severity glyph/color in
// the ordinary diagnostics gutter; and the "path:line:" prefix rides
// BufferView::VisitResultUnderPoint's existing regex, so Enter/click
// jump-to-failing-test needs no new plumbing. Passed tests are deliberately
// omitted -- the per-test gutter marks are the pass display, this buffer is
// the failures worklist.
//

#ifndef NED_EDITOR_TESTRUN_TESTRESULTSBUFFER_H
#define NED_EDITOR_TESTRUN_TESTRESULTSBUFFER_H

#include <string>

#include "TestResult.h"

namespace ned::text {
class Buffer;
class BufferList;
} // namespace ned::text

namespace ned::editor::testrun {

[[nodiscard]] std::string TestResultsBufferName();

// Finds-or-creates the read-only "*test results*" buffer and wholesale
// rewrites it from outcome (RefillSingletonBuffer's refreshed-in-place
// shape, not BuildMultibuffer's fresh-buffer-per-build one -- test results
// supersede each other the way "*vcs status*" refreshes do). Point lands on
// the first failure line.
text::Buffer& RebuildTestResultsBuffer(text::BufferList& bufferList, const TestRunOutcome& outcome);

} // namespace ned::editor::testrun

#endif // NED_EDITOR_TESTRUN_TESTRESULTSBUFFER_H
