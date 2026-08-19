//
// Translates decoded Notcurses input events into our KeyChord vocabulary.
//

#ifndef NED_UI_KEYTRANSLATION_H
#define NED_UI_KEYTRANSLATION_H

#include <optional>

#include "Editor/Key.h"
#include "UI/Widget.h"

namespace ned::ui {

// Returns std::nullopt for events we don't map to anything (mouse events,
// pure modifier-key-by-itself presses, signals/resize) -- callers should
// just ignore those rather than feeding a meaningless all-default KeyChord
// into the Dispatcher.
//
// A real simplification over the FTXUI-era translator (FTXUI -> Notcurses
// migration), not just a port: FTXUI handed over raw, undecoded terminal
// bytes, so that translator had to hand-decode UTF-8 and detect Alt/Meta
// itself via an Escape-byte-prefix heuristic (see that file's old header
// comment, since removed). Notcurses decodes all of this itself before
// this function ever sees it: ncinput::id is already either a real Unicode
// codepoint or a synthesized NCKEY_* constant for a special/arrow/function
// key, and Alt/Meta is a real modifier bit (NCKEY_MOD_ALT) rather than
// something to infer from byte-timing -- no prefix heuristic needed at all,
// and no longer any distinction between "fast Alt+key" and "slow
// Escape-then-key" to worry about (Notcurses' own terminal-input parser is
// what resolves that ambiguity, upstream of this function).
[[nodiscard]] std::optional<editor::KeyChord> TranslateKey(const Event& event);

} // namespace ned::ui

#endif // NED_UI_KEYTRANSLATION_H
