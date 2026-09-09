//
// Emacs query-replace-regexp, simplified to a single always-regex mode
// (Emacs distinguishes plain query-replace from query-replace-regexp; a
// literal string is a valid regex too, so skipping that distinction loses
// nothing). Runs on PCRE2 via RegexPattern.h (in-file-regex follow-up --
// formerly std::regex, which had no lookaround, no named groups, and a
// documented ^-anchoring bug from searching trimmed subranges; RegexPattern
// searches the whole text from an offset, so ^/\b/lookbehind see preceding
// context correctly, and ^/$ anchor at line boundaries as an editor user
// expects). Replacement text supports $1/$2/$&/${name}-style references
// (RegexPattern::FormatReplacement's full set), NOT Emacs' \1/\2 -- a real,
// minor syntax difference kept from the std::regex era, not a bug.
//
// huge-file-regex-replace follow-up: a huge (ITextStorage::IsHuge())
// buffer's content is never materialized into content_ -- ConfirmReplacement
// checks Content().IsHuge() once and every stage branches into a windowed
// path (FindNextMatchHuge and the huge_ branches of ReplaceAndNext/
// SkipAndNext) that reads bounded windows via Content().Substring instead,
// same shape as IncrementalSearch's SearchHuge. Forward-only, no wraparound,
// matching the existing (non-huge) behavior already.
//
// binary-safety-guardrails follow-up: ConfirmReplacement refuses outright
// (Stage::Done, a distinct StatusText() message) when
// buffer_.BinarySafeguardsActive() -- same posture as format-on-save/
// convert-line-endings' own guard sites (Buffer.h's own doc comment on that
// predicate), since the replace step writes bytes back into the buffer too.
// Pattern entry/searching alone is unaffected (harmless, read-only).
//

#ifndef NED_EDITOR_QUERYREPLACE_H
#define NED_EDITOR_QUERYREPLACE_H

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "RegexPattern.h"
#include "Text/Buffer.h"

namespace ned::editor {

class QueryReplace {
  public:
    enum class Stage { EnteringPattern,
                       EnteringReplacement,
                       Confirming,
                       Done };

    explicit QueryReplace(text::Buffer& buffer);

    // multibuffer-scoped-search follow-up: confines matching to the buffer's
    // own editable excerpt bodies (text::Buffer::ExcerptRange, editable
    // ones only) -- a match overlapping an excerpt's header/rule chrome is
    // skipped rather than offered. Off by default: every non-multibuffer
    // session is byte-for-byte unchanged, and a buffer with no editable
    // excerpt ranges is unscoped whichever way this is set (there is nothing
    // to scope to, and a read-only multibuffer refuses the replacement one
    // level down regardless).
    //
    // Deliberately a flag rather than a range list handed in the way
    // IncrementalSearch::SetSearchScope takes one: replacements shift every
    // offset after them, and ExcerptRanges are relocated across exactly
    // those edits, so re-reading them per match is what keeps the scope
    // correct mid-session. Ignored on the huge-buffer path, which no
    // multibuffer ever takes.
    void SetScopeToExcerptBodies(bool enabled);

    // Valid during EnteringPattern/EnteringReplacement; a no-op otherwise.
    void AppendChar(char32_t codepoint);
    void DeleteChar();

    // EnteringPattern -> EnteringReplacement. Throws RegexPatternError if
    // the pattern is invalid; the stage does not advance in that case. A
    // no-op if the pattern is empty or the stage isn't EnteringPattern.
    void ConfirmPattern();

    // EnteringReplacement -> Confirming (or straight to Done if there's no
    // match at all). A no-op if the stage isn't EnteringReplacement.
    void ConfirmReplacement();

    // Valid during Confirming; a no-op otherwise.
    void ReplaceAndNext(); // 'y'
    void SkipAndNext();    // 'n'
    void ReplaceAll();     // '!' -- replaces this and every remaining match, then Done
    void Finish();         // 'q' -- stop without touching the current match

    // Ends the session at any stage. Does NOT undo replacements already
    // made -- each one went through Buffer's normal undo-recording
    // DeleteRange/InsertAt, so it's undoable individually like any other
    // edit, matching Emacs' own query-replace (C-g doesn't revert prior
    // replacements in that session either).
    void Cancel();

    [[nodiscard]] Stage       CurrentStage() const;
    [[nodiscard]] std::string StatusText() const;
    [[nodiscard]] std::size_t ReplacementCount() const;

  private:
    void FindNextMatch();
    // The scope ranges as of right now -- empty when scoping is off or the
    // buffer has no editable excerpts. Re-read per call, see
    // SetScopeToExcerptBodies' own doc comment.
    [[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> ScopeRanges() const;
    // Where to resume searching after scope rejected a match starting at
    // matchStart -- the next scope range's own start when the match began
    // before it, one codepoint on when it began inside one but overran its
    // end, and nullopt when no range remains at all (the session is done).
    [[nodiscard]] std::optional<std::size_t> ResumeAfterRejectedMatch(std::size_t matchStart) const;
    // huge-file-regex-replace follow-up: FindNextMatchHuge is the huge_
    // branch of FindNextMatch -- windowed scanning via Content().Substring
    // instead of content_ (left empty for a huge buffer). See the .cpp for
    // the window/overlap-margin scheme.
    void FindNextMatchHuge();

    text::Buffer&               buffer_;
    Stage                       stage_ = Stage::EnteringPattern;
    std::string                 patternText_;
    std::string                 replacementText_;
    std::optional<RegexPattern> pattern_;      // set by ConfirmPattern
    bool                        huge_ = false; // buffer_.Content().IsHuge(), decided in ConfirmReplacement
    std::string                 content_;      // kept in sync with the buffer as replacements happen; empty when huge_
    std::size_t                 searchCursor_     = 0;
    std::size_t                 replacementCount_ = 0;

    bool        hasMatch_   = false;
    std::size_t matchStart_ = 0;
    std::size_t matchEnd_   = 0;
    std::string matchFormattedReplacement_; // this match's $1-expanded replacement text

    // binary-safety-guardrails follow-up: set by ConfirmReplacement when
    // buffer_.BinarySafeguardsActive() refuses the session outright; changes
    // StatusText()'s Stage::Done wording from the ordinary replacement-count
    // summary to a refusal message.
    bool binaryRefused_ = false;

    // See SetScopeToExcerptBodies' own doc comment.
    bool scopeToExcerptBodies_ = false;
};

} // namespace ned::editor

#endif // NED_EDITOR_QUERYREPLACE_H
