#include "DapThreadsPanel.h"

#include <algorithm>
#include <utility>

namespace ned::ui {

DapThreadsPanel::DapThreadsPanel(const Theme& theme, editor::dap::DapManager& dapManager) : dapManager_(dapManager), popup_(theme) {
    popup_.SetFocusable(true);
    popup_.SetOnHighlightChange([this](std::size_t index) { selectedIndex_ = index; });
    popup_.SetOnActivate([this](std::size_t index) { HandleActivate(index); });
    popup_.SetOnCancel([this] {
        if (onCancel_) {
            onCancel_();
        }
    });
    popup_.SetOnKey([this](const editor::KeyChord& chord) { HandleKey(chord); });
}

ListPopup& DapThreadsPanel::Popup() {
    return popup_;
}

void DapThreadsPanel::SetOnMessage(std::function<void(std::string)> handler) {
    onMessage_ = std::move(handler);
}

void DapThreadsPanel::SetOnCancel(std::function<void()> handler) {
    onCancel_ = std::move(handler);
}

void DapThreadsPanel::Show() {
    selectedIndex_ = 0;
    Refresh();
}

void DapThreadsPanel::Refresh() {
    dapManager_.RequestThreads([this](std::vector<editor::dap::DapManager::Thread> threads) {
        rows_          = std::move(threads);
        selectedIndex_ = rows_.empty() ? 0 : std::min(selectedIndex_, rows_.size() - 1);
        // Land on the thread the debuggee is actually stopped/inspecting on
        // the very first fetch after opening (selectedIndex_ still at its
        // Show()-reset 0) -- a later Refresh() (the stop-triggered one)
        // deliberately leaves whatever row the user has scrolled to alone.
        if (selectedIndex_ == 0) {
            const int current = dapManager_.FocusedThreadId();
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                if (rows_[i].id == current) {
                    selectedIndex_ = i;
                    break;
                }
            }
        }
        RefreshDisplay();
    });
}

void DapThreadsPanel::RefreshDisplay() {
    ListPopupModel model;
    model.title = "Threads";
    model.rows.reserve(rows_.size());
    const int current = dapManager_.FocusedThreadId();
    for (const editor::dap::DapManager::Thread& thread : rows_) {
        const bool isCurrent = thread.id == current;
        model.rows.push_back({.left     = isCurrent ? "→" : "",
                              .main     = thread.name,
                              .accented = isCurrent,
                              .right    = "#" + std::to_string(thread.id)});
    }
    if (!rows_.empty()) {
        model.selectedIndex = selectedIndex_;
    }
    popup_.SetModel(std::move(model));
}

void DapThreadsPanel::HandleActivate(std::size_t index) {
    if (index >= rows_.size()) {
        return;
    }
    const editor::dap::DapManager::Thread thread = rows_[index];
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
