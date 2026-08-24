//
// A compiled user-typed regex (PCRE2) -- the engine behind the two surfaces
// that regex-edit real content: query-replace-regexp (QueryReplace.h) and
// project-replace's rewrite step (ProjectReplace.h's ReplaceMatches). PCRE2
// over std::regex for lookaround, named groups, real Unicode (\p{...}, UTF
// mode with UCP), and JIT-compiled matching; deliberately a second engine
// alongside project *search*'s RE2 (ProjectSearch.h), which can't do
// lookaround/backreferences by design -- see CMakeLists.txt's own comment on
// the pcre2 fetch. Still a backtracking engine, so every match runs under a
// match-limit safety net (kMatchLimit in the .cpp) -- a pathological
// pattern/subject combination throws RegexPatternError instead of hanging
// the editor.
//
// Semantics chosen to match what an editor user expects (Emacs' own buffer
// regexes): ^/$ anchor at line boundaries, not just subject ends
// (PCRE2_MULTILINE, LF newline convention); `.` does not match a newline;
// \w/\b/\d are Unicode-aware (PCRE2_UCP). Search() takes a start offset over
// the *whole* subject rather than a trimmed subrange, so ^, \b, and
// lookbehind correctly see what precedes the offset -- the exact gap
// QueryReplace.h used to document as a known ^-anchoring limitation. Invalid
// UTF-8 in the subject is tolerated, not an error (PCRE2_MATCH_INVALID_UTF)
// -- a "text-looking" file on disk can still hold stray bytes.
//
// The header stays PCRE2-free (pimpl) so pcre2.h never leaks onto ned_lib's
// public include surface -- the same privacy treatment RE2 gets via
// ProjectSearch.cpp being its only includer.
//

#ifndef NED_EDITOR_REGEXPATTERN_H
#define NED_EDITOR_REGEXPATTERN_H

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ned::editor {

// Thrown by the constructor for an invalid pattern (carrying PCRE2's own
// diagnostic plus the offset it points at), and by Search/ReplaceAll if the
// match-limit safety net trips mid-match.
class RegexPatternError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct RegexGroupSpan {
    bool        matched = false; // false for a group that didn't participate, e.g. (a)? against "b"
    std::size_t start   = 0;     // byte offsets into the subject; meaningful only when matched
    std::size_t end     = 0;
};

struct RegexMatch {
    std::size_t                 start = 0; // whole-match byte range
    std::size_t                 end   = 0;
    std::vector<RegexGroupSpan> groups; // [0] is the whole match, then one entry per capture group
};

class RegexPattern {
  public:
    explicit RegexPattern(const std::string& pattern); // throws RegexPatternError
    ~RegexPattern();
    RegexPattern(RegexPattern&&) noexcept;
    RegexPattern& operator=(RegexPattern&&) noexcept;
    RegexPattern(const RegexPattern&)            = delete;
    RegexPattern& operator=(const RegexPattern&) = delete;

    // First match at or after startOffset. Returns nullopt when there is no
    // match (including startOffset past the end of subject).
    [[nodiscard]] std::optional<RegexMatch> Search(std::string_view subject, std::size_t startOffset = 0) const;

    // Expands replacementTemplate against one match: $1..$99 / ${n} group
    // references, ${name} for named groups, $& (or $0) the whole match,
    // $` / $' the subject text before/after the match, $$ a literal dollar
    // -- std::smatch::format's ECMAScript set (kept so replacement text
    // written against the old std::regex engine keeps working), plus named
    // groups. An unmatched-but-valid group expands to nothing; anything
    // unrecognized passes through literally rather than erroring -- the
    // template is live user input typed mid-session.
    [[nodiscard]] std::string FormatReplacement(std::string_view subject, const RegexMatch& match,
                                                std::string_view replacementTemplate) const;

    struct ReplaceAllResult {
        std::string text;
        std::size_t count = 0; // matches replaced
    };

    // Every match in subject replaced in one pass. A zero-width match
    // advances one codepoint after replacing, so `a*` against "bbb"
    // terminates (matching std::regex_replace's own stepping).
    [[nodiscard]] ReplaceAllResult ReplaceAll(std::string_view subject, std::string_view replacementTemplate) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ned::editor

#endif // NED_EDITOR_REGEXPATTERN_H
