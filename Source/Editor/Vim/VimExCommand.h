//
// Ex command-line parsing (":w", ":42", ":%s/foo/bar/g", ":'<,'>d", ...) -- pure parsing
// only, VimEngine.h owns execution (mapping a parsed ExCommand onto Buffer::Save/
// DeleteRange/etc., or an outbound PendingIntent for anything beyond single-buffer
// scope). Line numbers here are 0-based (this codebase's own convention throughout,
// matching Rope::ByteOffsetToLine), even though vim's own :42 syntax is 1-based --
// ParseExCommand does that conversion internally.
//
// Deliberate v1 scope: no :g/pattern/cmd global command, no mark-letter addresses beyond
// '< / '> (the visual-selection marks, supplied by the caller), no address arithmetic
// (":.+3"). :normal's own key-notation parsing (VimEngine's concern, not this file's) is
// plain-printable-characters-only -- no <Esc>/<C-x> notation.
//

#ifndef NED_EDITOR_VIM_VIMEXCOMMAND_H
#define NED_EDITOR_VIM_VIMEXCOMMAND_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ned::editor::vim {

struct ExRange {
    bool        present = false; // false: no range was typed -- command applies to the current line only
    std::size_t startLine;       // 0-based, inclusive
    std::size_t endLine;         // 0-based, inclusive
};

struct ExCommand {
    ExRange     range;
    std::string name; // e.g. "w", "wq", "q", "x", "d", "s", "normal", "" (a bare range: "goto this line")
    bool        bang = false;
    std::string rest; // raw remainder after name/bang, unprocessed (e.g. "/foo/bar/g" for :s, or the key sequence for :normal)
};

struct ExSubstituteArgs {
    std::string pattern;
    std::string replacement;
    std::string flags;
};

// currentLine/lastLine and visualRange (the '< / '> marks, if any) are 0-based, resolved
// by the caller (VimEngine) from its own live state -- this function is otherwise pure
// text parsing. Returns nullopt for text that isn't a well-formed ex command at all
// (blank input, an unparseable range).
[[nodiscard]] std::optional<ExCommand> ParseExCommand(std::string_view text, std::size_t currentLine, std::size_t lastLine,
                                                      std::optional<ExRange> visualRange);

// Splits ":s<delim>pattern<delim>replacement<delim>flags" (rest already has the leading
// "s"/"substitute" stripped, delim is the first character of rest, e.g. '/'). A trailing
// delimiter and flags are both optional ("/foo/bar" and "/foo/bar/" are equivalent).
// nullopt if rest doesn't start with a punctuation delimiter at all.
[[nodiscard]] std::optional<ExSubstituteArgs> ParseSubstituteArgs(std::string_view rest);

} // namespace ned::editor::vim

#endif // NED_EDITOR_VIM_VIMEXCOMMAND_H
