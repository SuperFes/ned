//
// vim-magic-translation follow-up. Real vim's *default* ('magic') pattern escaping
// convention is the opposite of PCRE2's (Editor/RegexPattern.h, the engine backing this
// codebase's own search/:s/:g) for one specific set of characters -- vim requires a
// backslash to *activate* grouping/alternation/quantifier meaning for
// `( ) | + ? = { }`, treating the bare character as literal, while PCRE2 (like every
// Perl-descended regex flavor) treats those same characters as special *without* a
// backslash. Everything else vim's default magic already treats as special without a
// backslash (`. * ^ $ [ ]`) already agrees with PCRE2, so those need no translation.
//
// TranslateVimMagicPattern is the one function that bridges this: given a pattern as the
// user actually typed it (vim's own default-magic convention), it returns the
// equivalent PCRE2 pattern RegexPattern can compile directly. Applied once, at the
// point a pattern is first read from typed command-line text (VimEngine::PerformSearch
// for `/`/`?`, VimEngine::ExecuteSubstitute/ExecuteGlobal for `:s`/`:g`) -- everything
// downstream of that (RunSearch/RepeatSearch/SubstituteLineRange/the "&" repeat-last-
// substitute command) already operates on an already-translated, PCRE2-ready pattern
// string, so it must never be applied a second time to the same pattern.
//
// Handled, beyond the "no translation needed" set above:
//   \(  \)  \|  \+  \?      -> (  )  |  +  ?          (bare forms escaped the other way)
//   (  )  |  +  ?  {  }     -> \(  \)  \|  \+  \?  \{  \}   (kept literal for PCRE2)
//   \=                      -> ?                       (vim's own \? synonym)
//   \<  \>                  -> \b  \b                  (word-boundary; PCRE2's \b isn't
//                                                        directional, a documented
//                                                        approximation)
//   \{n,m}  \{n}  \{,m}     -> {n,m}  {n}  {,m}         (PCRE2 already shares this syntax;
//                                                        vim's own \{...} is asymmetric --
//                                                        only the *opening* brace is
//                                                        backslash-escaped, the closer is
//                                                        a bare "}", confirmed against
//                                                        :help /\{)
//   \{-...}                 -> ...?                     (vim's "-" = non-greedy, expressed
//                                                        in PCRE2 as a trailing "?")
//   \{}  \{-}                -> *  *?                    (empty-body interval = "*")
//   \%(                     -> (?:                       (non-capturing group)
//   \zs                     -> \K                         (reset the reported match start;
//                                                        PCRE2's closest equivalent)
//   a bracket expression `[...]` is passed through verbatim, untranslated, throughout --
//   none of the above apply inside one (matches real vim's own bracket-expression rules,
//   which don't need backslash-escaping for grouping/quantifier characters either)
//
// Deliberately NOT handled (documented v1 cuts -- passed through byte-for-byte, which
// may compile to something PCRE2-incompatible or subtly different for these rare cases):
//   \ze (reset the reported match *end* -- no clean single-token PCRE2 equivalent);
//   \%[...] (optional-sequence matching, e.g. "r\%[ead]");
//   \m/\M/\V mid- or whole-pattern mode switches (only a *leading* \v is handled --
//   see below); vim's own character-class shorthand atoms beyond what already means the
//   same thing in PCRE2 unmodified (\d \s \S \w \W pass through fine as-is; \a \l \u \x
//   \o are vim-specific and either mean something different in PCRE2 or nothing at all,
//   left untranslated).
//
// A pattern starting with vim's very-magic prefix "\v" is a special, higher-value case:
// its own escaping convention already requires backslash to make `( ) | + ?` etc.
// *literal* -- i.e., the opposite of default magic and the *same* direction PCRE2 uses
// -- so a `\v`-prefixed pattern needs no translation at all beyond stripping the prefix
// itself, and is returned with the remainder passed through completely unmodified. This
// also preserves this codebase's own pre-existing behavior for such patterns exactly
// (before this translation existed, every pattern -- \v-prefixed or not -- was sent to
// PCRE2 unmodified, so \v users were already getting essentially-correct behavior).
//

#ifndef NED_EDITOR_VIM_VIMMAGIC_H
#define NED_EDITOR_VIM_VIMMAGIC_H

#include <string>
#include <string_view>

namespace ned::editor::vim {

[[nodiscard]] std::string TranslateVimMagicPattern(std::string_view pattern);

// vim-magic-translation follow-up, the replacement-text half: vim's own `:s` replacement
// syntax (`\1`-`\9` backreferences, bare `&` for the whole match, `\&` for a literal
// ampersand) doesn't match RegexPattern::FormatReplacement's ECMAScript-flavored
// template language (`$1`-`$99`, `$&`, a literal `$` needing `$$`) any more than the
// pattern side matches PCRE2's own syntax -- same shape of problem, a separate function
// since the translation rules don't overlap with TranslateVimMagicPattern's at all.
// Applied once, at the same point (VimEngine::ExecuteSubstitute, right after
// ParseSubstituteArgs) the pattern half is translated.
//
// Handled: \0-\9 -> $0-$9; bare & -> $&; \& -> &; \\ -> \ (one literal backslash); a
// bare $ -> $$ (escaped to stay literal, since $ is FormatReplacement's own special
// character, not vim's). Deliberately NOT handled (documented v1 cuts, passed through
// byte-for-byte): \r (insert a literal newline, splitting the line); \u/\l/\U/\L/\e/\E
// (case-conversion operators -- would need a replacement templating engine of this
// translator's own, since FormatReplacement is a sealed black box with no hook to
// case-convert an expanded group's text); ~ (the previous substitute's replacement
// text).
[[nodiscard]] std::string TranslateVimMagicReplacement(std::string_view replacement);

} // namespace ned::editor::vim

#endif // NED_EDITOR_VIM_VIMMAGIC_H
