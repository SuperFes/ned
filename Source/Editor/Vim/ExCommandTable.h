//
// The ex commands Engine understands, as data: one entry per command with its canonical
// name, the shorter spellings it also answers to, and a one-line description. Engine
// resolves a parsed ExCommand::name through LookupExCommand and switches on the id;
// the command-line completion popup lists the same table, so a command is either in
// both or in neither.
//
// Aliases are the exact spellings accepted -- vim's own "any unambiguous prefix down to
// a minimum" abbreviation rule is not implemented, so ":wri" is not ":write".
//

#ifndef NED_EDITOR_VIM_EXCOMMANDTABLE_H
#define NED_EDITOR_VIM_EXCOMMANDTABLE_H

#include <array>
#include <span>
#include <string_view>

namespace ned::editor::vim {

enum class ExCommandId { Substitute,
                         Write,
                         Quit,
                         Close,
                         Split,
                         VSplit,
                         Only,
                         WriteQuit,
                         Xit,
                         QuitAll,
                         Delete,
                         Join,
                         Yank,
                         Put,
                         ShiftRight,
                         ShiftLeft,
                         Move,
                         Copy,
                         Sort,
                         Read,
                         Global,
                         Normal };

struct ExCommandInfo {
    ExCommandId                     id;
    std::string_view                name;    // canonical spelling, what completion inserts
    std::array<std::string_view, 2> aliases; // unused slots are empty
    std::string_view                doc;
};

[[nodiscard]] std::span<const ExCommandInfo> ExCommands();

// Exact match against a canonical name or an alias; nullptr for anything else.
[[nodiscard]] const ExCommandInfo* LookupExCommand(std::string_view name);

} // namespace ned::editor::vim

#endif // NED_EDITOR_VIM_EXCOMMANDTABLE_H
