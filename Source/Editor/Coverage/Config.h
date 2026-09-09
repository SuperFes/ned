//
// Code-coverage gutter (ROADMAP.md's Maybelist entry): mutex-guarded static
// state, mirroring TestRun/TestRunConfig.h's exact shape -- the project's
// configured coverage-report file plus the currently-loaded, parsed report
// BufferView's coverage gutter reads. Unlike TestRunner (a live subprocess
// runner), there is no process to run here at all: load-coverage-report is
// a plain file read + parse, so config and loaded state live together in
// one small module rather than TestRunConfig/TestRunner's two-file split.
//

#ifndef NED_EDITOR_COVERAGE_CONFIG_H
#define NED_EDITOR_COVERAGE_CONFIG_H

#include <optional>
#include <string>

#include "Report.h"

namespace ned::editor::coverage {

// Empty clears the configured path. ned/set-coverage-file.
void                                     SetFile(std::string path);
[[nodiscard]] std::optional<std::string> File();

// Reads File(), parses it via ParseLcovInfo, and replaces the
// current report -- bumping ReportGeneration() so every open
// buffer's gutter cache (BufferView::EnsureCoverageGutterCache) invalidates
// on its next Paint(). Throws std::runtime_error if no file is configured
// or it can't be opened; a successfully-opened-but-unparseable file (wrong
// format, empty) is not an error -- ParseLcovInfo's own "unrecognized input
// yields an empty report" contract applies, same as every other parser in
// this codebase, so load-coverage-report (Commands.cpp) is what turns "the
// report parsed to zero files" into a status message.
void LoadCoverageReport();

// Discards the loaded report (bumps ReportGeneration() the same
// way) without touching the configured file path -- clear-coverage-report.
void ClearCoverageReport();

// By-value returns, TestRunConfig.h's own convention for a mutex-guarded
// static -- never a reference held past the lock.
[[nodiscard]] Report CurrentCoverageReport();
[[nodiscard]] std::size_t    ReportGeneration();

} // namespace ned::editor::coverage

#endif // NED_EDITOR_COVERAGE_CONFIG_H
