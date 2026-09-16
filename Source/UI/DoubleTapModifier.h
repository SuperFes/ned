//
// search-everywhere follow-up: detects two Shift presses (either side)
// close enough together, with nothing else in between, to count as one
// gesture -- BufferView's own second entry point onto search-everywhere,
// beside the ordinary M-s keybinding. Pure and buffer-free, fed one raw
// event at a time by the caller -- Editor/PrefixArgument.h's own shape,
// just over ncinput instead of KeyChord (KeyTranslation.cpp already drops a
// bare modifier press before it ever becomes a KeyChord, so this has to
// see the raw event, earlier in the pipeline).
//

#ifndef NED_UI_DOUBLETAPMODIFIER_H
#define NED_UI_DOUBLETAPMODIFIER_H

#include <chrono>
#include <optional>

#include <notcurses/notcurses.h>

namespace ned::ui {

class DoubleTapModifierDetector {
  public:
    // `window` mirrors BufferView/Internal.h's own kDoubleClickWindow
    // (400ms) -- the mouse double-click precedent this gesture is
    // conceptually the keyboard sibling of.
    explicit DoubleTapModifierDetector(std::chrono::milliseconds window = std::chrono::milliseconds(400));

    // Returns true exactly on the event that completes a double-tap: a bare
    // Shift press (NCKEY_LSHIFT or NCKEY_RSHIFT specifically -- NOT
    // KeyTranslation.cpp's own IsBareModifierKey range, which spans all 14
    // modifier keys and would let Shift-then-Ctrl count as a "double tap")
    // arriving within `window` of a previous one, with nothing else at all
    // in between. A Shift key's own NCTYPE_REPEAT/NCTYPE_RELEASE are inert
    // -- neither resets nor advances the pending window -- so a long Shift
    // hold's repeat stream can't cascade into repeated fires. Anything else
    // (a real key, a different modifier's own bare press) resets.
    [[nodiscard]] bool Feed(const ncinput& input);

    // A mouse action also counts as "something else happened" -- BufferView
    // calls this from OnMouseEvent, which never routes through Feed.
    void Reset();

  private:
    std::chrono::milliseconds                            window_;
    std::optional<std::chrono::steady_clock::time_point> pendingSince_;
};

} // namespace ned::ui

#endif // NED_UI_DOUBLETAPMODIFIER_H
