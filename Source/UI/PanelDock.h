//
// Tabbed-bottom-dock-overlays follow-up: the shared tab strip TerminalPanel/
// AcpPanel (bottom-docked)/DebugConsolePanel used to each reinvent
// independently -- see ROADMAP.md's own "Tabbed bottom-dock overlays" entry
// for why: all three anchor to the same bottom screen region via their own
// OverlayHost placement lambda, so more than one visible at once just
// painted over the others in the same real estate. PanelDock is the single
// OverlayHost overlay that now owns that region; each panel becomes a tab.
//
// Keyboard needs no plumbing through this class at all. main.cpp's event
// loop sends every keyboard Event straight to FocusedWidget() (Widget.h's
// flat registry) -- never through OverlayHost -- so each hosted panel keeps
// calling its own TakeFocus() exactly as it did standalone, and keyboard
// dispatch keeps reaching it directly, completely bypassing this class. Only
// mouse events are PanelDock's concern: OverlayHost delivers a mouse event
// to this widget's own Box alone, and this class either handles it locally
// (a click on the tab strip itself, row 0) or forwards the untouched, still-
// global-coordinate Event straight to the active panel's own OnEvent, which
// does its own LocalMouseEvent translation against its own Box_() exactly as
// it always has -- RepositionActivePanel keeps that Box_() correct.
//
// What used to be three separate per-panel affordances are now shared, one
// level up: a single close button hides the whole dock (each panel's own
// keyboard-driven close trigger -- TerminalPanel's reserved `` C-` ``, Esc on
// AcpPanel/DebugConsolePanel -- is untouched, still firing that panel's own
// SetOnToggleRequest callback exactly as before); maximize now applies to
// whichever tab is active, not just the terminal; drag-resizing the tab-strip
// row (AcpPanel's own former `mouse->at.y == 0` carve-out, generalized)
// writes back to whichever (get, set)-percent pair the *active* tab
// registered, or does nothing for a tab that registered none (DebugConsole,
// which never had resize at all) -- deliberately no new unified percent
// setting, every existing ned/set-*-percent binding keeps its exact current
// meaning, so the dock's own height can visibly change across a tab switch.
//
// AcpPanel's own right-dock mode is a fully separate, unaffected code path
// (its own standalone OverlayHost registration, own title row/collapse/
// resize) -- only bottom-docked ACP ever joins a PanelDock. See AcpPanel.h's
// own SetDockHosted for that split.
//

