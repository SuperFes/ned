//
// generic-popup follow-up: an ibuffer/dired-style buffer-list panel -- the
// focus-mode ListPopup's first real consumer. Owns one ListPopup (via
// Popup()) for the actual rendering/navigation/digit-jump/Enter/Escape
// contract, layering buffer-management semantics on top through
// ListPopup::SetOnActivate/SetOnCancel/SetOnKey:
//
//  - d marks the selected buffer for kill and advances the selection
//    (Emacs dired/ibuffer's own convention); u unmarks. g refreshes the row
//    list from BufferList (marks survive a refresh, matched by buffer
//    identity).
//  - x executes: any marked buffer that's modified triggers a single
//    one-shot y/n confirmation for the whole batch (self-contained --
//    Popup() already owns real keyboard focus, so no InteractiveRequest
//    plumbing is needed); y/n during that confirmation is read directly,
//    not through the normal d/u/x/g dispatch.
//  - Enter, or a digit key (1-9, ListPopup's own single-keystroke
//    direct-pick contract), switches straight to that row's buffer via
//    SetOnRequestSwitchToBuffer and expects the caller to hide/return focus
//    in response (main.cpp's toggle lambda does this the same way it does
//    for TerminalPanel/AcpPanel).
//  - Escape/C-g (ListPopup's own quit contract) either cancels an in-
//    progress kill confirmation, or -- if not confirming -- fires
//    SetOnCancel for the caller to hide/return focus the same way.
//
// A batch kill goes through SetOnBufferClosing (fired once per closed
// buffer, right before BufferList::Close) rather than
// WindowManager::RequestCloseBuffer -- that method re-prompts per buffer
// for a modified one, which would fight this panel's own single batch
// confirmation; main.cpp wires this hook to
// WindowManager::NotifyBufferClosing, the exact same pattern
// ProjectSidebar::SetOnBufferClosed already uses for its own
// bypasses-BufferView::CloseBufferNow close path.
//

#ifndef NED_UI_BUFFERLISTPANEL_H
#define NED_UI_BUFFERLISTPANEL_H

#include <cstddef>
#include <functional>
#include <vector>

#include "Editor/Key.h"
#include "ListPopup.h"
#include "Text/BufferList.h"
#include "Theme.h"

namespace ned::ui {

class BufferListPanel {
  public:
    // theme and bufferList must outlive this panel (same requirement every
    // other themed/BufferList-referencing widget in this codebase has).
    BufferListPanel(const Theme& theme, text::BufferList& bufferList);

    // The actual Widget to register with OverlayHost::Add/Show/Hide/
    // SetFocusReturn and to call TakeFocus() on -- this class itself is a
    // plain controller, not a Widget, since ListPopup already is one.
    [[nodiscard]] ListPopup& Popup();

    // Rebuilds the row list from BufferList and resets any in-progress kill
    // confirmation -- call before showing the panel (main.cpp's toggle
    // lambda calls this, then Popup().TakeFocus()).
    void Show();

    // Fired with the chosen buffer on Enter or a digit-key direct pick.
    // The caller is expected to switch to it and hide/return focus, same
    // as SetOnCancel below.
    void SetOnRequestSwitchToBuffer(std::function<void(text::Buffer&)> handler);

    // Fired on Escape/C-g when no kill confirmation is in progress -- the
    // caller is expected to hide the panel/return focus (OverlayHost's
    // SetFocusReturn already handles the focus half automatically on Hide).
    void SetOnCancel(std::function<void()> handler);

    // Fired once per buffer immediately before it's closed by x (execute)
    // -- see this file's own header comment for why this exists instead of
    // WindowManager::RequestCloseBuffer.
    void SetOnBufferClosing(std::function<void(text::Buffer&)> handler);

  private:
    text::BufferList&  bufferList_;
    ListPopup           popup_;

    std::vector<text::Buffer*> rows_;   // this Show()/Refresh()'s buffer order, index-parallel to popup_'s rows
    std::vector<bool>          marked_; // index-parallel to rows_
    std::size_t                selectedIndex_ = 0;

    bool                        confirming_ = false;
    std::vector<text::Buffer*> pendingKill_; // computed by x, executed on 'y'

    std::function<void(text::Buffer&)> onRequestSwitchTo_;
    std::function<void()>              onCancel_;
    std::function<void(text::Buffer&)> onBufferClosing_;

    void Refresh();        // rebuilds rows_/marked_ from bufferList_, preserving marks by identity
    void RefreshDisplay();  // pushes rows_/marked_/selectedIndex_ into popup_'s model

    void HandleActivate(std::size_t index);
    void HandleCancel();
    void HandleKey(const editor::KeyChord& chord);

    void BeginExecute(); // x -- either kills immediately or starts confirmation
    void ExecuteKill();  // the actual batch close, once confirmed (or nothing needed confirming)
};

} // namespace ned::ui

#endif // NED_UI_BUFFERLISTPANEL_H
