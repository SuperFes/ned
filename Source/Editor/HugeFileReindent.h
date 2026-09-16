//
// configurable-formatter follow-up: the huge-file streaming sweep. A whole-
// buffer Native reindent (Editor/Indent.h's IndentBuffer/IndentRegion) needs
// a real tree-sitter parse, which cannot run over a multi-GB document at
// all -- and even IndentRegion's own huge-file WINDOWING (bounded, re-parsed
// per call) exists for a viewport/single-edit's worth of lines, not for
// reindenting the entire file line by line, which would cost one bounded
// re-parse PER LINE. This is a genuinely different, much simpler engine for
// that one specific case: a LEXICAL, string/line-comment-aware bracket-depth
// counter -- no parse, no tree -- streamed via ITextStorage::ForEachChunk so
// the whole document is never materialized into one string.
//
// Scope, stated plainly rather than silently assumed:
// - Depth is tracked over the three ASCII bracket pairs `()`, `{}`, `[]`
//   only -- not the full per-language Imprint vocabulary (Editor/Imprint.h)
//   a real parse would consult (sigil-prefixed brackets like Janet's `@[`,
//   keyword-delimited bodies like bash's `do`/`done`, Python's own
//   indentation-only bodies with no bracket at all). Sufficient for the
//   overwhelming majority of real code in every bracket-based bundled
//   language; a language whose real nesting isn't fully captured by these
//   three pairs alone degrades to a flatter-than-ideal result rather than a
//   silently wrong one -- see the "all-or-nothing" note below for why that
//   is bounded rather than corrupting.
// - String literals are recognized generically (`"`/`'`, backslash-escaped)
//   -- not per-language (a raw string/heredoc/triple-quote convention isn't
//   modeled). This makes a bare `'` in ordinary prose (an English
//   contraction) look exactly like a string opener -- but line-comment
//   content (see next) is checked BEFORE string detection ever runs and
//   returns immediately, so a real "// don't do this" comment is
//   completely immune: only bare CODE containing a stray, unmatched quote
//   character is exposed at all, which is rare in every bundled language's
//   own real syntax. Line comments use Mode::lineCommentPrefix (already
//   modeled per language); BLOCK comments are NOT recognized at all -- a
//   bracket-like character inside one affects the depth counter exactly as
//   real code would. In the common case a commented-out balanced snippet
//   still cancels out; a comment containing a genuinely unbalanced bracket
//   can desync the counter for the rest of the file.
// - Every line's own new indent is exactly its nesting depth times
//   style.width -- a flat, uniform rule with none of Indent.h's own
//   per-construct nuance (alignment, indent.body, access-specifier-shaped
//   overrides, ...). Good enough to make a huge file'ss indentation
//   CONSISTENT; not a promise of matching what the real per-language engine
//   would produce on an ordinary-sized copy of the same file.
//
// All-or-nothing, not best-effort: depth is checked for negative (an extra
// closer before its opener) as it's tracked, and for landing back at exactly
// 0 by end of input (Finish()) -- either failure aborts the WHOLE sweep,
// leaving the real file completely untouched (the caller is responsible for
// writing to a separate temp file and only renaming it over the original on
// success -- see StreamHugeReindent's own doc comment). A wrong reindent
// silently applied to a file this large is far harder to notice or recover
// from than a plain refusal, so a lexical scan this simple is deliberately
// conservative about ever claiming success.
//

#ifndef NED_EDITOR_HUGEFILEREINDENT_H
#define NED_EDITOR_HUGEFILEREINDENT_H

#include <cstddef>
#include <fstream>
#include <string>
#include <string_view>

#include "IndentStyle.h"
#include "Text/ITextStorage.h"

namespace ned::editor {

struct HugeReindentOutcome {
    bool        success      = false;
    std::size_t linesChanged = 0;
    std::string errorMessage; // set only when !success
};

// The streaming state machine itself, exposed for direct use against
// ITextStorage::ForEachChunk (StreamHugeReindent below is the usual, whole-
// storage-in-one-call entry point; this is what a caller wanting more
// control over the write destination composes directly).
class HugeReindentStream {
  public:
    HugeReindentStream(std::ofstream& out, std::string lineCommentPrefix, IndentStyle style);

    // Feeds one chunk (any size, no alignment guarantee -- ITextStorage::
    // ForEachChunk's own contract). A no-op once failure has already been
    // detected (Finish() still reports it).
    void operator()(std::string_view chunk);

    // Call once after every chunk has been fed. Flushes buffered output and
    // makes the final "did this resolve cleanly" determination (depth back
    // to exactly 0, no unterminated string). Safe to call exactly once.
    [[nodiscard]] HugeReindentOutcome Finish();

  private:
    void Feed(char c);
    void EmitIndentForNewLine(bool firstByteIsCloser);
    void WriteByte(char c);
    void MaybeFlush();
    void FlushBuffer();
    void Fail(std::string reason);

    std::ofstream& out_;
    std::string    lineCommentPrefix_;
    IndentStyle    style_;

    std::string outBuffer_;
    std::string recentBytes_;   // rolling tail, bounded to lineCommentPrefix_.size(), for comment-start matching
    std::string originalIndent_; // the current line's own leading whitespace, held only to detect whether this
                                 // line's indent actually changed (linesChanged_) -- never written verbatim

    long long   depth_       = 0;
    std::size_t linesChanged_ = 0;

    bool atLineStart_   = true; // still skipping this line's own leading whitespace
    bool inLineComment_ = false;
    bool inString_      = false;
    bool escapeNext_    = false;
    char stringQuote_   = '\0';

    bool        failed_ = false;
    std::string failureReason_;
};

// The usual entry point: streams every chunk of `source` through a fresh
// HugeReindentStream writing to `out`. `lineCommentPrefix` and `style` are
// exactly Mode::lineCommentPrefix and the buffer's own EffectiveIndentStyle
// -- callers already have both without this needing a Mode dependency of
// its own (kept storage-level, mirroring Text/WhitespaceHygiene.h's own cut
// between pure text transforms and the Editor-level callers that configure
// them).
[[nodiscard]] HugeReindentOutcome StreamHugeReindent(const text::ITextStorage& source, std::ofstream& out,
                                                     const std::string& lineCommentPrefix, const IndentStyle& style);

} // namespace ned::editor

#endif // NED_EDITOR_HUGEFILEREINDENT_H
