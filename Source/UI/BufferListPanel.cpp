#include "BufferListPanel.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "Editor/BufferSave.h"

namespace ned::ui {

namespace {

    // buffer-list-panel-columns follow-up: a plain "N B"/"N.NK"/"N.NM"
    // formatter for the row's own `right` column -- Editor/BufferSave.h's
    // callers don't need one, and Buffer.cpp's own FormatBytesHuman is
    // GiB/MiB-only (tuned for disk-space-error messages, not a typical open
    // buffer's much smaller size), so this stays a small local helper
    // rather than reaching for either.
    std::string FormatBufferSize(std::size_t bytes) {
        constexpr double kKiB = 1024.0;
        constexpr double kMiB = 1024.0 * 1024.0;
        char             buf[16];
        if (bytes < static_cast<std::size_t>(kKiB)) {
            std::snprintf(buf, sizeof(buf), "%zuB", bytes);
        }
        else if (bytes < static_cast<std::size_t>(kMiB)) {
            std::snprintf(buf, sizeof(buf), "%.1fK", static_cast<double>(bytes) / kKiB);
        }
        else {
            std::snprintf(buf, sizeof(buf), "%.1fM", static_cast<double>(bytes) / kMiB);
        }
        return buf;
    }

} // namespace

BufferListPanel::BufferListPanel(const Theme& theme, text::BufferList& bufferList)
    : bufferList_(bufferList), popup_(theme) {
    popup_.SetFocusable(true);
    popup_.SetOnHighlightChange([this](std::size_t index) { selectedIndex_ = index; });
    popup_.SetOnActivate([this](std::size_t index) { HandleActivate(index); });
    popup_.SetOnCancel([this] { HandleCancel(); });
    popup_.SetOnKey([this](const editor::KeyChord& chord) { HandleKey(chord); });
    popup_.SetOnLeftColumnClick([this](std::size_t index, int columnOffset) { ToggleMarkAt(index, columnOffset); });
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

void BufferListPanel::SetOnMessage(std::function<void(std::string)> handler) {
    onMessage_ = std::move(handler);
}

void BufferListPanel::Show() {
    confirming_ = false;
    pendingKill_.clear();
    Refresh();
}

void BufferListPanel::Refresh() {
    const std::vector<text::Buffer*> previousRows = rows_;
    const std::vector<bool>          previousKill = markedKill_;
    const std::vector<bool>          previousSave = markedSave_;

    rows_.clear();
    markedKill_.clear();
    markedSave_.clear();
    for (const auto& buffer : bufferList_.Buffers()) {
        rows_.push_back(buffer.get());
        bool wasKill = false;
        bool wasSave = false;
        for (std::size_t i = 0; i < previousRows.size(); ++i) {
            if (previousRows[i] == buffer.get()) {
                wasKill = i < previousKill.size() && previousKill[i];
                wasSave = i < previousSave.size() && previousSave[i];
                break;
            }
        }
        markedKill_.push_back(wasKill);
        markedSave_.push_back(wasSave);
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
        left += markedKill_[i] ? 'D' : ' ';
        left += markedSave_[i] ? 'S' : ' ';
        left += buffer.Modified() ? '*' : ' ';

        std::string right = FormatBufferSize(buffer.Size());
        if (buffer.ReadOnly()) {
            right += " RO";
        }

        model.rows.push_back(
            {.left = left, .main = buffer.Name(), .accented = markedKill_[i] || markedSave_[i], .right = right});
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
        markedKill_[selectedIndex_] = true;
        selectedIndex_              = std::min(selectedIndex_ + 1, rows_.size() - 1);
        RefreshDisplay();
    }
    else if (chord.Codepoint == U's') {
        markedSave_[selectedIndex_] = true;
        selectedIndex_              = std::min(selectedIndex_ + 1, rows_.size() - 1);
        RefreshDisplay();
    }
    else if (chord.Codepoint == U'u') {
        markedKill_[selectedIndex_] = false;
        markedSave_[selectedIndex_] = false;
        selectedIndex_              = std::min(selectedIndex_ + 1, rows_.size() - 1);
        RefreshDisplay();
    }
    else if (chord.Codepoint == U'x') {
        BeginExecute();
    }
    else if (chord.Codepoint == U'g') {
        Refresh();
    }
}

void BufferListPanel::ToggleMarkAt(std::size_t index, int columnOffset) {
    // Mid-confirmation, a click's only sane targets are y/n, which the
    // mouse doesn't drive at all -- ignore rather than let a mark toggle
    // silently invalidate pendingKill_ underneath the pending y/n prompt.
    if (confirming_ || index >= rows_.size()) {
        return;
    }
    selectedIndex_ = index;
    if (columnOffset == 0) {
        markedKill_[index] = !markedKill_[index];
    }
    else if (columnOffset == 1) {
        markedSave_[index] = !markedSave_[index];
    }
    else {
        return; // the modified `*` glyph -- not a mark, not a click target
    }
    RefreshDisplay();
}

void BufferListPanel::BeginExecute() {
    // Saves are immediate and non-destructive -- no confirmation needed,
    // unlike the kill half below. Doing this first also means a buffer
    // marked both S and D saves before its own kill confirmation is
    // evaluated, so a since-modified buffer that just got saved no longer
    // forces that confirmation.
    std::size_t savedCount = 0;
    std::string saveFailures;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (!markedSave_[i]) {
            continue;
        }
        markedSave_[i]            = false;
        text::Buffer& buffer      = *rows_[i];
        const auto    noteFailure = [&](const std::string& reason) {
            saveFailures += (saveFailures.empty() ? "" : ", ") + buffer.Name() + " (" + reason + ")";
        };
        if (!buffer.Path()) {
            noteFailure("no file");
            continue;
        }
        try {
            editor::WriteBufferToDisk(buffer);
            ++savedCount;
        }
        catch (const std::exception& e) {
            noteFailure(e.what());
        }
    }
    if (savedCount > 0 || !saveFailures.empty()) {
        std::string message = "Saved " + std::to_string(savedCount) + " buffer" + (savedCount == 1 ? "" : "s");
        if (!saveFailures.empty()) {
            message += " (failed: " + saveFailures + ")";
        }
        if (onMessage_) {
            onMessage_(std::move(message));
        }
        RefreshDisplay(); // modified/size flags may have just changed
    }

    pendingKill_.clear();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (markedKill_[i]) {
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
