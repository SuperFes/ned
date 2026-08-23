//
// Multibuffers (ROADMAP.md): a read-only Buffer stitching together excerpts
// from multiple files/locations into one scrollable view, plus an index
// mapping each stitched byte range back to where it came from -- what lets
// jump-to-source work from anywhere inside an excerpt's body, not just a
// single "path:line:" index line the way BuildResultsBuffer's flat
// per-match summary buffers require (see Source/UI/BufferView.cpp's
// BuildResultsBuffer/VisitSearchResult, which this deliberately doesn't
// replace -- existing "*search results*"/"*vcs status*"-style flat buffers
// are untouched).
//
// v1 scope: read-only. An excerpt's body text is supplied verbatim by the
// caller (e.g. a VCS diff hunk's own +/-/context lines) rather than always
// derived from a line-range read here -- ReadExcerptText is offered as a
// convenience for a caller that *does* want "N lines of a real file's own
// current content" (a future LSP references/diagnostics consumer), not
// something BuildMultibuffer itself calls.
//

#ifndef NED_EDITOR_MULTIBUFFER_H
#define NED_EDITOR_MULTIBUFFER_H

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace ned::text {
class Buffer;
class BufferList;
} // namespace ned::text

namespace ned::editor::multibuffer {

// A per-line style a builder (or BuildMultibuffer itself, for Header/Rule)
// can request for one of a multibuffer's composite lines -- Added/Removed
// are currently only populated by the VCS full-diff builder, but the
// mechanism is generic rather than diff-specific so any future consumer
// (e.g. a diagnostics severity wash) can reuse it. Header/Rule are set
// automatically by BuildMultibuffer for every excerpt's own header/
// separator line, not something a builder requests directly (see its own
// doc comment). None means "no special styling," the default for every
// line nobody cares about.
enum class LineTint { None,
                      Added,
                      Removed,
                      Header, // bold, no background wash -- an excerpt's own title line
                      Rule }; // the separator line between excerpts, baked as box-drawing glyphs

// One excerpt to stitch into a multibuffer, as supplied by a builder (VCS
// full-diff today; a future LSP references/diagnostics consumer would
// populate these the same way). sourceStartLine is 1-indexed; 0 means "no
// single source line applies" -- jump-to-source is then a no-op for this
// excerpt, mirroring VisitSearchResult/VisitVcsResult's own existing
// "silent no-op on a non-matching line" posture rather than inventing a new
// failure mode.
struct ExcerptSource {
    std::filesystem::path sourcePath;
    std::size_t           sourceStartLine = 0;
    std::size_t           sourceEndLine   = 0;
    std::string           headerText; // rendered as its own line, e.g. "Source/Foo.cpp:12-40"
    std::string           bodyText;   // the excerpt's literal content; a trailing '\n' is added if missing
    // One entry per line of bodyText, in order -- empty means "no tinting
    // for this excerpt" (every consumer that doesn't care leaves this
    // empty); a mismatched length against bodyText's actual line count is
    // handled by BuildMultibuffer zipping only up to whichever is shorter,
    // the same degrade-don't-crash posture the rest of this subsystem takes
    // toward malformed/unexpected input.
    std::vector<LineTint> lineTints;
};

// One stitched excerpt, located within the composite buffer's own byte
// space -- BuildMultibuffer's output, kept alongside the composite Buffer
// via SetMultibufferIndexFor. compositeEndByte is exclusive and covers this
// excerpt's header + body only, not the blank separator line after it.
struct ExcerptSpan {
    std::filesystem::path sourcePath;
    std::size_t           sourceStartLine    = 0;
    std::size_t           sourceEndLine      = 0;
    std::size_t           compositeStartByte = 0;
    std::size_t           compositeEndByte   = 0;
};

// A real multibuffer has at most a few hundred excerpts (one per diff
// hunk/reference/diagnostic), not thousands, and SpanAtOffset is called
// once per jump-to-source keypress, not per frame -- a plain sorted vector
// and linear scan is deliberately not anything cleverer.
class MultibufferIndex {
  public:
    void                                          SetSpans(std::vector<ExcerptSpan> spans);
    [[nodiscard]] const ExcerptSpan*              SpanAtOffset(std::size_t compositeByteOffset) const;
    [[nodiscard]] const std::vector<ExcerptSpan>& Spans() const;

