//
// Emacs-flavored project-wide query-replace-regexp, simplified to a single
// whole-batch confirmation rather than a per-match y/n/!/q loop across many
// files (project-replace follow-up) -- see ROADMAP.md for why per-match
// confirmation across files was scoped out in favor of this. Mirrors
// QueryReplace's stage shape (EnteringPattern -> EnteringReplacement ->
// Confirming -> Done), but Confirming here means "review the previewed
// match list and confirm/cancel the whole batch," not "step through
// individual matches."
//
// Deliberately does not decide how the preview gets shown -- that's
// BufferView's job (it builds a results buffer from Matches() the same way
// project-search already does, and keeps it active through the whole flow
// so the file/line list stays visible while the replacement text is typed
// and while the final y/n confirmation is pending -- this is meant to be
// clear about exactly what's about to change, not a terse one-line count).
//

#ifndef NED_EDITOR_PROJECT_REPLACE_H
#define NED_EDITOR_PROJECT_REPLACE_H

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "Search.h"

namespace ned::text {
class BufferList;
} // namespace ned::text

namespace ned::editor {

class ProjectReplace {
  public:
    enum class Stage { EnteringPattern,
                       EnteringReplacement,
                       Confirming,
                       Done };

    // live-buffer-search follow-up: liveBuffers (optional -- nullptr keeps
    // the pre-existing disk-only search verbatim, which is what every test
    // constructing this bare relies on) is handed to SearchDirectory so the
    // previewed match list reflects unsaved edits, not just what's on disk.
    explicit ProjectReplace(std::filesystem::path root, text::BufferList* liveBuffers = nullptr);

    // Valid during EnteringPattern/EnteringReplacement; a no-op otherwise.
    void AppendChar(char32_t codepoint);
    void DeleteChar();

    // EnteringPattern -> EnteringReplacement. Runs SearchDirectory(root,
    // pattern) as a side effect, populating Matches() so the caller can
    // preview the affected files/lines before the replacement text is even
    // entered. Throws SearchPatternError if the pattern is invalid (RE2
    // syntax -- see ProjectSearch.h); the stage does not advance in that
    // case. A no-op if the pattern is empty or the stage isn't
    // EnteringPattern.
    void ConfirmPattern();

    // EnteringReplacement -> Confirming, or straight to Done if there were
    // no matches at all (mirrors QueryReplace's own "nothing to do" case).
    // A no-op if the stage isn't EnteringReplacement.
    void ConfirmReplacement();

    // Valid during Confirming only. Cancel() moves to Done.
    //
    // project-replace-review follow-up: there is no Confirm() here any more.
    // Applying the replacement is not this class's job at all now -- the
    // caller turns Matches()/PatternText()/ReplacementText() into an editable
    // review multibuffer (BufferView::BuildProjectReplaceReview) and the
    // review is applied through multibuffer-apply-changes, which is also
    // where the "into open buffers or straight to the files" choice lives.
    // That kept one replace path instead of two that could disagree about
    // what a match means.
    void Cancel();

    [[nodiscard]] Stage                           CurrentStage() const;
    [[nodiscard]] std::string                     StatusText() const;
    [[nodiscard]] const std::vector<SearchMatch>& Matches() const;
    // project-replace-review follow-up: the caller builds the review
    // multibuffer from these two plus Matches(), so it needs them back out
    // rather than only embedded in StatusText().
    [[nodiscard]] const std::string& PatternText() const;
    [[nodiscard]] const std::string& ReplacementText() const;

  private:
    std::filesystem::path    root_;
    text::BufferList*        liveBuffers_ = nullptr; // see the constructor's own doc comment
    Stage                    stage_       = Stage::EnteringPattern;
    std::string              patternText_;
    std::string              replacementText_;
    std::vector<SearchMatch> matches_;
};

} // namespace ned::editor

#endif // NED_EDITOR_PROJECT_REPLACE_H
