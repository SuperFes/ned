//
// Multibuffers (ROADMAP.md): a Buffer stitching together excerpts from
// multiple files/locations into one scrollable view, plus an index mapping
// each stitched byte range back to where it came from -- what lets
// jump-to-source work from anywhere inside an excerpt's body, not just a
// single "path:line:" index line the way BuildResultsBuffer's flat
// per-match summary buffers require (see Source/UI/BufferView.cpp's
// BuildResultsBuffer/VisitSearchResult, which this deliberately doesn't
// replace -- existing "*search results*"/"*vcs status*"-style flat buffers
// are untouched). Read-only unless at least one excerpt opts into editing
// -- see the editable-multibuffer paragraph below.
//
// An excerpt's body text is supplied verbatim by the caller (e.g. a VCS diff
// hunk's own +/-/context lines) rather than always derived from a line-range
// read here -- ReadExcerptText is offered as a convenience for a caller that
// *does* want "N lines of a real file's own current content" (the
// diagnostics/find-references consumers), not something BuildMultibuffer
// itself calls.
//
// Editable-multibuffer follow-up: an excerpt marked ExcerptSource::editable
// becomes a genuinely typable region of the composite buffer, writable back
// to its real source buffer via CommitExcerptChanges -- the wgrep-style
// (Emacs' editable-grep-results package) "edit here, commit there" flow.
// Every excerpt's own header line and the rule/separator lines between
// excerpts stay protected chrome regardless -- only the body text itself is
// ever editable. lsp-diagnostics-buffer/project-find-references mark their
// excerpts editable; vcs-full-diff-buffer (mixed +/-/context lines, real
// patch-apply semantics) and the agenda/clock-report multibuffers
// (synthetic summary text with no 1:1 source-byte mapping) deliberately
// don't, and stay exactly as read-only as before.
//

#ifndef NED_EDITOR_MULTIBUFFER_H
#define NED_EDITOR_MULTIBUFFER_H

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ned::text {
class Buffer;
class BufferList;
} // namespace ned::text

namespace ned::editor {
class ProjectUndoManager;
} // namespace ned::editor

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
    // Editable-multibuffer follow-up: see this header's own doc comment.
    // Default false -- every existing caller stays exactly as read-only as
    // before with no call-site change. Only takes effect when
    // sourceStartLine is nonzero (no source line means nowhere to write a
    // commit back to); BuildMultibuffer silently treats editable as false
    // otherwise rather than erroring, the same degrade-don't-crash posture
    // malformed input gets elsewhere in this subsystem.
    bool editable = false;
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
    // Auto-collapse-on-build follow-up: where this excerpt's own body text
    // starts within the composite buffer -- equal to compositeStartByte
    // when the excerpt carried no headerText (ExcerptSource::headerText
    // empty), strictly greater otherwise (past the header's own line and
    // its newline). FoldableExcerptBlocks below uses the gap between the
    // two to tell whether this excerpt has a visible header line to fold
    // *under* -- one with none is never offered for folding, since nothing
    // would stay visible to mark that a fold exists.
    std::size_t bodyStartByte = 0;
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

// Auto-collapse-on-build follow-up: codefold::FoldableBlocks' own
// multibuffer-shaped sibling -- returns exactly the (startByte, endByte)
// "blocks" shape codefold::FoldedLineRanges/ToggleFoldAtLine already expect
// (Editor/CodeFold.h), derived from index's own excerpt spans instead of a
// fresh tree-sitter parse: a span's compositeStartByte (its header line's
// own start, the same "line a fold is keyed by" convention CodeFold's own
// blocks use) paired with its compositeEndByte. An excerpt with no header
// line (ExcerptSpan::bodyStartByte == compositeStartByte) is excluded --
// see that field's own doc comment for why. What lets both
// BufferView::EnsureHiddenLineRangesCache and code-fold-toggle
// (Commands.cpp) treat a multibuffer exactly like an ordinary foldable
// buffer, via the same two CodeFold.h functions, with no multibuffer-
// specific fold logic of their own.
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> FoldableExcerptBlocks(const MultibufferIndex& index);
void                                                           ClearMultibufferIndexFor(const text::Buffer& buffer);

