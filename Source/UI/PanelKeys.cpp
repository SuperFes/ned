#include "PanelKeys.h"

#include <utility>

#include "Editor/Keymap.h"
#include "Editor/WhichKeySettings.h"

namespace ned::ui {

PanelKeys::PanelKeys(const editor::PanelMode& mode) : mode_(mode) {
}

std::optional<std::string> PanelKeys::Feed(const editor::KeyChord& chord) {
    pending_.push_back(chord);
    const editor::Keymap         keymap = editor::PanelKeymap(mode_);
    const editor::Keymap::Lookup lookup = keymap.Resolve(pending_);
    switch (lookup.result) {
        case editor::Keymap::LookupResult::Match:
            pending_.clear();
            Hint(std::nullopt);
            return lookup.commandName;
        case editor::Keymap::LookupResult::Prefix: {
            const std::string label = editor::FormatKeySequence(pending_) + "-";
            if (onMessage_) {
                onMessage_(label);
            }
            if (editor::WhichKeyEnabled()) {
                WhichKeyHint hint{.prefixLabel = label, .bindings = {}};
                for (const auto& child : keymap.ChildrenAt(pending_)) {
                    hint.bindings.emplace_back(editor::FormatKeyChord(child.chord), child.commandName.value_or("..."));
                }
                Hint(std::move(hint));
            }
            return std::nullopt;
        }
        case editor::Keymap::LookupResult::NoMatch:
            break;
    }
    const std::string sequence = editor::FormatKeySequence(pending_);
    pending_.clear();
    Hint(std::nullopt);
    if (onMessage_) {
        onMessage_(sequence + " is undefined in " + std::string(mode_.title));
    }
    return std::nullopt;
}

void PanelKeys::Reset() {
    pending_.clear();
    Hint(std::nullopt);
}

void PanelKeys::SetOnMessage(std::function<void(std::string)> handler) {
    onMessage_ = std::move(handler);
}

void PanelKeys::SetOnHint(std::function<void(std::optional<WhichKeyHint>)> handler) {
    onHint_ = std::move(handler);
}

void PanelKeys::Hint(std::optional<WhichKeyHint> hint) {
    if (!hint && !hinting_) {
        return;
    }
    hinting_ = hint.has_value();
    if (onHint_) {
        onHint_(std::move(hint));
    }
}

} // namespace ned::ui
