#include "PanelMode.h"

#include <algorithm>
#include <array>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Key.h"

namespace ned::editor {

namespace {

    constexpr std::array kRemotePanelCommands{
        PanelCommand{"remote-panel-open-root", "o", "Add a host's directory (host:/path, or a host for its home) as a root."},
        PanelCommand{"remote-panel-toggle-dotfiles", ".", "Show or hide entries whose names start with a dot."},
        PanelCommand{"remote-panel-refresh", "g", "List the selected directory again."},
        PanelCommand{"remote-panel-disconnect", "d", "Disconnect from the selected row's host."},
        PanelCommand{"remote-panel-remove-root", "x", "Remove the selected root from the panel; nothing on the host changes."},
        PanelCommand{"remote-panel-new-file", "n", "Create a file in the selected directory, or beside the selected file."},
        PanelCommand{"remote-panel-new-directory", "+", "Create a directory in the selected directory, or beside the selected file."},
        PanelCommand{"remote-panel-rename", "R", "Rename or move the selected file or directory on its host."},
        PanelCommand{"remote-panel-delete", "D", "Delete the selected file or directory from its host, after confirming."},
        PanelCommand{"remote-panel-reveal-in-terminal", "t", "Open a terminal on the selected row's host, in its directory or a file's own."},
    };

    constexpr std::array kFilesPanelCommands{
        PanelCommand{"files-panel-toggle-dotfiles", ".", "Show or hide directories whose names start with a dot."},
        PanelCommand{"files-panel-refresh", "g", "Read the project tree from disk again."},
        PanelCommand{"files-panel-new-file", "n", "Create a file in the selected directory, or beside the selected file."},
        PanelCommand{"files-panel-new-directory", "+", "Create a directory in the selected directory, or beside the selected file."},
        PanelCommand{"files-panel-rename", "R", "Rename or move the selected file or directory."},
        PanelCommand{"files-panel-delete", "D", "Delete the selected file or directory, after confirming."},
        PanelCommand{"files-panel-reveal-in-terminal", "t", "Open a terminal in the selected directory, or in a file's own."},
    };

    constexpr std::array kVcsPanelCommands{
        PanelCommand{"vcs-panel-mark", "SPC", "Mark or unmark the selected file, for staging or unstaging several at once."},
        PanelCommand{"vcs-panel-stage", "a", "Stage the marked files, or the selected one."},
        PanelCommand{"vcs-panel-unstage", "u", "Unstage the marked files, or the selected one."},
        PanelCommand{"vcs-panel-discard", "x", "Discard the selected file's changes, after confirming."},
        PanelCommand{"vcs-panel-stash", "z", "Stash the whole working tree."},
        PanelCommand{"vcs-panel-drop-stash", "d", "Drop the selected stash entry."},
        PanelCommand{"vcs-panel-fetch", "f", "Fetch from the remote."},
        PanelCommand{"vcs-panel-pull", "F", "Pull from the remote."},
        PanelCommand{"vcs-panel-push", "P", "Push to the remote."},
        PanelCommand{"vcs-panel-commit-menu", "c", "Open the commit menu: commit, amend, extend or reword."},
        PanelCommand{"vcs-panel-amend", "C", "Amend the previous commit, starting from its message."},
        PanelCommand{"vcs-panel-extend", "e", "Fold the staged changes into the previous commit, keeping its message."},
        PanelCommand{"vcs-panel-reword", "r", "Reword the previous commit's message, leaving its tree as it is."},
        PanelCommand{"vcs-panel-switch-branch", "w", "Switch to another branch."},
        PanelCommand{"vcs-panel-new-branch", "n", "Create a branch and switch to it."},
    };

