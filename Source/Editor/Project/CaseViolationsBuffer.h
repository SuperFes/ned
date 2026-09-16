//
// case-kind follow-up: the "*case violations*" results buffer --
// TestResultsBuffer.cpp's own shape (a flat, read-only worklist under a
// summary header), reused verbatim: each violation line carries a
// synthetic text::Buffer::Diagnostic (lighting the gutter/severity glyph)
// and a "path:line:" prefix (riding BufferView::VisitResultUnderPoint's
// existing regex fallback for Enter/click jump-to-source and next-error/
// previous-error -- see Editor/NextError.h).
//

#ifndef NED_EDITOR_PROJECT_CASEVIOLATIONSBUFFER_H
#define NED_EDITOR_PROJECT_CASEVIOLATIONSBUFFER_H

#include <string>
#include <vector>

#include "CaseCheck.h"

namespace ned::text {
class Buffer;
class BufferList;
} // namespace ned::text

namespace ned::editor {

[[nodiscard]] std::string CaseViolationsBufferName();

// Finds-or-creates the read-only "*case violations*" buffer and wholesale
// rewrites it from violations (RebuildTestResultsBuffer's own refreshed-in-
// place shape -- a fresh check-format-conventions run supersedes the last
// one rather than appending to it). Point lands on the first violation
// line, or offset 0 if there are none.
text::Buffer& RebuildCaseViolationsBuffer(text::BufferList& bufferList, const std::vector<ProjectCaseViolation>& violations);

} // namespace ned::editor

#endif // NED_EDITOR_PROJECT_CASEVIOLATIONSBUFFER_H
