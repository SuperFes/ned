//
// Emacs isearch, simplified: unlike real Emacs, a query change always
// re-searches fresh from the session's start point rather than trying to
// extend the current match in place first -- simpler, and produces the same
// result for the common case (typing a longer, more specific query), just
// potentially rescanning more than strictly necessary.
//
// Buffer text is materialized once at construction: isearch never mutates
// the buffer, so that snapshot can't go stale during a session. Exception:
// a huge (ITextStorage::IsHuge()) buffer is never materialized at all --
// see SearchHuge()'s own comment -- since even one copy of a multi-GB
// buffer, let alone the lowercased second copy, is exactly what huge-file
// editing's whole design otherwise avoids.
//
// Matching is smart-case, Emacs-style: case-insensitive unless the query
// itself contains an uppercase letter, at which point the whole search
// becomes case-sensitive. ASCII-only, like Buffer::MoveForwardWord's own
// word-char definition -- not Unicode case folding.
//

#ifndef NED_EDITOR_INCREMENTALSEARCH_H
#define NED_EDITOR_INCREMENTALSEARCH_H

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Text/Buffer.h"

namespace ned::editor {

class IncrementalSearch {
  public:
    enum class Direction { Forward,
                           Backward };

    IncrementalSearch(text::Buffer& buffer, Direction direction);

    // multibuffer-scoped-search follow-up: confines every subsequent search
    // to these byte ranges -- a match is accepted only when it lies wholly
    // inside one of them. Sorted-by-start, non-overlapping, half-open
    // [start, end); an empty vector (the default) means "the whole buffer,"
    // i.e. every pre-existing session is byte-for-byte unchanged.
    //
    // Meant to be called immediately after construction, before the first
    // query character: the snapshot this class searches is taken at
    // construction and never re-read, so the ranges can't go stale during a
    // session either. Ignored outright for a huge buffer (see SearchHuge()'s
    // own comment -- the reduced-feature-set precedent this follows), which
    // no multibuffer ever is.
    void SetSearchScope(std::vector<std::pair<std::size_t, std::size_t>> ranges);

    void AppendChar(char32_t codepoint);
    void DeleteChar(); // removes the last character of the query, if any

    // Appends arbitrary text to the query and re-searches, like a
    // multi-codepoint AppendChar -- backs the isearch C-y (yank kill-ring
    // text into the search string) binding. A no-op if text is empty.
    void AppendText(std::string_view text);

    // Appends the word (ASCII alnum/underscore run, matching
    // Buffer::MoveForwardWord's own word-char definition) starting at the
    // current point to the query and re-searches -- backs the isearch C-w
    // binding. A no-op if there's no word-shaped text left in that
    // direction from the current point.
    void AppendWordAtPoint();

    // Finds the next occurrence in the same direction, continuing past the
    // current match. A no-op if the query is empty.
    void RepeatSearch();

    // Flips Forward/Backward and re-searches from the current point --
    // backs isearch's C-r-during-forward-search /
    // C-s-during-backward-search direction-reversal binding. A no-op if the
    // query is empty (nothing to re-search for).
    void ReverseDirection();

    void Accept(); // keep the current point, end the session
    void Cancel(); // restore the original point, end the session

    [[nodiscard]] const std::string& Query() const;
    [[nodiscard]] bool               Found() const;
    // "I-search: query" / "Failing I-search: query" (backward prepends "Backward").
    [[nodiscard]] std::string StatusText() const;
    // StatusText()'s own prefix, everything up to but not including the
    // query itself -- split out so a caller that wants to render the query
    // with its own byte-range styling (partial-match-highlighting follow-up:
    // the UI layer distinguishing a failing query's still-matching prefix
    // from the rest) doesn't have to duplicate this format string.
    [[nodiscard]] std::string StatusLabel() const;

    // Byte length of the longest prefix of Query() that still matches
    // somewhere in the buffer -- Query().size() itself whenever Found() is
    // true (the whole query matches) or the buffer is huge (see below).
    // Lets a caller highlight exactly the trailing bytes responsible for a
    // failing search (real editors' "show me where it stopped matching"
    // isearch UX) without re-deriving the search itself. Not computed for a
    // huge buffer -- always Query().size() there regardless of Found(),
    // matching the existing reduced-feature-set precedent (SearchHuge()'s
    // own header comment): a caller sees "no shorter prefix info available"
    // and falls back to its plain, whole-query rendering.
    [[nodiscard]] std::size_t MatchedPrefixLength() const;

  private:
    // Whether [start, end) lies wholly inside one scope range -- always true
    // when scope_ is empty (the unscoped default).
    [[nodiscard]] bool InScope(std::size_t start, std::size_t end) const;
    // haystack.find/rfind's scope-aware equivalents: the nearest match at or
    // after (resp. before) `from` that InScope accepts, npos if there is
    // none. Plain find/rfind when scope_ is empty.
    [[nodiscard]] std::size_t FindForward(const std::string& haystack, const std::string& needle, std::size_t from) const;
    [[nodiscard]] std::size_t FindBackward(const std::string& haystack, const std::string& needle, std::size_t before) const;

    void Search(std::size_t from);
    // huge-file-search-and-save follow-up: SearchHuge is the huge_ branch
    // of Search -- windowed scanning via Content().Substring instead of
    // content_/contentLower_, which are left empty for a huge buffer.
    void SearchHuge(std::size_t from);
    // partial-match-highlighting follow-up: called only from Search() (never
    // SearchHuge(), see MatchedPrefixLength()'s own doc comment) right after
    // the full query_ has failed to match anywhere -- walks query_ backward
    // one codepoint at a time until a shorter prefix exists somewhere in the
    // buffer (existence only; direction/wrap-around, relevant to Search()
    // itself, don't apply here) or nothing shorter than the whole query is
    // left.
    [[nodiscard]] std::size_t ComputeMatchedPrefixLength() const;

    text::Buffer& buffer_;
    Direction     direction_;
    bool          huge_;         // buffer_.Content().IsHuge(), cached -- decided once, at construction
    std::string   content_;      // buffer text, materialized once; empty when huge_
    std::string   contentLower_; // ASCII-lowercased content_, for case-insensitive matching; empty when huge_
    std::string   query_;
    std::size_t   originalPoint_;
    bool          found_               = true;
    std::size_t   matchedPrefixLength_ = 0; // see MatchedPrefixLength()'s own doc comment
    // Empty == unscoped, the default -- see SetSearchScope's doc comment.
    std::vector<std::pair<std::size_t, std::size_t>> scope_;
};

} // namespace ned::editor

#endif // NED_EDITOR_INCREMENTALSEARCH_H
