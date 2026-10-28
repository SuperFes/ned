//
// A dock panel's mini-mode: the commands a panel answers while it holds
// focus, each with its docstring and default keys, and the keymap over them.
// Like a minor mode it is one more Keymap layer, but its commands act on the
// panel's selection rather than on a buffer, so they are named here rather
// than registered in the CommandRegistry, whose commands all run against one.
//
// init.janet rebinds them with ned/define-panel-key; describe-bindings and
// Docs/Commands.md list each mode's live and default bindings.
//

#ifndef NED_EDITOR_PANELMODE_H
#define NED_EDITOR_PANELMODE_H

#include <span>
#include <string_view>

#include "Keymap.h"

namespace ned::editor {

struct PanelCommand {
    std::string_view name;
    std::string_view keys; // ParseKeySequence notation; empty for unbound
    std::string_view docstring;
};

struct PanelMode {
    std::string_view              name;  // "remote-panel-mode"
    std::string_view              title; // "Remote panel"
    std::span<const PanelCommand> commands;
};

[[nodiscard]] const PanelMode&           FilesPanelMode();
[[nodiscard]] const PanelMode&           VcsPanelMode();
[[nodiscard]] const PanelMode&           DebugPanelMode();
[[nodiscard]] const PanelMode&           RemotePanelMode();
[[nodiscard]] std::span<const PanelMode> PanelModes();
[[nodiscard]] const PanelMode*           FindPanelMode(std::string_view name);
[[nodiscard]] const PanelCommand*        FindPanelCommand(const PanelMode& mode, std::string_view name);

// The mode's default keys with init.janet's bindings applied over them in
// the order they were made.
[[nodiscard]] Keymap PanelKeymap(const PanelMode& mode);

// Binds keys in a panel mode; an empty command unbinds them. Throws
// std::invalid_argument for an unknown mode or command, or bad keys.
void DefinePanelKey(std::string_view mode, std::string_view keys, std::string_view command);

void ResetPanelKeysForTesting();

} // namespace ned::editor

#endif // NED_EDITOR_PANELMODE_H