// Test-only: drops every registered index, regardless of buffer. Needed
// because the registry is keyed by raw Buffer* identity with no automatic
// per-Buffer cleanup hook (Text/ must not depend on Editor/, so Buffer's
// own destructor can't call ClearMultibufferIndexFor itself) -- a bare
// BufferList constructed directly in a test (never wired through a real
// WindowManager, the only real caller of ClearMultibufferIndexFor) leaves a
// stale entry behind when its Buffers are destroyed, and a later test's
// freshly allocated Buffer can land at the same address, spuriously
// "inheriting" it. Mirrors ProviderRegistry::ClearRegistry's own
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
//
// Auto-collapse-on-build follow-up: an excerpt with a header line
// (ExcerptSource::headerText non-empty) is collapsed by default -- via an
// ordinary text::Buffer::FoldMarker::Collapsed at its own header line, the
// same one FoldableExcerptBlocks/code-fold-toggle later toggle -- when
// either its own body passes MultibufferAutoCollapseLineThreshold()'s line
// count or MultibufferAutoCollapseByteThreshold()'s byte length (a single
// huge/minified hunk), or its ordinal among excerpts passes
// MultibufferAutoCollapseExcerptCap() (a plain-large result set, e.g. very
// many references). One caller-agnostic policy applied here rather than
// duplicated in every BuildMultibuffer caller -- see
// Editor/MultibufferFoldSettings.h for the thresholds themselves.
//
// Multibuffer-gaps follow-up: at most MultibufferMaxExcerpts()
// (Editor/MultibufferLimits.h) excerpts are stitched; the rest are dropped
// and named in a trailing "N more not shown" note line of the composite's
// own text (styled like a header, outside every ExcerptSpan, so clicking it
// is the same no-op a rule line already is). totalAvailable lets a caller
// that already capped its *own* per-excerpt work -- BufferView's
// find-references pays one file read per resolved LSP location, so it
// stops reading at the cap rather than handing over excerpts it then
// discards -- report the true match count anyway, so the note names
// everything dropped rather than only what this function itself dropped.
// 0 (the default) means "excerpts is the whole set," every existing caller
// unchanged.
text::Buffer& BuildMultibuffer(text::BufferList& bufferList, const std::string& name,
                               const std::vector<ExcerptSource>& excerpts, std::size_t totalAvailable = 0);

// Editable-multibuffer follow-up (wgrep-style commit): writes every changed
// editable excerpt in composite's own ExcerptRanges() back to its real
// source Buffer (opened/found via bufferList.OpenOrCreateFile, so it always
// lands on the live, in-memory buffer -- unsaved changes in the source stay
// unsaved, participate in that buffer's own undo tree, LSP sync, etc.).
// "Changed" means the excerpt's current composite text differs from the
// ExcerptRange::originalText snapshot captured at build/last-commit time --
// an untouched excerpt is left alone. Does not write to disk: leaves that
// to the user's normal save-buffer on whichever source buffers came out
// modified, the same scope wgrep itself keeps (commits to buffers, not
// files).
// Where a commit puts the reviewed text. LiveBuffers is the default and the
// reviewable one: every touched file becomes an open, modified buffer (opened
// if it wasn't), nothing reaches disk until the user saves, and the whole
// commit is one undo. Disk is the sed-flavored one -- the file itself is
// rewritten in place, atomically, preserving its mode/xattrs/links the same
// way Buffer::SaveToFile does, with no buffer opened for it.
//
// The two aren't cleanly separable in practice and deliberately aren't kept
// so: a file whose buffer is open *and modified* is committed into that
// buffer even under Disk, because writing the file behind unsaved edits is
// exactly the staleness this subsystem exists to avoid. An open but
// unmodified buffer is reverted after the write so it shows what's now on
// disk. CommitResult reports which happened.
enum class CommitTarget { LiveBuffers,
                          Disk };

