#include "PrefixArgument.h"

namespace ned::editor {

namespace {

    bool IsPlainCharacter(const KeyChord& chord) {
        return !chord.Control && !chord.Meta && chord.Special == SpecialKey::None && chord.Codepoint != 0;
    }

    bool IsCtrlU(const KeyChord& chord) {
        return chord.Control && !chord.Meta && chord.Special == SpecialKey::None && chord.Codepoint == U'u';
    }

} // namespace

PrefixArgumentReader::PrefixArgumentReader() = default;

PrefixArgumentReader::Outcome PrefixArgumentReader::HandleKey(const KeyChord& chord) {
    if (!hasDigits_ && !negative_ && IsCtrlU(chord)) {
        ++rawCuCount_;
        return Outcome::Continue;
    }
    if (IsPlainCharacter(chord) && chord.Codepoint >= U'0' && chord.Codepoint <= U'9') {
        numericValue_ = numericValue_ * 10 + static_cast<long>(chord.Codepoint - U'0');
        hasDigits_    = true;
        return Outcome::Continue;
    }
    if (!hasDigits_ && !negative_ && IsPlainCharacter(chord) && chord.Codepoint == U'-') {
        negative_ = true;
        return Outcome::Continue;
    }
    return Outcome::Terminate;
}

long PrefixArgumentReader::Value() const {
    if (hasDigits_) {
        return negative_ ? -numericValue_ : numericValue_;
    }
    if (negative_) {
        return -1;
    }
    long multiplier = 1;
    for (int i = 0; i < rawCuCount_; ++i) {
        multiplier *= 4;
    }
    return multiplier;
}

std::string PrefixArgumentReader::StatusText() const {
    std::string text = "C-u ";
    if (negative_ && !hasDigits_) {
        text += "-";
    }
    else {
        text += std::to_string(Value());
    }
    text += "-";
    return text;
}

} // namespace ned::editor
