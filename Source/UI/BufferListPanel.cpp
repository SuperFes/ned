#include "BufferListPanel.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string_view>
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

    std::string DisplayPath(const std::filesystem::path& path) {
        std::string text = path.string();
        if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
            const std::string_view prefix(home);
            if (text.starts_with(prefix) && (text.size() == prefix.size() || text[prefix.size()] == '/')) {
                return "~" + text.substr(prefix.size());
            }
        }
        return text;
    }

    constexpr std::size_t kMarksColumn = 0;

} // namespace

BufferListPanel::BufferListPanel(const Theme& theme, text::BufferList& bufferList) : theme_(theme), bufferList_(bufferList), table_(theme) {
    table_.SetDigitActivate(true);
    table_.SetOnActivate([this](const std::string& name) { HandleActivate(name); });
    table_.SetOnCancel([this] { HandleCancel(); });
    table_.SetOnKey([this](const editor::KeyChord& chord) { HandleKey(chord); });
    table_.SetOnCellClick(
        [this](const std::string& name, std::size_t column, int offset) { return HandleCellClick(name, column, offset); });
}

TableView& BufferListPanel::Popup() {
    return table_;
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
    const std::vector<text::BufferIdentity> previousRows = rows_;
    const std::vector<bool>                 previousKill = markedKill_;
    const std::vector<bool>                 previousSave = markedSave_;

    rows_.clear();
    markedKill_.clear();
    markedSave_.clear();
    for (const auto& buffer : bufferList_.Buffers()) {
        rows_.emplace_back(*buffer);
        bool wasKill = false;
        bool wasSave = false;
        for (std::size_t i = 0; i < previousRows.size(); ++i) {
            if (previousRows[i].Is(*buffer)) {
                wasKill = i < previousKill.size() && previousKill[i];
                wasSave = i < previousSave.size() && previousSave[i];
                break;
            }
        }
        markedKill_.push_back(wasKill);
        markedSave_.push_back(wasSave);
    }
    RefreshDisplay();
}

void BufferListPanel::RefreshDisplay() {
    std::vector<const text::Buffer*> live;
    live.reserve(rows_.size());
    for (const text::BufferIdentity& row : rows_) {
        const text::Buffer* const buffer = bufferList_.Find(row);
        if (buffer == nullptr) {
            Refresh(); // a buffer closed elsewhere; Refresh rebuilds from live buffers only
            return;
        }
        live.push_back(buffer);
    }

    table::Model model;
    model.title   = confirming_ ? "Kill buffers? (y/n)" : "Buffers";
    model.columns = {
        table::Column{.id = "marks", .sortable = false},
        table::Column{.id = "name", .header = "Buffer", .width = table::Column::Width::Flex, .minWidth = 8},
        table::Column{.id              = "size",
                      .header          = "Size",
                      .align           = table::Align::Right,
                      .dropPriority    = 1,
                      .descendingFirst = true},
        table::Column{.id = "file", .header = "File", .width = table::Column::Width::Flex, .minWidth = 12, .dropPriority = 2},
    };
    model.groups.emplace_back();
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const text::Buffer& buffer = *live[i];
        std::string         marks;
        marks += markedKill_[i] ? 'D' : ' ';
        marks += markedSave_[i] ? 'S' : ' ';
        marks += buffer.Modified() ? '*' : ' ';
        marks += buffer.ReadOnly() ? '%' : ' ';

        const bool marked = markedKill_[i] || markedSave_[i];
        model.groups[0].rows.push_back(table::Row{
            .id    = buffer.Name(),
            .cells = {table::Cell{.text       = std::move(marks),
                                  .foreground = marked ? std::optional<Color>(theme_.borderAccent.foreground) : std::nullopt,
                                  .bold       = marked},
                      table::Cell{.text = buffer.Name()},
                      table::Cell{.text       = FormatBufferSize(buffer.Size()),
                                  .sortNumber = static_cast<std::int64_t>(buffer.Size())},
                      table::Cell{.text       = buffer.Path() ? DisplayPath(*buffer.Path()) : std::string(),
                                  .foreground = theme_.indentGuideForeground}}});
    }
    table_.SetModel(std::move(model));
}

std::optional<std::size_t> BufferListPanel::IndexOf(const std::string& name) const {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (const text::Buffer* const buffer = bufferList_.Find(rows_[i]); buffer != nullptr && buffer->Name() == name) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> BufferListPanel::SelectedIndex() const {
    const std::optional<std::string> name = table_.SelectedRowId();
    return name ? IndexOf(*name) : std::nullopt;
}

void BufferListPanel::HandleActivate(const std::string& name) {
    if (confirming_) {
        return; // Enter/digit-pick is meaningless mid-confirmation
    }
    if (const std::optional<std::size_t> index = IndexOf(name); index && onRequestSwitchTo_) {
        if (text::Buffer* const buffer = bufferList_.Find(rows_[*index])) {
            onRequestSwitchTo_(*buffer);
        }
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

    const std::optional<std::size_t> selected = SelectedIndex();
    if (chord.Codepoint == U'x') {
        BeginExecute();
    }
    else if (chord.Codepoint == U'g') {
        Refresh();
    }
    else if (selected && (chord.Codepoint == U'd' || chord.Codepoint == U's' || chord.Codepoint == U'u')) {
        if (chord.Codepoint == U'd') {
            markedKill_[*selected] = true;
        }
        else if (chord.Codepoint == U's') {
            markedSave_[*selected] = true;
        }
        else {
            markedKill_[*selected] = false;
            markedSave_[*selected] = false;
        }
        RefreshDisplay();
        table_.MoveSelection(1);
    }
}

bool BufferListPanel::HandleCellClick(const std::string& name, std::size_t column, int offset) {
    if (column != kMarksColumn) {
        return false;
    }
    // Mid-confirmation, a click's only sane targets are y/n, which the
    // mouse doesn't drive at all -- ignore rather than let a mark toggle
    // silently invalidate pendingKill_ underneath the pending y/n prompt.
    const std::optional<std::size_t> index = IndexOf(name);
    if (confirming_ || !index) {
        return true;
    }
    if (offset == 0) {
        markedKill_[*index] = !markedKill_[*index];
    }
    else if (offset == 1) {
        markedSave_[*index] = !markedSave_[*index];
    }
    else {
        return true; // the modified/read-only flags aren't marks
    }
    RefreshDisplay();
    return true;
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
        text::Buffer* const live  = bufferList_.Find(rows_[i]);
        if (live == nullptr) {
            continue;
        }
        text::Buffer& buffer      = *live;
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
        if (markedKill_[i] && bufferList_.Find(rows_[i]) != nullptr) {
            pendingKill_.push_back(rows_[i]);
        }
    }
    if (pendingKill_.empty()) {
        return;
    }

    const bool anyModified = std::ranges::any_of(pendingKill_, [this](const text::BufferIdentity& identity) {
        const text::Buffer* const buffer = bufferList_.Find(identity);
        return buffer != nullptr && buffer->Modified();
    });
    if (anyModified) {
        confirming_ = true;
        RefreshDisplay();
        return;
    }
    ExecuteKill();
}

void BufferListPanel::ExecuteKill() {
    confirming_ = false;
    for (const text::BufferIdentity& identity : pendingKill_) {
        text::Buffer* const buffer = bufferList_.Find(identity);
        if (buffer == nullptr) {
            continue; // closed elsewhere while the confirmation was up
        }
        if (onBufferClosing_) {
            onBufferClosing_(*buffer);
        }
        bufferList_.Close(buffer->Name());
    }
    pendingKill_.clear();
    Refresh();
}

} // namespace ned::ui
