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

#ifndef NED_EDITOR_PROJECTREPLACE_H
#define NED_EDITOR_PROJECTREPLACE_H

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "ProjectSearch.h"

namespace ned::editor {

struct ReplaceSummary {
    std::size_t filesChanged     = 0;
    std::size_t replacementCount = 0;
};

class ProjectReplace {
  public:
    enum class Stage { EnteringPattern,
                       EnteringReplacement,
                       Confirming,
                       Done };

    explicit ProjectReplace(std::filesystem::path root);

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

    // Valid during Confirming only. Confirm() performs the actual rewrite
    // (see ReplaceMatches below) and moves to Done; Cancel() moves to Done
    // without touching any file.
    [[nodiscard]] ReplaceSummary Confirm();
    void                         Cancel();

    [[nodiscard]] Stage                           CurrentStage() const;
    [[nodiscard]] std::string                     StatusText() const;
    [[nodiscard]] const std::vector<SearchMatch>& Matches() const;

  private:
    std::filesystem::path    root_;
    Stage                    stage_ = Stage::EnteringPattern;
    std::string              patternText_;
    std::string              replacementText_;
    std::vector<SearchMatch> matches_;
};

// Rewrites every unique file referenced in matches, replacing every
// occurrence of pattern with replacement -- RegexPattern::ReplaceAll (PCRE2,
// in-file-regex follow-up; formerly std::regex) over each file's *full*
// content (not line-by-line), so multiple occurrences on one line are all
// counted and replaced, not just the single SearchMatch recorded per
// matching line -- and, with PCRE2_MULTILINE, ^/$ anchor at every line
// boundary of that full content, matching what the per-line search preview
// showed. Writes via a sibling "<path>.ned-tmp" file then
// std::filesystem::rename, mirroring Buffer::SaveToFile's own safety
// pattern, so a failure partway through a given file can't leave it
// truncated. Skips (without counting) any file that can't be read or
// written. Throws RegexPatternError if pattern is invalid. The old
// split-engine caveat (RE2-validated pattern failing here) is practically
// closed -- PCRE2 accepts essentially everything ConfirmPattern's RE2
// preview does -- but the reverse constraint remains: PCRE2-only syntax
// (lookaround, backreferences) is rejected up front by the RE2-backed
// preview, so it can't be used in a *project* replace, only in the
// single-buffer query-replace-regexp. ProjectReplace::Confirm's caller
// (BufferView::HandleProjectReplaceKey) still catches exceptions here --
// the match-limit safety net can trip at rewrite time (see RegexPattern.h).
[[nodiscard]] ReplaceSummary ReplaceMatches(const std::vector<SearchMatch>& matches, const std::string& pattern,
                                            const std::string& replacement);

} // namespace ned::editor

#endif // NED_EDITOR_PROJECTREPLACE_H
