#include "BufferListPanel.h"

#include <algorithm>
#include <utility>

namespace ned::ui {

BufferListPanel::BufferListPanel(const Theme& theme, text::BufferList& bufferList)
    : bufferList_(bufferList), popup_(theme) {
    popup_.SetFocusable(true);
    popup_.SetOnHighlightChange([this](std::size_t index) { selectedIndex_ = index; });
    popup_.SetOnActivate([this](std::size_t index) { HandleActivate(index); });
    popup_.SetOnCancel([this] { HandleCancel(); });
    popup_.SetOnKey([this](const editor::KeyChord& chord) { HandleKey(chord); });
}

ListPopup& BufferListPanel::Popup() {
    return popup_;
}

void BufferListPanel::SetOnRequestSwitchToBuffer(std::function<void(text::Buffer&)> handler) {
    onRequestSwitchTo_ = std::move(handler);
}

void BufferListPanel::SetOnCancel(std::function<void()> handler) {
    onCancel_ = std::move(handler);
}

void BufferListPanel::SetOnBufferClosing(std::function<void(text::Buffer&)> handler) {
    onBufferClosing_ = std::move(handler);
}

void BufferListPanel::Show() {
    confirming_ = false;
    pendingKill_.clear();
    Refresh();
}

void BufferListPanel::Refresh() {
    const std::vector<text::Buffer*> previousRows   = rows_;
    const std::vector<bool>          previousMarked = marked_;

    rows_.clear();
    marked_.clear();
    for (const auto& buffer : bufferList_.Buffers()) {
        rows_.push_back(buffer.get());
        bool wasMarked = false;
        for (std::size_t i = 0; i < previousRows.size(); ++i) {
            if (previousRows[i] == buffer.get() && i < previousMarked.size()) {
                wasMarked = previousMarked[i];
                break;
            }
        }
        marked_.push_back(wasMarked);
    }

    selectedIndex_ = rows_.empty() ? 0 : std::min(selectedIndex_, rows_.size() - 1);
    RefreshDisplay();
}

void BufferListPanel::RefreshDisplay() {
    ListPopupModel model;
    model.title = confirming_ ? "Kill buffers? (y/n)" : "Buffers";
    model.rows.reserve(rows_.size());
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const text::Buffer& buffer = *rows_[i];
        std::string          left;
        left += marked_[i] ? 'D' : ' ';
        left += buffer.Modified() ? '*' : ' ';
        model.rows.push_back({.left = left, .main = buffer.Name(), .accented = marked_[i]});
    }
    if (!rows_.empty()) {
        model.selectedIndex = selectedIndex_;
    }
    popup_.SetModel(std::move(model));
}

void BufferListPanel::HandleActivate(std::size_t index) {
    if (confirming_ || index >= rows_.size()) {
        return; // Enter/digit-pick is meaningless mid-confirmation
    }
    if (onRequestSwitchTo_) {
        onRequestSwitchTo_(*rows_[index]);
    }
}

void BufferListPanel::HandleCancel() {
    if (confirming_) {
        confirming_ = false;
        pendingKill_.clear();
        RefreshDisplay();
        return;
    }
    if (onCancel_) {
        onCancel_();
    }
}

void BufferListPanel::HandleKey(const editor::KeyChord& chord) {
    if (confirming_) {
        if (chord.Codepoint == U'y' || chord.Codepoint == U'Y') {
            ExecuteKill();
        }
        else if (chord.Codepoint == U'n' || chord.Codepoint == U'N') {
            HandleCancel();
        }
        return; // any other key is ignored while confirming
    }

    if (rows_.empty()) {
        return;
    }

    if (chord.Codepoint == U'd') {
        marked_[selectedIndex_] = true;
        selectedIndex_          = std::min(selectedIndex_ + 1, rows_.size() - 1);
        RefreshDisplay();
    }
    else if (chord.Codepoint == U'u') {
        marked_[selectedIndex_] = false;
        selectedIndex_          = std::min(selectedIndex_ + 1, rows_.size() - 1);
        RefreshDisplay();
    }
    else if (chord.Codepoint == U'x') {
        BeginExecute();
    }
    else if (chord.Codepoint == U'g') {
        Refresh();
    }
}

void BufferListPanel::BeginExecute() {
    pendingKill_.clear();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (marked_[i]) {
            pendingKill_.push_back(rows_[i]);
        }
    }
    if (pendingKill_.empty()) {
        return;
    }

    const bool anyModified =
        std::any_of(pendingKill_.begin(), pendingKill_.end(), [](const text::Buffer* buffer) { return buffer->Modified(); });
    if (anyModified) {
        confirming_ = true;
        RefreshDisplay();
        return;
    }
    ExecuteKill();
}

void BufferListPanel::ExecuteKill() {
    confirming_ = false;
    for (text::Buffer* buffer : pendingKill_) {
        if (onBufferClosing_) {
            onBufferClosing_(*buffer);
        }
        bufferList_.Close(buffer->Name());
    }
    pendingKill_.clear();
    Refresh();
}

} // namespace ned::ui
