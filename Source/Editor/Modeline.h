//
// File-local settings written into the file itself, the way Vim and Emacs
// read them: a `vim: ft=verilog ts=4 sw=4 et` (or `vim: set ... :`)
// modeline in the first or last few lines, or an Emacs
// `-*- mode: verilog; tab-width: 4; indent-tabs-mode: nil -*-` line (or the
// bare `-*- verilog -*-` form) on the first line, second after a `#!`.
//
// Pure: text in, settings out. Only the first and last kModelineLines lines
// are read (Vim's own 'modelines' default).
//

#ifndef NED_EDITOR_MODELINE_H
#define NED_EDITOR_MODELINE_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ned::editor {

inline constexpr std::size_t kModelineLines = 5;

struct Modeline {
    // A ned language name ("verilog", "cpp"), with Vim/Emacs spellings mapped
    // ("c++", "sh", "vlang"); nullopt when the modeline names none.
    std::optional<std::string> language;
    std::optional<bool>        useTabs;
    std::optional<int>         width; // shiftwidth / the mode's basic offset, else tab width
};

[[nodiscard]] Modeline ParseModeline(std::string_view text);

} // namespace ned::editor

#endif // NED_EDITOR_MODELINE_H
