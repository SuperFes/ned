//
// An ibuffer/dired-style buffer list: a TableView (via Popup()) of every
// open buffer -- marks, name, size, file -- with buffer management layered
// on through its SetOnActivate/SetOnCancel/SetOnKey/SetOnCellClick:
//
//  - d marks the selected buffer for kill and advances the selection
//    (dired/ibuffer's own convention); s marks it for save the same way; u
//    clears whichever mark(s) that row has. g refreshes the list from
//    BufferList (marks survive a refresh, matched by buffer identity). A
//    click on the D or S glyph toggles that one mark instead of switching
//    to the buffer.
//  - x executes: every save-marked buffer is written to disk immediately
//    (non-destructive, no confirmation) via Editor/BufferSave.h's
//    WriteBufferToDisk -- the same helper save-buffer/save-some-buffers use
//    -- then, of whatever's left marked for kill, any that's still modified
//    triggers a single one-shot y/n confirmation for the whole kill batch
//    (self-contained -- Popup() already owns real keyboard focus, so no
//    InteractiveRequest plumbing is needed); y/n during that confirmation
//    is read directly, not through the normal d/u/s/x/g dispatch.
//  - Enter, or a digit key (1-9, the Nth row as currently sorted),
//    switches straight to that row's buffer via SetOnRequestSwitchToBuffer
//    and expects the caller to hide/return focus in response.
//  - Escape/C-g either cancels an in-progress kill confirmation, or -- if
//    not confirming -- fires SetOnCancel for the caller to hide/return
//    focus the same way.
//
// The marks column is ibuffer's: D kill, S save, * modified, % read-only.
// Size and file are the facts a plain text::Buffer can report about itself
// with no access to a Mode/WindowManager (which this panel deliberately has
// neither of).
//
// A batch kill goes through SetOnBufferClosing (fired once per closed
// buffer, right before BufferList::Close) rather than
// WindowManager::RequestCloseBuffer -- that method re-prompts per buffer
// for a modified one, which would fight this panel's own single batch
// confirmation; main.cpp wires this hook to
// WindowManager::NotifyBufferClosing, the exact same pattern
// ProjectSidebar::SetOnBufferClosed already uses for its own
// bypasses-BufferView::CloseBufferNow close path. SetOnMessage (also
// optional, defaults to a no-op) is fired after a batch save with a
// summary ("Saved 2 buffers", failures named inline) -- main.cpp wires it
// to the same shared statusMessage string every other command uses.
//

#ifndef NED_UI_BUFFERLISTPANEL_H
#define NED_UI_BUFFERLISTPANEL_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Editor/Key.h"
#include "TableView.h"
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
    // plain controller, not a Widget, since TableView already is one.
    [[nodiscard]] TableView& Popup();

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

    // Fired after a batch save (x, when anything was marked with s) with a
    // one-line summary. Optional -- defaults to a no-op, same as every
    // other Set* hook here.
    void SetOnMessage(std::function<void(std::string)> handler);

  private:
    const Theme&      theme_;
    text::BufferList& bufferList_;
    TableView         table_;

    std::vector<text::Buffer*> rows_;       // BufferList's order as of the last Refresh(); ids are buffer names
    std::vector<bool>          markedKill_; // index-parallel to rows_ -- d/D
    std::vector<bool>          markedSave_; // index-parallel to rows_ -- s/S

    bool                       confirming_ = false;
    std::vector<text::Buffer*> pendingKill_; // computed by x, executed on 'y'

    std::function<void(text::Buffer&)> onRequestSwitchTo_;
    std::function<void()>              onCancel_;
    std::function<void(text::Buffer&)> onBufferClosing_;
    std::function<void(std::string)>   onMessage_;

    void Refresh();        // rebuilds rows_/markedKill_/markedSave_ from bufferList_, preserving marks by identity
    void RefreshDisplay(); // pushes rows_/markedKill_/markedSave_ into table_'s model

    [[nodiscard]] std::optional<std::size_t> IndexOf(const std::string& name) const;
    [[nodiscard]] std::optional<std::size_t> SelectedIndex() const;

    void HandleActivate(const std::string& name);
    void HandleCancel();
    void HandleKey(const editor::KeyChord& chord);
    bool HandleCellClick(const std::string& name, std::size_t column, int offset);

    void BeginExecute(); // x -- saves every markedSave_ row immediately, then either kills markedKill_ rows or starts confirmation
    void ExecuteKill();  // the actual batch close, once confirmed (or nothing needed confirming)
};

} // namespace ned::ui

#endif // NED_UI_BUFFERLISTPANEL_H
