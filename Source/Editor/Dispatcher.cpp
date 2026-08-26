#include "Dispatcher.h"

#include <unordered_map>
#include <utility>

namespace ned::editor {

namespace {

    // prefix-argument follow-up: a short, explicit list of direction-symmetric
    // motion commands a negative prefix argument (C-u -, M-- once that entry
    // point exists) flips to the opposite command -- hand-curated, same idiom
    // as LspServerConfig's argv table, not a general mechanism. A command not
    // in this table just runs abs(value) times regardless of sign.
    const std::unordered_map<std::string, std::string>& DirectionPairs() {
        static const std::unordered_map<std::string, std::string> pairs = {
            {"forward-char", "backward-char"},
            {"backward-char", "forward-char"},
            {"next-line", "previous-line"},
            {"previous-line", "next-line"},
            {"forward-word", "backward-word"},
            {"backward-word", "forward-word"},
            {"scroll-page-down", "scroll-page-up"},
            {"scroll-page-up", "scroll-page-down"},
        };
        return pairs;
    }

    // self-insert-fallback follow-up: whether an otherwise-unbound chord
    // should still self-insert -- true for a plain (no Control/Meta,
    // Special == None) printable codepoint. Printable ASCII (0x20-0x7E)
    // never reaches this check at all (BuildDefaultGlobalKeymap gives each
    // one its own real self-insert-command entry, so those Match before
    // Dispatcher::Feed's NoMatch branch is ever reached); this is what
    // makes *every other* printable Unicode codepoint -- accented Latin,
    // CJK, emoji, ... -- self-insert too, without enumerating over a
    // million keymap entries for them. C0 controls (0x00-0x1F), DEL
    // (0x7F), and the C1 control range (0x80-0x9F) are excluded -- real
    // control characters, not text a keystroke should ever insert literally.
    bool IsSelfInsertableFallback(const KeyChord& chord) {
        if (chord.Control || chord.Meta || chord.Special != SpecialKey::None || chord.Codepoint == 0) {
            return false;
        }
        if (chord.Codepoint < 0x20 || chord.Codepoint == 0x7F) {
            return false;
        }
        return chord.Codepoint < 0x80 || chord.Codepoint > 0x9F;
    }

    struct PrefixArgResolution {
        std::string commandName;
        long        repeatCount;
    };

    PrefixArgResolution ResolvePrefixArg(const std::string& commandName, long value) {
        if (value >= 0) {
            return {commandName, value};
        }
        const auto&        pairs         = DirectionPairs();
        const auto         it            = pairs.find(commandName);
        const std::string& effectiveName = it != pairs.end() ? it->second : commandName;
        return {effectiveName, -value};
    }

} // namespace

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
            context.lastCommand = lastInvokedCommand_;

            if (context.prefixArg) {
                const PrefixArgResolution resolution = ResolvePrefixArg(lookup.commandName, *context.prefixArg);
                if (resolution.repeatCount == 1) {
                    registry_.Invoke(resolution.commandName, context);
                }
                else if (resolution.repeatCount > 0) {
                    // One undo step per keystroke, the same rule multi-cursor
                    // edits already follow -- see Buffer::BeginUndoGroup.
                    context.buffer.BeginUndoGroup();
                    try {
                        for (long i = 0; i < resolution.repeatCount; ++i) {
                            registry_.Invoke(resolution.commandName, context);
                        }
                    }
                    catch (...) {
                        context.buffer.EndUndoGroup();
                        throw;
                    }
                    context.buffer.EndUndoGroup();
                }
                // repeatCount == 0 (C-u 0 <cmd>): runs the command zero times.
                context.prefixArg.reset();
            }
            else {
                registry_.Invoke(lookup.commandName, context);
            }

            lastInvokedCommand_ = lookup.commandName;
            return Outcome::Invoked;
        }
        case Keymap::LookupResult::Prefix:
            return Outcome::Pending;
        case Keymap::LookupResult::NoMatch: {
            // self-insert-fallback follow-up: only a *bare* unbound chord
            // (not the tail of an unresolved multi-chord prefix, e.g. an
            // unbound key after "C-c") falls through to self-insert-command
            // -- pending_ still holds the full attempted sequence at this
            // point (Feed pushed chord onto it up top), so size() == 1 means
            // this NoMatch fired on the very first chord, no prefix in
            // progress.
            const bool bareChord = pending_.size() == 1;
            pending_.clear();
            context.prefixArg.reset(); // an unbound key cancels a pending argument, matching Emacs
            if (bareChord && IsSelfInsertableFallback(chord)) {
                if (const Command* selfInsert = registry_.Find("self-insert-command")) {
                    if (recording_) {
                        currentMacro_.push_back(chord);
                        lastRecordedChordCount_ = 1;
                    }
                    context.lastCommand = lastInvokedCommand_;
                    selfInsert->Invoke(context);
                    lastInvokedCommand_ = "self-insert-command";
                    return Outcome::Invoked;
                }
            }
            return Outcome::Unbound;
        }
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

const KeymapStack& Dispatcher::Keymaps() const {
    return keymaps_;
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

const std::string& Dispatcher::LastInvokedCommand() const {
    return lastInvokedCommand_;
}

void Dispatcher::DiscardMostRecentlyRecordedChords() {
    if (lastRecordedChordCount_ <= currentMacro_.size()) {
        currentMacro_.resize(currentMacro_.size() - lastRecordedChordCount_);
    }
    lastRecordedChordCount_ = 0;
}

} // namespace ned::editor
