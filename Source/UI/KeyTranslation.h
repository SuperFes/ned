//
// Translates FTXUI key events into our KeyChord vocabulary.
//

#ifndef NED_UI_KEYTRANSLATION_H
#define NED_UI_KEYTRANSLATION_H

#include <optional>

#include <ftxui/component/event.hpp>

#include "Editor/Key.h"

namespace ned::ui {

// Returns std::nullopt for events we don't map to anything (mouse events,
// unrecognized/malformed sequences) -- callers should just ignore those
// rather than feeding a meaningless all-default KeyChord into the
// Dispatcher.
//
// A real upgrade over the old TermOx/escape-backed translator (TermOx ->
// FTXUI migration): Alt/Meta is now reliably detected as a single keypress,
// not only via the classic two-separate-keystroke Escape-then-key fallback.
// Confirmed empirically, not assumed, against FTXUI's own raw terminal
// input parser during the migration's pre-work spike: a fast Alt+<key>
// press and a slow, genuinely separate Escape keystroke followed later by
// an unrelated key arrive as distinguishable Events (the former as one
// Event whose input() is ESC followed by the key's own bytes; the latter as
// two independent Events). The old Escape-then-key fallback still works
// too -- each keystroke just translates independently as always -- nothing
// was removed, only strictly gained. See this function's own Meta-handling
// branch for how both paths collapse onto the same decoding logic.
[[nodiscard]] std::optional<editor::KeyChord> TranslateKey(const ftxui::Event& event);

} // namespace ned::ui

#endif // NED_UI_KEYTRANSLATION_H
