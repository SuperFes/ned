#include "DapThreadsPanel.h"

#include <algorithm>
#include <utility>

namespace ned::ui {

DapThreadsPanel::DapThreadsPanel(const Theme& theme, editor::dap::Manager& dapManager) : theme_(theme), dapManager_(dapManager), table_(theme) {
    table_.SetDigitActivate(true);
    table_.SetOnActivate([this](const std::string& id) { HandleActivate(id); });
    table_.SetOnCancel([this] {
        if (onCancel_) {
            onCancel_();
        }
    });
    table_.SetOnKey([this](const editor::KeyChord& chord) { HandleKey(chord); });
}

TableView& DapThreadsPanel::Popup() {
    return table_;
}

void DapThreadsPanel::SetOnMessage(std::function<void(std::string)> handler) {
    onMessage_ = std::move(handler);
}

void DapThreadsPanel::SetOnCancel(std::function<void()> handler) {
    onCancel_ = std::move(handler);
}

void DapThreadsPanel::Show() {
    landOnCurrent_ = true;
    Refresh();
}

void DapThreadsPanel::Refresh() {
    dapManager_.RequestThreads([this](std::vector<editor::dap::Manager::Thread> threads) {
        rows_ = std::move(threads);
        RefreshDisplay();
        // The thread the debuggee is stopped/inspecting on, the first time
        // after opening; a later (stop-triggered) refresh leaves the
        // selection wherever the user put it.
        if (landOnCurrent_) {
            landOnCurrent_ = false;
            table_.SelectRow(std::to_string(dapManager_.FocusedThreadId()));
        }
    });
}

void DapThreadsPanel::RefreshDisplay() {
    table::Model model;
    model.title       = "Threads";
    model.placeholder = "(no threads)";
    model.columns     = {
        table::Column{.id = "current", .sortable = false},
        table::Column{.id = "name", .header = "Thread", .width = table::Column::Width::Flex, .minWidth = 6},
        table::Column{.id = "id", .header = "Id", .align = table::Align::Right},
    };
    model.groups.emplace_back();
    const int current = dapManager_.FocusedThreadId();
    for (const editor::dap::Manager::Thread& thread : rows_) {
        const bool isCurrent = thread.id == current;
        model.groups[0].rows.push_back(table::Row{
            .id    = std::to_string(thread.id),
            .cells = {table::Cell{.text = isCurrent ? "→" : "", .foreground = theme_.borderAccent.foreground, .bold = true},
                      table::Cell{.text = thread.name},
                      table::Cell{.text = "#" + std::to_string(thread.id), .sortNumber = thread.id}}});
    }
    table_.SetModel(std::move(model));
}

void DapThreadsPanel::HandleActivate(const std::string& id) {
    const auto found =
        std::ranges::find_if(rows_, [&id](const editor::dap::Manager::Thread& thread) { return std::to_string(thread.id) == id; });
    if (found == rows_.end()) {
        return;
    }
    const editor::dap::Manager::Thread thread = *found;
    dapManager_.SelectThread(thread.id, [this, name = thread.name](bool success) {
        if (onMessage_) {
            onMessage_(success ? ("Selected thread: " + name) : "Failed to select thread.");
        }
        RefreshDisplay(); // the current-thread marker (->) moves regardless of success/failure reporting above
    });
}

void DapThreadsPanel::HandleKey(const editor::KeyChord& chord) {
    if (chord.Codepoint == U'g') {
        Refresh();
    }
}

} // namespace ned::ui
