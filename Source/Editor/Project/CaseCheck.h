//
// case-kind follow-up: project-wide naming-convention checking, the
// "no user-visible surfacing yet" gap ROADMAP.md recorded when
// Editor/FormatCase.h's checker (ComputeCaseViolations) landed as a pure,
// buffer-free scan with no consumer. This is that consumer's data half --
// walking the project the same way Agenda.h walks every ".org" file, but
// over every file whose mode actually resolves to a real Mode with a
// localScopes/symbolKind query, running ComputeCaseViolations against each.
//
// Deliberately NOT the multi-threaded engine Project/Search.h is -- a case
// scan is run on demand (check-format-conventions), not on every keystroke,
// and ModeForPath's own "rebuilt fresh per lookup" cost (queries recompiled
// per call, see Mode.h) already dominates whatever a thread pool would save
// on top of it. Directory walking still reuses Project/Search.h's own two
// filters (GitIgnoreMatcher, Text/BinaryDetect.h's binary sniff) so a case
// scan skips exactly what a project search would.
//

#ifndef NED_EDITOR_PROJECT_CASECHECK_H
#define NED_EDITOR_PROJECT_CASECHECK_H

#include <cstddef>
#include <filesystem>
#include <vector>

#include "Editor/FormatCase.h"

namespace ned::text {
class BufferList;
} // namespace ned::text

namespace ned::editor {

// One CaseViolation attributed to the file it was found in, plus the
// 1-indexed source line its name starts on (computed at collection time,
// against the same text ComputeCaseViolations scanned -- CaseViolation's own
// byte offsets are only meaningful against that text, which this struct
// does not keep around).
struct ProjectCaseViolation {
    std::filesystem::path file;
    std::size_t           line = 0;
    CaseViolation          violation;
};

// Scans every non-ignored, non-binary, non-huge file under root
// (std::filesystem::recursive_directory_iterator + GitIgnoreMatcher, dot-
// directories skipped -- CollectSearchableFiles's own walk in
// Project/Search.cpp, duplicated rather than exposed since that function is
// file-local) whose ModeForPath resolves a mode with localScopes and/or
// symbolKind. A live, modified, non-huge open buffer's own text is preferred
// over the file's on-disk content (bufferList may be null, meaning "always
// read from disk" -- e.g. the headless --format CLI has no BufferList at
// all).
//
// Returns violations in file-walk order, then in ComputeCaseViolations' own
// per-file order (which is localScopes-derived entities before symbolKind-
// derived ones -- see that function). Returns an empty vector, never
// throws, for a nonexistent/unlistable root -- SearchDirectory's own
// convention.
[[nodiscard]] std::vector<ProjectCaseViolation> CollectProjectCaseViolations(const std::filesystem::path& root,
                                                                              text::BufferList* bufferList = nullptr);

} // namespace ned::editor

#endif // NED_EDITOR_PROJECT_CASECHECK_H
