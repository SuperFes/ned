#include "Dispatcher.h"

#include <utility>

namespace ned::editor {

Dispatcher::Dispatcher(const CommandRegistry& registry, KeymapStack keymaps) : registry_(registry), keymaps_(std::move(keymaps)) {
}

Dispatcher::Outcome Dispatcher::Feed(const KeyChord& chord, CommandContext& context) {
    pending_.push_back(chord);
    context.triggeringKey = chord;

    const auto lookup = keymaps_.Resolve(pending_);

    switch (lookup.result) {
        case Keymap::LookupResult::Match: {
            // Keyboard-macro recording: batch-append the whole consumed
            // sequence (not one chord at a time as Prefix results arrive) --
            // a multi-chord binding like C-x C-s must be replayed as both
            // chords together, since feeding only the last one alone with no
            // pending_ state at replay time would resolve completely
            // differently. Gated on recording_'s state as of the start of
            // this call: if this Match is what starts recording
            // (kmacro-start-macro), recording_ is still false here (the real
            // StartRecording() call happens later, from BufferView, after
            // Feed has already returned for this keypress -- see
            // DiscardMostRecentlyRecordedChords's own comment), so its own
            // triggering chord(s) are naturally never appended. The
            // symmetric stop-side exclusion is the caller's job, via
            // DiscardMostRecentlyRecordedChords -- Feed itself has no way to
            // know, at this point, whether the command it's about to invoke
            // will end up being treated as "the one that ends recording".
            if (recording_) {
                currentMacro_.insert(currentMacro_.end(), pending_.begin(), pending_.end());
                lastRecordedChordCount_ = pending_.size();
            }
            pending_.clear();
            registry_.Invoke(lookup.commandName, context);
            return Outcome::Invoked;
        }
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

const CommandRegistry& Dispatcher::Registry() const {
    return registry_;
}

void Dispatcher::StartRecording() {
    recording_ = true;
    currentMacro_.clear();
}

void Dispatcher::StopRecording() {
    if (!recording_) {
        return;
    }
    recording_ = false;
    lastMacro_ = std::move(currentMacro_);
    currentMacro_.clear();
}

bool Dispatcher::IsRecording() const {
    return recording_;
}

const std::vector<KeyChord>& Dispatcher::LastMacro() const {
    return lastMacro_;
}

void Dispatcher::DiscardMostRecentlyRecordedChords() {
    if (lastRecordedChordCount_ <= currentMacro_.size()) {
        currentMacro_.resize(currentMacro_.size() - lastRecordedChordCount_);
    }
    lastRecordedChordCount_ = 0;
}

} // namespace ned::editor