#ifndef NED_UI_PANELDOCK_H
#define NED_UI_PANELDOCK_H

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class PanelDock : public Widget {
  public:
    // A panel-specific title-row button shown only while that panel's tab is
    // active (Terminal's scrollback-search icon is the one user today).
    struct TabAction {
        char32_t              icon;
        std::function<void()> onClick;
    };

    explicit PanelDock(const Theme& theme);

    // Registers one tab, in call order (left to right). `panel` must outlive
    // this PanelDock -- the usual non-owning convention this codebase's
    // Set*-after-construction wiring already follows.
    //   - titleText: the tab's full dynamic label (falls back to `label`
    //     when unset).
    //   - getPercent/setPercent: resize-drag target for this tab, supplied
    //     together or not at all -- unset leaves drag-resize a no-op while
    //     this tab is active.
    //   - extraActions: panel-specific buttons rendered only while this
    //     tab is active.
    // Returns the new tab's index, for main.cpp's toggle lambdas to close
    // over -- registration order can shift (ACP's own tab is only added at
    // all when it's bottom-docked; see AcpPanel::SetDockHosted), so a caller
    // shouldn't assume a fixed index.
    std::size_t AddPanel(std::string label, Widget& panel, std::function<std::string()> titleText = {},
                         std::function<int()> getPercent = {}, std::function<void(int)> setPercent = {},
                         std::function<std::vector<TabAction>()> extraActions = {});

    // Clamped to [0, PanelCount()). Repositions the panel's own Box_ to this
    // dock's content region and takes keyboard focus for it -- the same
    // "show and focus" pairing every one of the three toggle lambdas already
    // did by hand before this class existed.
    void SwitchTo(std::size_t index);

    [[nodiscard]] std::size_t ActiveIndex() const {
        return active_;
    }
    [[nodiscard]] Widget*     ActivePanel() const;
    [[nodiscard]] std::size_t PanelCount() const {
        return entries_.size();
    }

    // Whether the dock currently covers the full buffer-area height instead
    // of whichever percent the active tab's own getPercent reports --
    // TerminalPanel's own former Maximized(), promoted here so it applies to
    // any tab, not just the terminal.
    [[nodiscard]] bool Maximized() const {
        return maximized_;
    }

    // The active tab's own getPercent() result, or nullopt if it registered
    // none (DebugConsole today) -- what main.cpp's own placement lambda
    // reads to size this dock when not maximized.
    [[nodiscard]] std::optional<int> ActivePercent() const;

    // Fires whenever something this dock's own placement lambda depends on
    // changes out from under it (the maximize toggle) -- TerminalPanel's own
    // former SetOnLayoutChange, wired by main.cpp to re-Show this dock so
    // OverlayHost recomputes its Box from the placement lambda immediately.
    void SetOnLayoutChange(std::function<void()> onLayoutChange);

    // The shared `[x]` -- wired by main.cpp to hide this dock and hand focus
    // back to the editor.
    void SetOnCloseRequest(std::function<void()> onClose);

    // The full terminal size, refreshed by main.cpp's placement lambda every
    // Reflow -- AcpPanel::SetTerminalSize's own precedent, needed to convert
    // a resize-drag's pixel delta into a percent delta (this dock's own
    // Box_() only ever reports its own current size, not the terminal's).
    void SetTerminalSize(Size size);

    void Paint(Canvas canvas) override;
    bool OnEvent(const Event& event) override;
    // Not Focusable(): see this header's own comment on keyboard dispatch.

  private:
    struct Entry {
        std::string                             label;
        Widget*                                 panel = nullptr;
        std::function<std::string()>            titleText;
        std::function<int()>                    getPercent;
        std::function<void(int)>                setPercent;
        std::function<std::vector<TabAction>()> extraActions;
    };

    // Sets ActivePanel()'s Box_ to this dock's own Box_ minus the tab-strip
    // row -- called from both OnResize and SwitchTo, since either can leave
    // the active panel's Box_ stale (Overlay.h's own "boxes are current as
    // of the last Paint/Reflow" contract doesn't cover a tab switch that
    // isn't itself a resize).
    void RepositionActivePanel();

    // Row-0 hit testing, mirroring TerminalPanel::TitleButtonAt/AcpPanel's
    // own bracket-offset convention: tab labels first (left to right, widest
    // to fit), then the active tab's own extraActions, then `[▲]`/`[x]`
    // right-aligned. Returns true if `x` landed on a real target and invokes
    // it; false means "start a resize-drag instead" (AcpPanel's own
    // `mouse->at.y == 0` fallback, generalized to every unclaimed column).
    bool HandleTabStripClick(int x);

    void BeginResize(Point globalMouse);
    void UpdateResize(Point globalMouse);
    void EndResize();

    void OnResize(Size previous) override;

    const Theme&          theme_;
    std::vector<Entry>    entries_;
    std::size_t           active_    = 0;
    bool                  maximized_ = false;
    std::function<void()> onLayoutChange_;
    std::function<void()> onCloseRequest_;
    Size                  terminalSize_{.width = 0, .height = 0};

    bool  resizing_ = false;
    Point resizeAnchorGlobal_{.x = 0, .y = 0};
    int   resizeStartPercent_ = 0;
};

} // namespace ned::ui

#endif // NED_UI_PANELDOCK_H