    // Composite-line (0-indexed, matching Rope::ByteOffsetToLine) -> tint,
    // sorted by line -- BuildMultibuffer's own translation of every
    // excerpt's lineTints into the composite buffer's line numbering.
    void                   SetLineTints(std::vector<std::pair<std::size_t, LineTint>> tints);
    [[nodiscard]] LineTint TintForLine(std::size_t compositeLine) const;

  private:
    std::vector<ExcerptSpan>                      spans_;
    std::vector<std::pair<std::size_t, LineTint>> lineTints_;
};

// Registry keyed by composite-buffer identity -- mirrors BufferView's own
// buffer-keyed caches (hiddenLineRangesCacheBuffer_ etc.) rather than living
// on Buffer itself, since only multibuffer consumers need this. Not
// mutex-guarded, unlike TabWidth.h/ProjectRoot.h's process-wide settings:
// multibuffer construction and lookup both happen on the main thread only,
// the same single-threaded assumption BufferView's own per-buffer caches
// make. ClearMultibufferIndexFor is a safe no-op if buffer was never
// registered -- meant to be called from WindowManager::NotifyBufferClosing
// alongside its other per-buffer cleanup.
[[nodiscard]] MultibufferIndex* MultibufferIndexFor(const text::Buffer& buffer);
void                            SetMultibufferIndexFor(text::Buffer& buffer, MultibufferIndex index);
void                            ClearMultibufferIndexFor(const text::Buffer& buffer);

// Test-only: drops every registered index, regardless of buffer. Needed
// because the registry is keyed by raw Buffer* identity with no automatic
// per-Buffer cleanup hook (Text/ must not depend on Editor/, so Buffer's
// own destructor can't call ClearMultibufferIndexFor itself) -- a bare
// BufferList constructed directly in a test (never wired through a real
// WindowManager, the only real caller of ClearMultibufferIndexFor) leaves a
// stale entry behind when its Buffers are destroyed, and a later test's
// freshly allocated Buffer can land at the same address, spuriously
// "inheriting" it. Mirrors VcsProviderRegistry::ClearRegistry's own
// test-only reset convention for the same reason (a mutex-free/hook-free
// static registry that only real app code cleans up incrementally).
void ClearRegistryForTesting();

// Prefers a live, already-open Buffer's own content (BufferList::FindByPath)
// so unsaved edits show up in the excerpt; falls back to a raw file read
// otherwise. Returns "" (never throws) on any read failure or an
// out-of-range line request -- an excerpt whose source vanished mid-build
// degrades to an empty body rather than aborting the whole multibuffer, the
// same posture ExtractHunkPatch/ParseDiffHunks already take toward
// malformed input elsewhere in this subsystem. startLine/endLine are
// 1-indexed and inclusive.
[[nodiscard]] std::string ReadExcerptText(text::BufferList& bufferList, const std::filesystem::path& path,
                                          std::size_t startLine, std::size_t endLine);

// Builds a fresh, read-only Buffer named name from excerpts -- always
// creates a new buffer, mirroring BuildVcsBlameBuffer/BuildVcsLogBuffer
// (a caller that wants a refreshed-in-place singleton, like *vcs status*,
// does that refill itself the way RefillSingletonBuffer already does).
// Registers the resulting MultibufferIndex via SetMultibufferIndexFor --
// callers don't need to build one by hand.
text::Buffer& BuildMultibuffer(text::BufferList& bufferList, const std::string& name,
                               const std::vector<ExcerptSource>& excerpts);

} // namespace ned::editor::multibuffer

#endif // NED_EDITOR_MULTIBUFFER_H
