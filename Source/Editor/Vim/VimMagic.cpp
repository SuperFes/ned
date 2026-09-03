#include "VimMagic.h"

namespace ned::editor::vim {

namespace {

// PCRE2 has no directional word-boundary atoms the way vim's \< (word start) and \>
// (word end) do -- \b matches both edges, a documented approximation (see VimMagic.h).
constexpr std::string_view kWordBoundary = "\\b";

} // namespace

std::string TranslateVimMagicPattern(std::string_view pattern) {
    if (pattern.starts_with("\\v")) {
        return std::string(pattern.substr(2));
    }

    std::string out;
    out.reserve(pattern.size());
    bool        inBracket    = false;
    std::size_t bracketStart = 0; // index of the first byte that could close the bracket

    for (std::size_t i = 0; i < pattern.size();) {
        const char c = pattern[i];

        if (inBracket) {
            out += c;
            // A ']' as the very first character of the class (or right after a leading
            // '^') is a literal member, not the closer -- the same POSIX bracket-
            // expression rule both vim and PCRE2 already follow identically.
            if (c == ']' && i > bracketStart) {
                inBracket = false;
            }
            ++i;
            continue;
        }

        if (c == '[') {
            inBracket = true;
            out += c;
            ++i;
            bracketStart = i;
            if (i < pattern.size() && pattern[i] == '^') {
                out += pattern[i];
                ++i;
                bracketStart = i;
            }
            continue;
        }

        if (c == '\\' && i + 1 < pattern.size()) {
            const char next = pattern[i + 1];
            switch (next) {
                case '(':
                case ')':
                case '|':
                case '+':
                case '?':
                    out += next;
                    i += 2;
                    continue;
                case '=': // vim's own \? synonym
                    out += '?';
                    i += 2;
                    continue;
                case '<':
                case '>':
                    out += kWordBoundary;
                    i += 2;
                    continue;
                case 'z':
                    if (i + 2 < pattern.size() && pattern[i + 2] == 's') {
                        out += "\\K"; // \zs -- reset the reported match start
                        i += 3;
                        continue;
                    }
                    // \ze (reset the match *end*) has no clean PCRE2 equivalent --
                    // passed through verbatim, a documented cut (VimMagic.h).
                    out += c;
                    out += next;
                    i += 2;
                    continue;
                case '%':
                    if (i + 2 < pattern.size() && pattern[i + 2] == '(') {
                        out += "(?:"; // \%( -- non-capturing group
                        i += 3;
                        continue;
                    }
                    out += c;
                    out += next;
                    i += 2;
                    continue;
                case '{': {
                    // \{...} (or \{-...}, vim's own non-greedy marker) -- vim's interval
                    // quantifier. Asymmetric on purpose: only the *opening* brace is
                    // backslash-escaped in vim's own source syntax, the closer is a bare
                    // '}' (confirmed against :help /\{ -- not the \{...\} shape it would
                    // be reasonable to guess at). The body between them (digits/comma)
                    // already means the same thing in PCRE2's own {n,m} syntax, copied
                    // verbatim; an empty body means "0 or more", i.e. "*", which PCRE2
                    // has no empty-braces spelling for.
                    std::size_t j         = i + 2;
                    bool        nonGreedy = false;
                    if (j < pattern.size() && pattern[j] == '-') {
                        nonGreedy = true;
                        ++j;
                    }
                    std::string body;
                    while (j < pattern.size() && pattern[j] != '}') {
                        body += pattern[j];
                        ++j;
                    }
                    if (body.empty()) {
                        out += '*';
                    }
                    else {
                        out += '{';
                        out += body;
                        out += '}';
                    }
                    if (nonGreedy) {
                        out += '?';
                    }
                    i = (j < pattern.size()) ? j + 1 : j; // skip past the closing bare '}'
                    continue;
                }
                default:
                    out += c;
                    out += next;
                    i += 2;
                    continue;
            }
        }

        if (c == '(' || c == ')' || c == '|' || c == '+' || c == '?' || c == '{' || c == '}') {
            out += '\\';
            out += c;
            ++i;
            continue;
        }

        out += c;
        ++i;
    }
    return out;
}

std::string TranslateVimMagicReplacement(std::string_view replacement) {
    std::string out;
    out.reserve(replacement.size());

    for (std::size_t i = 0; i < replacement.size();) {
        const char c = replacement[i];

        if (c == '\\' && i + 1 < replacement.size()) {
            const char next = replacement[i + 1];
            if (next >= '0' && next <= '9') {
                out += '$';
                out += next;
                i += 2;
                continue;
            }
            if (next == '&') { // literal ampersand
                out += '&';
                i += 2;
                continue;
            }
            if (next == '\\') { // one literal backslash
                out += '\\';
                i += 2;
                continue;
            }
            // \r, \u, \l, \U, \L, \e, \E, ~ and anything else unrecognized: passed
            // through byte-for-byte, a documented cut (VimMagic.h).
            out += c;
            out += next;
            i += 2;
            continue;
        }

        if (c == '&') { // the whole match, unless already handled as \& above
            out += "$&";
            ++i;
            continue;
        }

        if (c == '$') { // FormatReplacement's own special character -- escaped to stay literal
            out += "$$";
            ++i;
            continue;
        }

        out += c;
        ++i;
    }
    return out;
}

} // namespace ned::editor::vim
