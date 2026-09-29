#include "ChoicePicker.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "Editor/FuzzyMatch.h"
#include "Text/Utf8.h"

namespace ned::ui::acppanel {

ChoicePicker::ChoicePicker(std::string title, std::vector<ChoiceItem> items, std::function<void(std::size_t)> onChoose) : title_(std::move(title)), items_(std::move(items)), onChoose_(std::move(onChoose)) {
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].current) {
            selection_ = i;
            break;
        }
    }
}

std::vector<std::size_t> ChoicePicker::Visible() const {
    std::vector<std::pair<int, std::size_t>> scored;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (filter_.empty()) {
            scored.emplace_back(0, i);
        }
        else if (const std::optional<int> score = editor::FuzzyScore(items_[i].label, filter_)) {
            scored.emplace_back(*score, i);
        }
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::size_t> visible;
    visible.reserve(scored.size());
    for (const auto& [score, index] : scored) {
        visible.push_back(index);
    }
    return visible;
}

ChoicePicker::KeyResult ChoicePicker::HandleKey(const editor::KeyChord& chord) {
    const std::vector<std::size_t> visible = Visible();
    auto                           choose  = [&](std::size_t position) {
        if (position >= visible.size()) {
            return KeyResult::Handled;
        }
        if (onChoose_) {
            onChoose_(visible[position]);
        }
        return KeyResult::Chosen;
    };

    if (chord.Special == editor::SpecialKey::Escape || (chord.Control && chord.Codepoint == U'g')) {
        return KeyResult::Closed;
    }
    if (chord.Special == editor::SpecialKey::Enter) {
        return choose(selection_);
    }
    if (chord.Special == editor::SpecialKey::Up || chord.Special == editor::SpecialKey::Down ||
        (chord.Control && (chord.Codepoint == U'p' || chord.Codepoint == U'n'))) {
        if (!visible.empty()) {
            const bool down = chord.Special == editor::SpecialKey::Down || chord.Codepoint == U'n';
            selection_      = down ? (selection_ + 1) % visible.size() : (selection_ + visible.size() - 1) % visible.size();
        }
        return KeyResult::Handled;
    }
    if (chord.Special == editor::SpecialKey::Backspace) {
        if (!filter_.empty()) {
            filter_.resize(text::PreviousCodepointBoundary(filter_, filter_.size()));
            selection_ = 0;
        }
        return KeyResult::Handled;
    }
    const bool plain = !chord.Control && !chord.Meta && chord.Special == editor::SpecialKey::None && chord.Codepoint != 0;
    if (plain && filter_.empty() && chord.Codepoint >= U'1' && chord.Codepoint <= U'9') {
        return choose(static_cast<std::size_t>(chord.Codepoint - U'1'));
    }
    if (plain) {
        filter_ += text::EncodeCodepointUtf8(chord.Codepoint);
        selection_ = 0;
    }
    return KeyResult::Handled;
}

std::vector<DisplayLine> ChoicePicker::Format(int maxRows) const {
    std::vector<DisplayLine> lines;
    lines.push_back({.text = title_ + (filter_.empty() ? "" : "  " + filter_), .style = DisplayStyle::Warning});
    const std::vector<std::size_t> visible = Visible();
    if (visible.empty()) {
        lines.push_back({.text = "  (no matches)", .style = DisplayStyle::Dim});
        return lines;
    }
    const std::size_t room  = static_cast<std::size_t>(std::max(1, maxRows - 1));
    const std::size_t first = selection_ >= room ? selection_ - room + 1 : 0;
    for (std::size_t position = first; position < visible.size() && position < first + room; ++position) {
        const ChoiceItem& item     = items_[visible[position]];
        const bool        selected = position == selection_;
        const std::string number   = filter_.empty() && position < 9 ? std::to_string(position + 1) : " ";
        std::string       text     = (selected ? "> " : "  ") + number + " " + (item.current ? "● " : "  ") + item.label;
        if (!item.detail.empty()) {
            text += "  " + item.detail;
        }
        lines.push_back({.text = std::move(text), .style = selected ? DisplayStyle::Accent : DisplayStyle::Plain});
    }
    return lines;
}

} // namespace ned::ui::acppanel
