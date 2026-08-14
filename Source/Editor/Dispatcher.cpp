#include "Dispatcher.h"

#include <utility>

namespace ned::editor {

Dispatcher::Dispatcher(const CommandRegistry& registry, KeymapStack keymaps)
    : registry_(registry), keymaps_(std::move(keymaps)) {}

Dispatcher::Outcome Dispatcher::Feed(const KeyChord& chord, CommandContext& context) {
    pending_.push_back(chord);
    context.triggeringKey = chord;

    const auto lookup = keymaps_.Resolve(pending_);

    switch (lookup.result) {
        case Keymap::LookupResult::Match:
            pending_.clear();
            registry_.Invoke(lookup.commandName, context);
            return Outcome::Invoked;
        case Keymap::LookupResult::Prefix:
            return Outcome::Pending;
        case Keymap::LookupResult::NoMatch:
            pending_.clear();
            return Outcome::Unbound;
    }

    pending_.clear();
    return Outcome::Unbound; // unreachable; silences -Wreturn-type
}

void Dispatcher::Reset() {
    pending_.clear();
}

const std::vector<KeyChord>& Dispatcher::Pending() const {
    return pending_;
}

} // namespace ned::editor
