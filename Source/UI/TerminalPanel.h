//
// Terminal-panel follow-up: the built-in terminal, a bottom-drawer overlay
// composing Editor/Terminal/Emulator (libvterm emulation) with
// Editor/Terminal/PtyProcess (the forkpty shell). Registered with main.cpp's
// OverlayHost rather than the Container tree -- it floats over BufferView
// without reflowing anything; see Overlay.h's header comment for the layer's
// contract.
//
// Focus is the entire modality story: while this panel holds keyboard focus
// (main.cpp routes keys to FocusedWidget() only), essentially every key is
// forwarded to the shell via Emulator::SendKey -- including C-c/C-z/C-s
// (EventLoop already disabled line signals and IXON, so they arrive as
// ordinary bytes), which a shell genuinely needs. Exactly one chord is
// reserved and never forwarded: C-` (kToggleChord), which invokes the
// registered toggle callback. On terminals where C-` isn't pressable at all
// (legacy encodings send NUL, which the Notcurses patch maps to C-Space --
// deliberately not stolen here, shells use C-@ for set-mark; a real, live
// stuck-drawer report drove the escape hatches below), the mouse always
// works: the title row carries three bracketed buttons, right-aligned --
// [▼] minimize (hide, shell kept alive -- identical to toggle-hide), [▲]
// maximize (toggle full buffer-area height; see Maximized()), [×] close
// (kill the shell and hide; the next show spawns a fresh one) -- and
// clicking outside the drawer refocuses the editor (OverlayHost only
// intercepts clicks inside the drawer's Box), after which `C-c t` (the
// portable editor-side binding) hides the visible panel outright rather
// than refocusing it (see main.cpp's toggle lambda for why that
// non-VS-Code semantic is deliberate).
//
// Lifecycle: the shell spawns on first EnsureStarted(), survives hide/show
// untouched, and is torn down with this widget after EventLoop::Run returns
// (the same owner-destroys-after-Run ordering every TaskProcess/LspClient
// owner relies on). A shell that exits leaves the panel in an "exited"
// state -- output kept visible, `[process exited]` appended, Enter respawns
// -- rather than closing the drawer out from under the user. The dead
// PtyProcess is deliberately kept allocated until the respawn: destroying it
// inside its own onExit callback would tear down the std::function currently
// being executed.
//

#ifndef NED_UI_TERMINALPANEL_H
#define NED_UI_TERMINALPANEL_H

#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "Editor/Key.h"
#include "Editor/Terminal/Emulator.h"
#include "Editor/Terminal/PtyProcess.h"
#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class EventLoop;

class TerminalPanel : public Widget {
  public:
    explicit TerminalPanel(const Theme& theme);

    // The one chord the panel never forwards to the shell -- see header
    // comment.
    static const editor::KeyChord kToggleChord;

    // Same nullable-EventLoop convention as BufferView::SetEventLoop: unset
    // (every headless test), EnsureStarted never spawns a real shell and
    // key output goes only to the write sink.
    void SetEventLoop(EventLoop* eventLoop);

    // Invoked when the reserved toggle chord is pressed while this panel
    // holds focus -- wired by main.cpp to the same three-state toggle
    // lambda the toggle-terminal command drives. Unset is a safe no-op.
    void SetOnToggleRequest(std::function<void()> onToggle);

    // Spawns the shell if none is running yet (or the previous one exited).
    // Safe to call repeatedly; a live shell is never disturbed.
    void EnsureStarted();

    [[nodiscard]] bool ShellRunning() const;

    // Routes pty output into the emulator -- public because it's also the
    // deterministic output-injection seam every headless test uses, the
    // TaskProcess::DispatchOutput precedent.
    void Feed(std::string_view bytes);

    // Replaces where forwarded key bytes go -- tests capture them here
    // instead of spawning a shell.
    void SetWriteSinkForTesting(std::function<void(std::string_view)> sink);

    // Drives the shell-exited transition directly -- the headless stand-in
    // for PtyProcess's onExit callback, same seam philosophy as Feed().
    void HandleExitForTesting() {
        HandleExit();
    }

    void Paint(Canvas canvas) override;
    bool OnEvent(const Event& event) override;
    void OnResize(Size previous) override;

    [[nodiscard]] bool Focusable() const override {
        return true;
    }

    [[nodiscard]] std::optional<Point> CursorPosition() const override;

    [[nodiscard]] CursorShape CursorShapeHint() const override {
        return CursorShape::Block;
    }

    // Whether the drawer is currently maximized (full buffer-area height
    // instead of the configured percentage) -- flipped by the title row's
    // [▲] button; main.cpp's placement function reads it fresh.
    [[nodiscard]] bool Maximized() const {
        return maximized_;
    }

    // Invoked whenever this panel changes something its own placement
    // depends on (the maximize toggle) -- wired by main.cpp to re-box the
    // overlay, since the panel can't reach the OverlayHost itself.
    void SetOnLayoutChange(std::function<void()> onLayoutChange);

    // Kills the shell and clears the session outright -- the title row's
    // [×], as opposed to [▼]/toggle-hide which keep the shell alive. Safe
    // headlessly (no pty to kill); public as a test seam like Feed().
    void CloseSession();

  private:
    // Title-row buttons, drawn right-aligned as [▼][▲][×] (minimize/
    // maximize/close). Minimum width for them to be drawn/hittable.
    static constexpr int kMinWidthForTitleButtons = 14;

    enum class TitleButton { None,
                             Minimize,
                             Maximize,
                             Close };

    [[nodiscard]] int         ContentRows() const;
    [[nodiscard]] int         ContentCols() const;
    [[nodiscard]] TitleButton TitleButtonAt(Point local) const;

    void HandleExit();
    void ForwardPendingOutput();
    void ScrollBy(int deltaLines);

    const Theme& theme_;

    editor::terminal::Emulator                    emulator_;
    std::unique_ptr<editor::terminal::PtyProcess> pty_;
    bool                                          exited_ = false;

    EventLoop*                            eventLoop_ = nullptr; // see SetEventLoop
    std::function<void()>                 onToggleRequest_;
    std::function<void()>                 onLayoutChange_; // see SetOnLayoutChange
    std::function<void(std::string_view)> writeSink_;
    bool                                  maximized_ = false;

    // Lines scrolled up into the ring; 0 = live view. Snapped back to 0 by
    // any forwarded keypress (not by output arriving -- reading scrollback
    // while a command streams shouldn't fight the user).
    int scrollbackOffset_ = 0;
};

} // namespace ned::ui

#endif // NED_UI_TERMINALPANEL_H
