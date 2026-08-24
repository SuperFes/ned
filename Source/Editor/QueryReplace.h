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

#ifndef NED_EDITOR_QUERYREPLACE_H
#define NED_EDITOR_QUERYREPLACE_H

#include <cstddef>
#include <optional>
#include <string>

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

    text::Buffer&               buffer_;
    Stage                       stage_ = Stage::EnteringPattern;
    std::string                 patternText_;
    std::string                 replacementText_;
    std::optional<RegexPattern> pattern_; // set by ConfirmPattern
    std::string                 content_; // kept in sync with the buffer as replacements happen
    std::size_t                 searchCursor_     = 0;
    std::size_t                 replacementCount_ = 0;

    bool        hasMatch_   = false;
    std::size_t matchStart_ = 0;
    std::size_t matchEnd_   = 0;
    std::string matchFormattedReplacement_; // this match's $1-expanded replacement text
};

} // namespace ned::editor

#endif // NED_EDITOR_QUERYREPLACE_H
