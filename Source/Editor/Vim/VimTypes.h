//
// Shared vocabulary for the Vim emulation layer (Source/Editor/Vim/, ned::editor::vim).
// See ROADMAP.md's "Input model: optional Vim/vi keybinding emulation" for the design
// rationale -- this is a dedicated key-consuming state machine (VimEngine.h), the same
// shape as PrefixArgumentReader/IncrementalSearch, not a new KeymapStack layer: a static
// Keymap trie can't express "operator + count + any motion" composition.
//

#ifndef NED_EDITOR_VIM_VIMTYPES_H
#define NED_EDITOR_VIM_VIMTYPES_H

#include <cstddef>

namespace ned::editor::vim {

// The engine's own top-level mode -- distinct from Buffer's Mode (major-mode syntax/
// keymap), same name collision Org/Snippet's own state machines already accept.
// Operator-pending isn't a member here: it's transient sub-state inside Normal (see
// VimEngine::pendingOperator_), the same way Emacs' own C-u reading isn't a distinct
// InputMode value in BufferView either -- it's not something anything outside the
// engine needs to render differently, beyond the status text VimEngine already reports.
enum class Mode { Normal,
                  Insert,
                  Replace,
                  Visual,
                  VisualLine,
                  VisualBlock,
                  CommandLine };

// A motion's landing point plus how an operator applying to [point, target) (or
// [target, point) if target < point) should treat it. Also the plain-move return shape
// (VimEngine ignores linewise/inclusive when just moving point, not applying an
// operator).
struct MotionResult {
    std::size_t target;
    bool        linewise  = false; // whole-line motions (j/k/gg/G/{/}/...) -- operator acts on complete lines
    bool        inclusive = false; // charwise motions that include their own landing grapheme (f/t/e/$/%/...)
    bool        found     = true;  // false for a failed f/F/t/T (target char not found) -- motion/operator both no-op
};

// A text object's range, always [start, end) regardless of point's position relative to
// it (VimTextObject.h's functions are the only source of these).
struct ObjectRange {
    std::size_t start;
    std::size_t end;
    bool        linewise = false; // ip/ap
    bool        found    = true;  // false when no such object exists at point (e.g. i( outside any parens)
};

} // namespace ned::editor::vim

#endif // NED_EDITOR_VIM_VIMTYPES_H
