//
// rename-review follow-up (ROADMAP.md, "Rename through the review
// multibuffer, not blind"): the pure half of turning a rename's edits into
// an editable review multibuffer -- classifying every hit as a real code
// reference, a comment or a string, finding the comment/string occurrences
// a rename *didn't* offer, and laying the result out as
// multibuffer::ExcerptSource rows with a proposed body per row.
//
// Pure and buffer-free, the same split Text/ThreeWayMerge.h and
// Editor/LocalScopes.h already take: everything here works over plain text
// plus a HighlightSpan list, so it is unit-testable with no Buffer, no
// Parser and no Screen. BufferView owns the I/O half -- reading each file's
// live text, calling Mode::highlight, building the multibuffer and applying
// the proposed bodies into it.
//
// Two things make this more than a preview of edits that were going to
// happen anyway:
//
//   * A server's rename response is references-only, so classification
//     alone would always report "all clean". The value is the inverse
//     direction -- FindExtraCandidates scans each touched file for
//     whole-word occurrences of the old name the edit set does NOT cover
//     and keeps the ones that land inside a comment or a string. That is
//     JetBrains' "Search in comments and strings" checkbox, over text the
//     review already has in hand.
//   * Those extra candidates are excluded by default (an excerpt whose
//     proposed body is its original body commits nothing), so the risky
//     half is opt-in per excerpt rather than something to notice and undo.
//
// Classification needs no new query: Mode::highlight already spans every
// comment and string in all bundled languages, and SyntaxClass already
// separates them from everything else. Mode.h's overlap rule (later spans
// win) is honoured -- the last span covering an offset decides.
//

#ifndef NED_EDITOR_RENAMEREVIEW_H
#define NED_EDITOR_RENAMEREVIEW_H

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Editor/Mode.h"
#include "Editor/Multibuffer.h"

namespace ned::editor::rename {

// What a single occurrence of the renamed name turned out to be. Reference
// is the only kind a rename applies by default; the other two are exactly
// the ones a project-wide search-and-replace would rewrite blind and an
// actual rename must not.
enum class HitKind { Reference,
                     Comment,
                     String };

// One occurrence within a single file's text. startByte/endByte are that
// file's own byte offsets, endByte exclusive.
struct RenameHit {
    std::size_t startByte = 0;
    std::size_t endByte   = 0;
    HitKind     kind      = HitKind::Reference;
};

// Every hit in one file, plus what the review needs to render it. text is
// the file's full content (the live buffer's, when one is open -- the
// caller resolves that). hits must be sorted by startByte and
// non-overlapping; a caller merging two sources (a server's edits plus
// FindExtraCandidates' own) is responsible for that ordering, which
// FindExtraCandidates itself already preserves.
struct FileRenameHits {
    std::filesystem::path  file;
    std::string            displayPath; // project-relative when the caller could resolve one
    std::string            text;
    std::vector<RenameHit> hits;
};

// One review row: the excerpt as it stands on disk/in the buffer, plus the
// two proposed rewrites of its body. An excerpt cycles original ->
// referencesOnly -> allHits (see BufferView's own M-a/M-r handling); the
// three collapse into fewer distinct states whenever a row has only one
// kind of hit, which is the common case.
struct ReviewExcerpt {
    multibuffer::ExcerptSource source;             // body is the ORIGINAL text, never a proposal
    std::string                referencesOnlyBody; // == source.bodyText when the row has no reference hit
    std::string                allHitsBody;        // == referencesOnlyBody when the row has no risky hit
    bool                       hasReference = false;
    bool                       hasRisky     = false; // any Comment or String hit
};

// The SyntaxClass -> HitKind mapping on its own, exposed because it is the
// one rule a new SyntaxClass could invalidate and the cheapest thing to
// pin in a test.
[[nodiscard]] HitKind KindForSyntaxClass(SyntaxClass syntaxClass);

// The last span covering startByte decides, per Mode.h's own overlap rule.
// An offset no span covers is a Reference -- unhighlighted text is code as
// far as every bundled query is concerned, and defaulting the other way
// would silently exclude real references in a mode with no highlighter.
[[nodiscard]] HitKind ClassifyHit(const std::vector<HighlightSpan>& spans, std::size_t startByte);

// Whole-word occurrences of name in text that `covered` (sorted by
// startByte) does not already contain, classified via spans, keeping only
// the Comment and String ones. Returned sorted by startByte.
//
// "Whole word" is an ASCII identifier boundary -- alphanumerics, '_', every
// byte >= 0x80 (so a UTF-8 word is never split), and, deliberately, every
// character appearing in name itself. That last clause is what makes a
// Lisp/Clojure name like foo-bar or valid? scan correctly without this
// module needing a per-language identifier charset; its cost is a missed
// candidate in a language where the same character is an operator (C's
// x-1 while renaming x), which is the safe direction to be wrong in for a
// row that is opt-in anyway.
[[nodiscard]] std::vector<RenameHit> FindExtraCandidates(std::string_view text, const std::vector<HighlightSpan>& spans,
                                                         std::string_view name, const std::vector<RenameHit>& covered);

// Lays every file's hits out as review rows: one row per line-range a
// group of hits shares, its body the original line(s), its header the
// project-relative path, line and a tag naming what the row contains
// ("[comment]", "[string]", "[+comment]" for a row mixing a real reference
// with a risky one; a pure-reference row carries no tag at all).
//
// Rows are grouped by risk rather than left in file order: every row
// carrying a real reference first, then comment-only rows, then
// string-only ones, each group keeping the caller's own file order and
// ascending line order within it. That is the grouping a review wants --
// the rows that are going to be applied read as one block, and the ones
// that are not sit below them instead of interleaved.
[[nodiscard]] std::vector<ReviewExcerpt> BuildReviewExcerpts(const std::vector<FileRenameHits>& files,
                                                             const std::string&                 newName);

} // namespace ned::editor::rename

#endif // NED_EDITOR_RENAMEREVIEW_H
