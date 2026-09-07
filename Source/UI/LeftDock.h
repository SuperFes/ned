//
// Unified-left-dock follow-up (see ROADMAP.md's "Persistent left-side glyph
// rail for toggling panels" entry): the single widget the left screen slot
// is meant to hold, replacing ProjectSidebar and VcsPanel each owning their
// own border/width/collapse chrome and being kept "mutually exclusive" only
// by ad hoc coordination in BufferView.cpp's toggle handlers. This is step 1
// of that entry's migration order -- a standalone widget, exercised against
// small fake content widgets, with no main.cpp/WindowManager wiring yet and
// ProjectSidebar/VcsPanel not yet retrofitted to be hosted by it.
//
// Shape: a fixed-width glyph "rail" (kRailWidth columns, one row per
// registered panel, VS Code activity-bar style) always painted at this
// widget's own left edge, beside a bordered content region showing whichever
// panel is currently active. Registration is PanelDock's own AddPanel shape
// (a stable id, not a rail position -- `content` must outlive this LeftDock,
// the usual non-owning Set*-after-construction convention). Unlike PanelDock
// (a horizontal tab strip a caller explicitly shows/hides), this dock also
// owns ProjectSidebar's own width/collapse-to-a-strip contract, since it's
// replacing that widget's chrome, not just PanelDock's tab-switching.
//
// Collapse is rail-driven, not divider-driven: clicking the *already active*
// panel's glyph while expanded collapses (VS Code's own convention);
// clicking any glyph while collapsed expands and switches to it in one
// click. This sidesteps the divider-double-click-collapse inconsistency
// ProjectSidebar/VcsPanel/AcpPanel each had to independently patch (see
// git log --grep=divider-double-click-collapse-gap) -- there is no
// double-click state to get out of sync here, because collapse was never a
// divider gesture to begin with. The right border column keeps its
// established meaning as the resize-drag divider (ProjectSidebar's own
// BeginResize/UpdateResize/EndResize shape) while expanded.
//
// Keyboard needs no plumbing through this class, PanelDock's own precedent:
// main.cpp's event loop sends every keyboard Event straight to
// FocusedWidget() (Widget.h's flat registry), never through this dock, so
// a hosted content widget keeps calling its own TakeFocus() exactly as it
// would standalone. Only mouse events are this class's concern -- handled
// locally (rail glyph clicks, the resize divider) or forwarded unmodified
// (still global/absolute coordinates) to the active panel's own OnEvent,
// which does its own LocalMouseEvent translation against its own Box_()
// (kept current by RepositionActiveContent, called from both Paint and
// SwitchTo/OnResize) exactly as it does standalone -- PanelDock's own
// "boxes are current as of the last Paint/Reflow" contract, restated here.
//

