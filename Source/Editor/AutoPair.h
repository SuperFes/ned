//
// auto-pair-brackets-and-quotes follow-up: pure decision logic for
// electric-pair-style bracket/quote handling -- "type an opener, get the
// pair; type the redundant closer, skip over it; backspace an empty pair,
// remove both; type an opener over a selection, wrap it" -- deliberately
// stateless (see this feature's own design notes: no per-buffer "was this
// closer auto-inserted by us" tracking, unlike e.g. CodeMirror's
// closeBrackets addon). A structural check against the two neighboring
// graphemes plus (for quotes only) the syntax class at point is enough to
// match what mainstream editors actually ship, without extra state that has
// to stay correct across undo/redo, multi-cursor, and macro replay.
//
// Kept UI-free and Buffer-free on purpose: Commands.cpp's self-insert-
// command/backward-delete-char gather an AutoPairQuery from a real Buffer
// and Mode, but the decision itself is a plain function of that data, the
// same "compute the decision separately from applying it" split
// Rectangle.h/Register.h already establish.
//

#ifndef NED_EDITOR_AUTOPAIR_H
#define NED_EDITOR_AUTOPAIR_H

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "Editor/Mode.h" // SyntaxClass

namespace ned::editor {

// The pair table every TreeSitterModeFromLanguage-built Mode starts from:
// (), [], {}, "", ''. Exposed here rather than only living inline inside
// Mode.cpp's factories, so Commands.cpp and this file's own tests share one
// definition instead of two copies drifting apart. auto-pair-round-2
// follow-up wires this into Mode::autoPairs -- Lisp-family modes (Janet/
// Clojure/Jank) are expected to drop the '' entry there, since a bare quote
// in those languages is the reader's own quote macro, not a paired
// delimiter, and pairing it would fight the language's real syntax.
[[nodiscard]] const std::vector<std::pair<char, char>>& DefaultAutoPairs();

// DefaultAutoPairs() minus the '' entry -- for Lisp-family modes (Janet/
// Clojure/Jank), where a bare quote is the reader's own quote macro (e.g.
// `'(a b c)`), not a paired string delimiter. Double-quoted strings still
// pair normally -- only single-quote pairing would fight the language's own
// syntax. Mode.cpp's JanetMode/ClojureMode/JankMode set Mode::autoPairs to
// this instead of DefaultAutoPairs().
[[nodiscard]] const std::vector<std::pair<char, char>>& LispAutoPairs();

// Process-wide on/off switch, mirroring AutoRevert.h's exact
// SetAutoRevertEnabled/AutoRevertEnabled shape -- default true. Commands.cpp
// treats "disabled" the same as an empty pair table (DecideSelfInsert's own
// "no pairing configured" case), so this needs no plumbing into the pure
// decision functions themselves.
void               SetAutoPairEnabled(bool enabled);
[[nodiscard]] bool AutoPairEnabled();

// Looks up opener's matching close character in pairs, e.g. '(' -> ')'.
// Commands.cpp needs this once DecideSelfInsert has already returned
// InsertPair/WrapSelection -- the action alone doesn't carry which
// character to actually insert as the close.
[[nodiscard]] std::optional<char> ClosingCharFor(char opener, const std::vector<std::pair<char, char>>& pairs);

enum class PairAction {
    InsertPlain,        // ordinary self-insert, no pairing involved
    InsertPair,         // insert typed char + its matching close, point lands between them
    SkipOver,           // move point past the already-present matching close instead of inserting
    WrapSelection,       // insert typed char before the region and its close after, point/mark cleared
    DeleteAdjacentPair, // backward-delete-char should remove both neighbors, not just the one before point
};

// Everything DecideSelfInsert needs, gathered by the caller from Buffer/
// CommandContext -- kept a plain struct rather than threading a Buffer&
// through so this stays a pure function, unit-testable without constructing
// one.
struct AutoPairQuery {
    char typed = '\0';

    // The single grapheme immediately before/after point, or empty at a
    // buffer boundary -- never a whole line, only ever compared against
    // one-ASCII-byte delimiters and a word-character check, so a genuinely
    // multi-byte grapheme here just fails both and falls through to
    // whichever non-word-boundary branch applies.
    std::string_view charBefore;
    std::string_view charAfter;

    bool hasSelection = false;

    // Default when the caller has no syntax info available (no Mode wired,
    // or its highlight function is empty) -- same "absence means unknown,
    // don't suppress" stance the rest of the codebase takes for optional
    // CommandContext facts.
    SyntaxClass classAtPoint = SyntaxClass::Default;

    // Null or empty means no pairing configured at all -- everything
    // self-inserts plain, matching a Mode with no autoPairs entries.
    const std::vector<std::pair<char, char>>* pairs = nullptr;
};

[[nodiscard]] PairAction DecideSelfInsert(const AutoPairQuery& query);

// backward-delete-char's own query: just the two neighboring graphemes plus
// the pair table -- deleting an adjacent pair is a plain structural check,
// no selection/syntax-class involvement (a selection active means
// backward-delete-char isn't even the command in play; BufferView's own
// delete-selection path handles that first).
[[nodiscard]] bool ShouldDeleteAdjacentPair(std::string_view charBefore, std::string_view charAfter,
                                            const std::vector<std::pair<char, char>>& pairs);

} // namespace ned::editor

#endif // NED_EDITOR_AUTOPAIR_H