struct CommitResult {
    std::size_t committedExcerpts = 0; // ranges actually written
    std::size_t buffersCommitted  = 0; // source files applied into an open Buffer
    std::size_t filesWritten      = 0; // source files rewritten on disk directly
    // One entry per skipped range -- its source path plus why (currently
    // always "externally modified since this multibuffer was built,"
    // ExternallyModified()/ContentGeneration() checked per source buffer --
    // see CommitExcerptChanges' own doc comment). A skip never aborts the
    // rest of the commit.
    std::vector<std::pair<std::filesystem::path, std::string>> skipped;
};
// projectUndo (optional -- nullptr keeps every existing call site
// byte-identical) records the whole commit as one ProjectUndoManager
// transaction, so a commit spanning several source files backs out of all of
// them with a single undo instead of leaving siblings written while the file
// point happens to be in rolls back. A commit touching one file is left to
// that buffer's own undo, which is already exactly the right thing --
// RecordTransaction drops a single-file transaction itself.
//
// onlyPath (optional) restricts the commit to excerpts whose source is that
// one file -- the review view's "apply just this file" gesture. Every other
// excerpt is left pending, not skipped-and-reported: it isn't a failure, the
// caller simply didn't ask for it.
CommitResult CommitExcerptChanges(text::BufferList& bufferList, text::Buffer& composite,
                                  ProjectUndoManager*          projectUndo = nullptr,
                                  const std::filesystem::path* onlyPath    = nullptr,
                                  CommitTarget                 target      = CommitTarget::LiveBuffers);

// The source file an excerpt covering compositeByteOffset came from, or
// nullopt if that offset isn't inside any excerpt (a header, a rule, the
// blank between two). Reads text::Buffer::ExcerptRanges(), not the
// MultibufferIndex -- the two agree on where an excerpt's *body* is, and only
// the ranges know which of them are editable.
[[nodiscard]] std::optional<std::filesystem::path> ExcerptPathAtOffset(const text::Buffer& composite,
                                                                       std::size_t         compositeByteOffset);

// Puts the excerpt covering compositeByteOffset back to the text it had when
// the multibuffer was built (or when it was last committed) -- the
// "I don't want this one" gesture, and the reason an excerpt carries an
// originalText snapshot at all. One undo step. Returns false if that offset
// isn't inside an editable excerpt, or if the excerpt is already unchanged.
//
// Note what this deliberately is *not*: an undo of an already-committed
// change. Committing repoints originalText at the committed text, so
// reverting afterwards is a no-op by construction -- undoing a commit is the
// source buffer's own undo (see Commands.cpp's undo-buffer-only for the
// one-file-at-a-time flavor of it).
bool RevertExcerptAtOffset(text::Buffer& composite, std::size_t compositeByteOffset);

// RevertExcerptAtOffset for every excerpt sharing the source file of the one
// under compositeByteOffset -- the same gesture at file granularity, all in
// one undo step. Returns how many excerpts actually changed back.
std::size_t RevertExcerptsForFileAtOffset(text::Buffer& composite, std::size_t compositeByteOffset);

// The composite byte offset of the next/previous excerpt *body* start
// relative to compositeByteOffset, for stepping through a review without
// landing on chrome. Nullopt when there is no such excerpt (already at the
// last/first one, or none at all).
[[nodiscard]] std::optional<std::size_t> NextExcerptBodyStart(const text::Buffer& composite,
                                                              std::size_t         compositeByteOffset);
[[nodiscard]] std::optional<std::size_t> PreviousExcerptBodyStart(const text::Buffer& composite,
                                                                  std::size_t         compositeByteOffset);

} // namespace ned::editor::multibuffer

#endif // NED_EDITOR_MULTIBUFFER_H