    constexpr std::array kDebugPanelCommands{
        PanelCommand{"debug-panel-toggle-enabled", "SPC", "Enable or disable the selected breakpoint."},
        PanelCommand{"debug-panel-remove", "d", "Remove the selected breakpoint or watch."},
        PanelCommand{"debug-panel-edit", "c", "Edit the selected row: a breakpoint's condition, a watch's expression or a variable's value."},
        PanelCommand{"debug-panel-set-hit-condition", "h", "Set the selected breakpoint's hit count condition."},
        PanelCommand{"debug-panel-set-log-message", "l", "Turn the selected breakpoint into a logpoint, or edit its message."},
        PanelCommand{"debug-panel-add-function-breakpoint", "a", "Add a breakpoint on a function, by name."},
        PanelCommand{"debug-panel-watch", "w", "Watch the selected variable by name."},
        PanelCommand{"debug-panel-set-value", "=", "Assign a new value to the selected watch or variable."},
        PanelCommand{"debug-panel-clear-section", "X", "Clear every entry in the selected row's section."},
        PanelCommand{"debug-panel-refresh", "g", "Ask the debug adapter for the session's state again."},
    };

    // Rail order, then Remote.
    constexpr std::array kPanelModes{
        PanelMode{.name = "files-panel-mode", .title = "Files panel", .commands = kFilesPanelCommands},
        PanelMode{.name = "vcs-panel-mode", .title = "VCS panel", .commands = kVcsPanelCommands},
        PanelMode{.name = "debug-panel-mode", .title = "Debug panel", .commands = kDebugPanelCommands},
        PanelMode{.name = "remote-panel-mode", .title = "Remote panel", .commands = kRemotePanelCommands},
    };

    struct UserBinding {
        std::vector<KeyChord> keys;
        std::string           command; // empty unbinds
    };

    std::mutex& BindingsMutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::map<std::string, std::vector<UserBinding>, std::less<>>& BindingsStorage() {
        static std::map<std::string, std::vector<UserBinding>, std::less<>> bindings;
        return bindings;
    }

} // namespace

const PanelMode& FilesPanelMode() {
    return kPanelModes[0];
}

const PanelMode& VcsPanelMode() {
    return kPanelModes[1];
}

const PanelMode& DebugPanelMode() {
    return kPanelModes[2];
}

const PanelMode& RemotePanelMode() {
    return kPanelModes[3];
}

std::span<const PanelMode> PanelModes() {
    return kPanelModes;
}

const PanelMode* FindPanelMode(std::string_view name) {
    const auto it = std::ranges::find(kPanelModes, name, &PanelMode::name);
    return it == kPanelModes.end() ? nullptr : &*it;
}

const PanelCommand* FindPanelCommand(const PanelMode& mode, std::string_view name) {
    const auto it = std::ranges::find(mode.commands, name, &PanelCommand::name);
    return it == mode.commands.end() ? nullptr : &*it;
}

Keymap PanelKeymap(const PanelMode& mode) {
    Keymap keymap;
    for (const PanelCommand& command : mode.commands) {
        if (!command.keys.empty()) {
            keymap.Bind(ParseKeySequence(command.keys), std::string(command.name));
        }
    }
    const std::lock_guard<std::mutex> lock(BindingsMutex());
    const auto                        it = BindingsStorage().find(mode.name);
    if (it == BindingsStorage().end()) {
        return keymap;
    }
    for (const UserBinding& binding : it->second) {
        if (binding.command.empty()) {
            keymap.Unbind(binding.keys);
        }
        else {
            keymap.Bind(binding.keys, binding.command);
        }
    }
    return keymap;
}

void DefinePanelKey(std::string_view mode, std::string_view keys, std::string_view command) {
    const PanelMode* panelMode = FindPanelMode(mode);
    if (panelMode == nullptr) {
        throw std::invalid_argument("unknown panel mode: " + std::string(mode));
    }
    if (!command.empty() && FindPanelCommand(*panelMode, command) == nullptr) {
        throw std::invalid_argument(std::string(command) + " isn't a " + std::string(mode) + " command");
    }
    std::vector<KeyChord> sequence = ParseKeySequence(keys);
    if (sequence.empty()) {
        throw std::invalid_argument("no keys to bind");
    }
    const std::lock_guard<std::mutex> lock(BindingsMutex());
    BindingsStorage()[std::string(mode)].push_back(UserBinding{.keys = std::move(sequence), .command = std::string(command)});
}

void ResetPanelKeysForTesting() {
    const std::lock_guard<std::mutex> lock(BindingsMutex());
    BindingsStorage().clear();
}

} // namespace ned::editor
