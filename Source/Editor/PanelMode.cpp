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

    constexpr std::array kPanelModes{
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

const PanelMode& RemotePanelMode() {
    return kPanelModes[0];
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
