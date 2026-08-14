//
// A persistent, always-visible single-column button that toggles
// ProjectSidebar's ox::Widget::active flag (project-sidebar follow-up,
// round-2 feedback: a mouse-clickable show/hide, distinct from the
// C-c C-p keybinding). Deliberately a separate sibling widget rather than
// living inside ProjectSidebar itself -- once the sidebar's own .active flag
// goes false it stops being laid out at all, so a toggle affordance drawn
// *inside* it would vanish along with it, leaving nothing on screen to
// click to bring it back.
//

#ifndef NED_UI_SIDEBARTOGGLE_H
#define NED_UI_SIDEBARTOGGLE_H

#include <ox/ox.hpp>

namespace ned::ui {

class ProjectSidebar;

class SidebarToggle : public ox::Widget {
  public:
    // brush must outlive this widget (the usual convention). sidebar starts
    // unset (nullptr) -- wired in after construction via SetSidebar, the
    // same "connect after the widget tree exists" pattern
    // BufferView::SetScrollBar/SetProjectSidebar already use: main.cpp
    // builds this widget and ProjectSidebar as siblings in the same
    // Row{...} initializer, so neither can hold a reference to the other at
    // construction time.
    explicit SidebarToggle(const ox::Brush& brush);

    void SetSidebar(ProjectSidebar* sidebar);

    // The ox::Row containing ProjectSidebar -- flipping .active alone does
    // not make TermOx recompute that Row's child widths (layout only
    // recomputes on an actual terminal resize event, never on a plain field
    // write; see BufferView::SetSidebarRow's longer comment for the full
    // explanation), so mouse_press calls sidebarRow->resize(...) itself
    // right after toggling. nullptr (the default) means toggling only flips
    // the flag without reflowing, same as leaving this unset on BufferView.
    void SetSidebarRow(ox::Widget* sidebarRow);

    void paint(ox::Canvas c) override;
    void mouse_press(ox::Mouse mouse) override;

    // A resize-drag on ProjectSidebar's divider (round-2 sidebar follow-up)
    // can end with the cursor over this widget if the user shrinks the
    // sidebar down past it -- no mouse-capture in TermOx means the release
    // is simply hit-tested to wherever the cursor happens to be. Without
    // this, that session would never see a matching release and IsResizing()
    // would stay stuck true, same class of problem ScrollArrowButton's own
    // mouse_leave override exists to guard against for its repeat timer.
    void mouse_release(ox::Mouse mouse) override;

  private:
    const ox::Brush& brush_;
    ProjectSidebar*  sidebar_    = nullptr;
    ox::Widget*      sidebarRow_ = nullptr;
};

} // namespace ned::ui

#endif // NED_UI_SIDEBARTOGGLE_H
