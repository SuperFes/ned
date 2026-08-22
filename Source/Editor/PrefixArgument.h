//
// Emacs universal-argument (C-u), simplified: once digits or a leading "-"
// have been seen, a further bare C-u chord terminates reading rather than
// continuing it (real Emacs treats that as invalid input too in practice --
// the multiplier-doubling behavior only applies to a *run* of bare C-u
// presses before any digit). A documented simplification, same class of cut
// as KeymapStack's own non-Emacs layering policy.
//
// Pure and buffer-free, mirroring IncrementalSearch's shape: no outcome enum
// on construction (the caller only ever constructs this right after seeing
// the first C-u), but HandleKey does need to report whether it consumed the
// chord, since the terminating key is not part of the prefix-argument syntax
// and must be re-dispatched normally by the caller with the resolved value
// applied.
//

#ifndef NED_EDITOR_PREFIXARGUMENT_H
#define NED_EDITOR_PREFIXARGUMENT_H

#include <string>

#include "Key.h"

namespace ned::editor {

class PrefixArgumentReader {
  public:
    PrefixArgumentReader(); // state right after the first C-u

    enum class Outcome {
        Continue,  // chord consumed, still reading
        Terminate, // chord not part of prefix-argument syntax; caller re-dispatches it
    };

    [[nodiscard]] Outcome HandleKey(const KeyChord& chord);

    // Resolved value: explicit digits (signed, if a leading "-" was seen) if
    // any were typed; else 4 raised to the number of consecutive bare C-u
    // presses seen (4, 16, 64, ...); else -1 for a bare "C-u -" with no
    // following digits.
    [[nodiscard]] long Value() const;

    // "C-u 4-" / "C-u 42-" / "C-u -" -- matches the "C-x-" while-waiting
    // convention BufferView already uses for a pending key sequence.
    [[nodiscard]] std::string StatusText() const;

  private:
    int  rawCuCount_   = 1;
    bool hasDigits_    = false;
    long numericValue_ = 0;
    bool negative_     = false;
};

} // namespace ned::editor

#endif // NED_EDITOR_PREFIXARGUMENT_H
