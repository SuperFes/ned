//
// Translates TermOx/esc key events into our KeyChord vocabulary.
//

#ifndef NED_UI_KEYTRANSLATION_H
#define NED_UI_KEYTRANSLATION_H

#include <esc/key.hpp>
#include <optional>

#include "Editor/Key.h"

namespace ned::ui {

// Returns std::nullopt for keys we don't map to anything (e.g. Insert,
// raw-mode-only modifier keys) -- callers should just ignore those rather
// than feeding a meaningless all-default KeyChord into the Dispatcher.
//
// Note: Alt/Meta is NOT detected here. In this terminal library's portable
// (non-raw) key mode, Alt+<key> and plain <key> arrive as the identical
// event -- there is no reliable way to distinguish them at this layer (raw
// mode exists but is a Linux-virtual-console-only feature, not usable inside
// a normal terminal emulator). The supported Meta input path is the classic
// terminal fallback: press Escape, then the key, as two separate keystrokes.
// Key::Escape translates to SpecialKey::Escape like any other key; it's
// Dispatcher's existing prefix-sequence handling that turns a fed "ESC x"
// sequence into a bindable command, with no special-casing needed here.
[[nodiscard]] std::optional<editor::KeyChord> TranslateKey(esc::Key key);

} // namespace ned::ui

#endif // NED_UI_KEYTRANSLATION_H
