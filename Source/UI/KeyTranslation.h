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
// Notcurses decodes raw terminal bytes itself before this function ever
// sees them: ncinput::id is already either a real Unicode codepoint or a
// synthesized NCKEY_* constant for a special/arrow/function key, and
// Alt/Meta is a real modifier bit (NCKEY_MOD_ALT) rather than something to
// infer from byte-timing -- no UTF-8 decoding or Escape-byte-prefix
// heuristic needed here, and no distinction between "fast Alt+key" and
// "slow Escape-then-key" to worry about (Notcurses' own terminal-input
// parser resolves that ambiguity upstream of this function).
[[nodiscard]] std::optional<editor::KeyChord> TranslateKey(const Event& event);

} // namespace ned::ui

#endif // NED_UI_KEYTRANSLATION_H
