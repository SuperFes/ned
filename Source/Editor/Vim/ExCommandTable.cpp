#include "ExCommandTable.h"

#include <algorithm>

namespace ned::editor::vim {

namespace {

    constexpr std::array kExCommands{
        ExCommandInfo{ExCommandId::Substitute, "substitute", {"s"}, "Replace a pattern in the range"},
        ExCommandInfo{ExCommandId::Write, "write", {"w"}, "Save the buffer"},
        ExCommandInfo{ExCommandId::Quit, "quit", {"q"}, "Close the window, quitting on the last one"},
        ExCommandInfo{ExCommandId::Close, "close", {"clo"}, "Close the window"},
        ExCommandInfo{ExCommandId::Split, "split", {"sp"}, "Split the window horizontally"},
        ExCommandInfo{ExCommandId::VSplit, "vsplit", {"vs", "vsp"}, "Split the window vertically"},
        ExCommandInfo{ExCommandId::Only, "only", {"on"}, "Close every other window"},
        ExCommandInfo{ExCommandId::WriteQuit, "wq", {}, "Save, then close the window"},
        ExCommandInfo{ExCommandId::Xit, "xit", {"x"}, "Save, then close the window"},
        ExCommandInfo{ExCommandId::QuitAll, "qall", {"qa", "quitall"}, "Quit the editor"},
        ExCommandInfo{ExCommandId::Delete, "delete", {"d"}, "Delete the lines in the range"},
        ExCommandInfo{ExCommandId::Join, "join", {"j"}, "Join the lines in the range"},
        ExCommandInfo{ExCommandId::Yank, "yank", {"y"}, "Yank the lines in the range"},
        ExCommandInfo{ExCommandId::Put, "put", {"pu"}, "Put a register below the line"},
        ExCommandInfo{ExCommandId::ShiftRight, ">", {}, "Indent the lines in the range"},
        ExCommandInfo{ExCommandId::ShiftLeft, "<", {}, "Outdent the lines in the range"},
        ExCommandInfo{ExCommandId::Move, "move", {"m"}, "Move the lines below an address"},
        ExCommandInfo{ExCommandId::Copy, "copy", {"co", "t"}, "Copy the lines below an address"},
        ExCommandInfo{ExCommandId::Sort, "sort", {}, "Sort the lines in the range"},
        ExCommandInfo{ExCommandId::Read, "read", {"r"}, "Insert a file below the line"},
        ExCommandInfo{ExCommandId::Global, "global", {"g"}, "Run a command on matching lines"},
        ExCommandInfo{ExCommandId::Normal, "normal", {"norm"}, "Run Normal-mode keys"},
    };

} // namespace

std::span<const ExCommandInfo> ExCommands() {
    return kExCommands;
}

const ExCommandInfo* LookupExCommand(std::string_view name) {
    if (name.empty()) {
        return nullptr;
    }
    const auto it = std::ranges::find_if(kExCommands, [name](const ExCommandInfo& info) {
        return info.name == name || std::ranges::find(info.aliases, name) != info.aliases.end();
    });
    return it == kExCommands.end() ? nullptr : &*it;
}

} // namespace ned::editor::vim
