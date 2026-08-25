//
// which-key follow-up: the pure data WhichKeyPopup renders, computed by
// BufferView from Dispatcher::Pending()/Keymaps() and handed up to
// main.cpp's shared OverlayHost via BufferView::SetOnPrefixHintChanged --
// kept in its own tiny header (no Editor/ dependency) so main.cpp can wire
// BufferView to WhichKeyPopup without either one needing to know about the
// other's own header.
//

#ifndef NED_UI_WHICHKEYHINT_H
#define NED_UI_WHICHKEYHINT_H

#include <string>
#include <utility>
#include <vector>

namespace ned::ui {

struct WhichKeyHint {
    std::string prefixLabel; // e.g. "C-x-", matches the echo area's own pending-prefix convention

    // One entry per possible next chord, formatted via FormatKeyChord;
    // second element is the bound command name, or "..." for a chord that's
    // itself a deeper, still-unbound prefix.
    std::vector<std::pair<std::string, std::string>> bindings;
};

} // namespace ned::ui

#endif // NED_UI_WHICHKEYHINT_H
