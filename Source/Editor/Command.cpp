#include "Command.h"

#include <algorithm>
#include <stdexcept>

namespace ned::editor {

Command::Command(std::string name, std::string docstring, CommandFunction function)
    : Name_(std::move(name)), Docstring_(std::move(docstring)), Function_(std::move(function)) {}

const std::string& Command::Name() const {
    return Name_;
}

const std::string& Command::Docstring() const {
    return Docstring_;
}

void Command::Invoke(CommandContext& context) const {
    Function_(context);
}

void CommandRegistry::Register(std::string name, std::string docstring, CommandFunction function) {
    Command command(name, std::move(docstring), std::move(function)); // name copied here
    commands_.insert_or_assign(std::move(name), std::move(command));  // original moved in as key
}

const Command* CommandRegistry::Find(const std::string& name) const {
    const auto it = commands_.find(name);
    return it == commands_.end() ? nullptr : &it->second;
}

void CommandRegistry::Invoke(const std::string& name, CommandContext& context) const {
    const auto it = commands_.find(name);
    if (it == commands_.end()) {
        throw std::out_of_range("ned: no such command: " + name);
    }
    it->second.Invoke(context);
}

std::vector<std::string> CommandRegistry::Names() const {
    std::vector<std::string> names;
    names.reserve(commands_.size());
    for (const auto& [name, command] : commands_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> CompleteCommandNames(const CommandRegistry& registry, std::string_view prefix) {
    std::vector<std::string> matches;
    for (const auto& name : registry.Names()) {
        if (name.size() >= prefix.size() && std::string_view(name).substr(0, prefix.size()) == prefix) {
            matches.push_back(name);
        }
    }
    return matches; // registry.Names() is already sorted, so this stays sorted
}

} // namespace ned::editor
