//
// next-error/previous-error (ROADMAP.md, Navigation & Search): Emacs' unifying
// "walk the last set of located things" primitive, generalized over every
// read-only results buffer in this codebase that BufferView::
// VisitResultUnderPoint can already jump to source from -- both its
// multibuffer path (Editor/Multibuffer.h's MultibufferIndex, used by
// "*diagnostics*"/"*references: ...*"/"*agenda*"/"*clock report*"/
// "*vcs diff*") and its flat "path:line:" regex fallback ("*vcs status*",
// "*vcs blame ...*", "*search results*", "*project replace*",
// "*test results*"). A buffer whose lines carry no source location at all
// (e.g. "*vcs log ...*") is never registered here in the first place --
// walking it would just report "no results" for no reason, see each
// registration call site in Source/UI/BufferView.cpp.
//
// Global rather than per-pane, deliberately -- mirroring real Emacs' single
// next-error-last-buffer: running a search in one split then invoking
// next-error from another split's own source buffer should still walk that
// search's results, not silently do nothing because focus moved. Main-thread
// only, not mutex-guarded -- every registration call and every
// next-error/previous-error keystroke run on the main thread, the same
// assumption Multibuffer.h's own MultibufferIndexFor registry already makes.
//

#ifndef NED_EDITOR_NEXT_ERROR_H
#define NED_EDITOR_NEXT_ERROR_H

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ned::text {
class Buffer;
} // namespace ned::text

namespace ned::editor {

// One jump-able location found in a results buffer, in walk order.
// resultBufferOffset is the byte offset *within the results buffer itself*
// (not the source file) where this location's line/excerpt starts -- what
// StepError compares the results buffer's own Point() against to find
// "where we currently are," mirroring
// BufferView::JumpToNextHunk/JumpToPreviousHunk's own point-relative walk
// over a private, buffer-local cache.
struct ErrorLocation {
    std::filesystem::path sourcePath;
    std::size_t           sourceLine         = 0; // 1-indexed
    std::size_t           resultBufferOffset = 0;
};

// Records bufferName as "the last results buffer" for next-error/
// previous-error to walk, and resets the walk cursor StepResultLocation
// advances (a fresh results buffer always starts stepping from the top,
// even if bufferName is unchanged from last time -- rebuilding *is* the
// fresh action, e.g. re-running a search). Called once by every
// results-buffer builder right after it creates/refreshes its buffer -- see
// Source/UI/BufferView.cpp's call sites (BuildResultsBuffer,
// BuildVcsStatusBuffer, BuildVcsBlameBuffer, RequestVcsFullDiffBuffer,
// RequestDiagnosticsBuffer, RequestProjectFindReferences,
// BuildAgendaMultibuffer, BuildClockReportMultibuffer, and
// testrun::RebuildTestResultsBuffer's own call site) for the full list.
void SetLastResultsBuffer(const std::string& bufferName);

// nullopt means "nothing has registered a results buffer yet this session."
[[nodiscard]] std::optional<std::string> LastResultsBuffer();

// Extracts every jump-able location from buffer, in composite/document
// order (ascending resultBufferOffset) -- prefers Editor/Multibuffer.h's
// MultibufferIndex when buffer carries one (exact source ranges, any line
// inside an excerpt counts as one location), else falls back to
// VisitResultUnderPoint's own "^(.*):(\\d+):" per-line regex scan. Returns
// an empty vector, never throws, for a buffer with no locations at all.
[[nodiscard]] std::vector<ErrorLocation> CollectResultLocations(const text::Buffer& buffer);

// Advances (forward) or retreats (!forward) an internal cursor over
// locations and returns the entry landed on, or nullopt when there's
// nowhere further to go (already at the last/first entry, or locations is
// empty). Deliberately index-based rather than compared against the
// results buffer's own Point(): a fresh buffer's Point() defaults to 0,
// indistinguishable from "we're already sitting on the first entry," which
// also starts at offset 0 -- an index has no such ambiguity. The cursor is
// shared process-wide state (reset by SetLastResultsBuffer, see its own
// doc comment), not tied to locations' identity across calls, so a
// results buffer that changed shape between calls just walks whatever
// locations describes now from wherever the cursor currently sits.
[[nodiscard]] std::optional<ErrorLocation> StepResultLocation(const std::vector<ErrorLocation>& locations, bool forward);

// Test-only: drops the registered "last results buffer" name and resets the
// StepResultLocation cursor, mirroring Editor/Multibuffer.h's own
// ClearRegistryForTesting -- needed since this is process-wide state a
// later, unrelated TEST_CASE would otherwise inherit.
void ClearLastResultsBufferForTesting();

} // namespace ned::editor

#endif // NED_EDITOR_NEXT_ERROR_H