#ifndef NED_UI_LEFTDOCK_H
#define NED_UI_LEFTDOCK_H

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class LeftDock : public Widget {
  public:
    explicit LeftDock(const Theme& theme);

    // Registers one panel, in call order (top to bottom in the rail).
    // `content` must outlive this LeftDock. `glyph` is the single codepoint
    // shown in its own rail row; `name` is the content region's border
    // title while this panel is active. The first registered panel becomes
    // active by construction (SwitchTo/ActivePanel() default to id 0).
    // Returns a stable id (PanelDock::AddPanel's own precedent) -- not a
    // rail row position, so a caller should hold onto it rather than assume
    // it equals registration order forever (no RemovePanel exists yet;
    // unlike PanelDock's tabs, a registered left panel isn't expected to
    // come and go at runtime the way a terminal tab does).
    std::size_t AddPanel(char32_t glyph, std::string name, Widget& content);

    [[nodiscard]] std::size_t ActivePanel() const {
        return active_;
    }
    [[nodiscard]] Widget* ActiveContent() const;
    [[nodiscard]] std::size_t PanelCount() const {
        return entries_.size();
    }

    // Programmatic switch (session restore) -- no commit callback, mirrors
    // ProjectSidebar::SetCollapsed's own silent/programmatic vs.
    // CommitCollapsed's deliberate-user-action split, applied here to which
    // panel is active. A no-op if `id` isn't registered.
    void SwitchTo(std::size_t id);

    // A deliberate user click on a rail glyph: SwitchTo(id) plus, if set,
    // onActivePanelCommitted_. A no-op if `id` isn't registered.
    void CommitSwitchTo(std::size_t id);

    // Current desired total width in columns (rail + content region),
    // ProjectSidebar::Width()'s own contract: reports kRailWidth while
    // Collapsed() (width_ itself untouched, so expanding restores the
    // previous width exactly).
    [[nodiscard]] int Width() const;
    void              SetWidth(int width); // clamped to a sane minimum, same as a resize drag
    [[nodiscard]] int ExpandedWidth() const; // width_ itself, not masked by Collapsed()

    [[nodiscard]] bool Collapsed() const;
    void               SetCollapsed(bool collapsed); // programmatic; no commit callback
    void               ToggleCollapsed();             // deliberate; commits

    // ProjectSidebar::TakeKeyboardFocus/ReturnFocus's own pairing, promoted
    // here now that collapse lives on this widget instead: a caller driving
    // a hosted content widget's own keyboard-focus entry point (e.g.
    // BufferView's focus-project-sidebar handling) calls this first --
    // remembers whether this dock was collapsed, then expands it
    // (programmatically, like SetCollapsed -- a quick keyboard visit
    // shouldn't overwrite the remembered visibility preference) -- and
    // calls the content widget's own TakeFocus() itself. Pair with
    // NoteFocusReturned(), called from that same content widget's own
    // focus-return path (wired at the call site, ProjectSidebar's own
    // policy-at-the-wiring-site precedent), which re-collapses if this dock
    // was collapsed at the matching PrepareForKeyboardFocus call -- a
    // dock summoned by keyboard while hidden goes back to hidden the moment
    // focus leaves. Safe to call NoteFocusReturned() even when no
    // PrepareForKeyboardFocus is pending (a plain no-op).
    void PrepareForKeyboardFocus();
    void NoteFocusReturned();

    [[nodiscard]] bool IsResizing() const;
    // Called by whichever widget's OnEvent sees the matching mouse-move/
    // release during a resize -- ProjectSidebar's own cross-widget
    // cooperation shape, for when a growing drag carries the cursor past
    // this widget's own bounds (there's no mouse-capture concept; see
    // Widget.h's own header comment). globalMouseX is raw/absolute, no
    // translation needed.
    void UpdateResize(int globalMouseX);
    void EndResize();

    // Called with the new width when a divider drag ends having actually
    // moved it. Unset (default) is a safe no-op; a real caller wires this
    // to editor::SetVariable, ProjectSidebar::SetOnWidthCommitted's own
    // policy-at-the-wiring-site precedent (kept out of this widget so
    // unit-test drags never touch real persisted state).
    void SetOnWidthCommitted(std::function<void(int)> handler);

    // Called with the new collapse state on a deliberate rail-glyph click
    // that changes it. Unset is a safe no-op.
    void SetOnCollapseCommitted(std::function<void(bool)> handler);

    // Called with the newly active panel's id on CommitSwitchTo. Unset is a
    // safe no-op; distinct from collapse/width commits since switching
    // panels is a much more frequent action and a caller may reasonably
    // choose not to persist every click.
    void SetOnActivePanelCommitted(std::function<void(std::size_t)> handler);

    void Paint(Canvas c) override;
    bool OnEvent(const Event& event) override;
    void OnResize(Size previous) override;
    // Not Focusable(): see this header's own comment on keyboard dispatch.

  private:
    struct Entry {
        std::size_t id = 0;
        char32_t    glyph  = U' ';
        std::string name;
        Widget*     content = nullptr;
    };

    [[nodiscard]] Entry*       FindEntry(std::size_t id);
    [[nodiscard]] const Entry* FindEntry(std::size_t id) const;

    // The absolute box of the bordered content region (rail excluded), and
    // the interior box one row/column inside that border -- what the active
    // panel actually paints into. Both empty/degenerate while Collapsed()
    // or PanelCount() == 0; callers guard on those first rather than relying
    // on a degenerate Box being harmless.
    [[nodiscard]] Box ContentBox() const;
    [[nodiscard]] Box ContentInteriorBox() const;

    // Points the active entry's content widget's own Box_() at
    // ContentInteriorBox() -- called from Paint, SwitchTo, and OnResize, so
    // it's never stale when a mouse event needs to forward into it (mirrors
    // PanelDock::RepositionActivePanel's own doc comment on why this can't
    // just be computed once at construction).
    void RepositionActiveContent();

    void BeginResize(int globalMouseX);
    void CommitCollapsed(bool collapsed); // SetCollapsed + onCollapseCommitted_, ProjectSidebar's own split

    const Theme&           theme_;
    std::vector<Entry>     entries_;
    std::size_t            nextId_ = 0;
    std::size_t            active_ = 0; // an Entry::id, not a rail row -- see ActivePanel's doc comment

    int  width_     = 30; // total width including the rail -- see Width()
    bool collapsed_ = false;

    // See PrepareForKeyboardFocus/NoteFocusReturned.
    bool collapseOnFocusReturn_ = false;

    bool resizing_            = false;
    int  resizeAnchorGlobalX_ = 0;
    int  resizeAnchorWidth_   = 0;
    int  resizeStartWidth_    = 0;

    std::function<void(int)>         onWidthCommitted_;
    std::function<void(bool)>        onCollapseCommitted_;
    std::function<void(std::size_t)> onActivePanelCommitted_;
};

} // namespace ned::ui

#endif // NED_UI_LEFTDOCK_H
