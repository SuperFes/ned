//
// Debugging wishlist: a dedicated live thread window (gf/GDBFrontend audit,
// ROADMAP.md's own entry) -- BufferListPanel's exact controller-plus-
// focus-mode-ListPopup shape (a plain, non-Widget controller owning one
// ListPopup via Popup(), driven through SetOnHighlightChange/SetOnActivate/
// SetOnCancel rather than a bespoke Paint()/OnEvent() override), applied to
// DapManager::RequestThreads/SelectThread instead of BufferList.
//
// Unlike dap-select-thread's one-shot numbered picker (BufferView::
// BeginDapThreadSelect, still the M-x default for a quick pick), this panel
// stays open across stops -- WindowManager::SetOnDapThreadsRefreshNeeded
// wires Refresh() into the same SetOnStopped handler that already updates
// the status line and jumps to source on every stop event, so the row list
// (and which row is marked current) is never more than one stop stale while
// the panel is visible. Refresh() is a safe no-op call at any other time
// (Show()'s own call handles the "just opened" case; a stray call while
// hidden just repaints an invisible widget).
//
// Enter/digit-pick/click select that row's thread via DapManager::
// SelectThread and report success/failure through SetOnMessage --
// BeginDapThreadSelect's own status-line wording, not a jump (selecting a
// thread changes which one subsequent inspection/step/continue targets;
// it doesn't by itself move the debuggee or the buffer, matching
// DapSelectThread's existing behavior exactly). 'g' manually re-fetches,
// BufferListPanel's own convention, useful the instant a panel wasn't open
// yet to catch the stop-triggered refresh.
//

#ifndef NED_UI_DAPTHREADSPANEL_H
#define NED_UI_DAPTHREADSPANEL_H

#include <functional>
#include <string>
#include <vector>

#include "Editor/Dap/DapManager.h"
#include "Editor/Key.h"
#include "ListPopup.h"
#include "Theme.h"

namespace ned::ui {

class DapThreadsPanel {
  public:
    // theme and dapManager must outlive this panel (same requirement every
    // other themed/manager-referencing widget in this codebase has).
    DapThreadsPanel(const Theme& theme, editor::dap::DapManager& dapManager);

    // The actual Widget to register with OverlayHost::Add/Show/Hide/
    // SetFocusReturn and to call TakeFocus() on -- BufferListPanel's own
    // "this class is a plain controller, not a Widget" shape.
    [[nodiscard]] ListPopup& Popup();

    // Rebuilds the row list and resets selection to the current thread (if
    // any) -- call before showing the panel (main.cpp's toggle lambda calls
    // this, then Popup().TakeFocus()).
    void Show();

    // Re-fetches the row list from DapManager without resetting selection
    // (clamped if the new list is shorter) -- called by Show() and by
    // WindowManager::SetOnDapThreadsRefreshNeeded's wiring on every stop
    // event while the panel may or may not be visible.
    void Refresh();

    // Fired after a successful or failed SelectThread call, BufferListPanel::
    // SetOnMessage's own shape -- the caller (main.cpp) forwards this to the
    // shared status line.
    void SetOnMessage(std::function<void(std::string)> handler);

    // Fired on Escape/C-g -- the caller is expected to hide the panel/return
    // focus (OverlayHost's SetFocusReturn already handles the focus half
    // automatically on Hide), BufferListPanel::SetOnCancel's own contract.
    void SetOnCancel(std::function<void()> handler);

  private:
    editor::dap::DapManager& dapManager_;
    ListPopup                popup_;

    std::vector<editor::dap::DapManager::Thread> rows_; // this Show()/Refresh()'s thread order, index-parallel to popup_'s rows
    std::size_t                                  selectedIndex_ = 0;

    std::function<void(std::string)> onMessage_;
    std::function<void()>            onCancel_;

    void RefreshDisplay(); // pushes rows_/selectedIndex_ into popup_'s model, marking DapManager::CurrentThreadId()'s own row
    void HandleActivate(std::size_t index);
    void HandleKey(const editor::KeyChord& chord);
};

} // namespace ned::ui

#endif // NED_UI_DAPTHREADSPANEL_H
