//
// A persistent, always-visible single-column button that toggles
// ProjectSidebar's Widget::active flag (project-sidebar follow-up, round-2
// feedback: a mouse-clickable show/hide, distinct from the C-c C-p
// keybinding). Deliberately a separate sibling widget rather than living
// inside ProjectSidebar itself -- once the sidebar's own .active flag goes
// false it stops being included in the layout at all, so a toggle affordance
// drawn *inside* it would vanish along with it, leaving nothing on screen to
// click to bring it back.
//

#ifndef NED_UI_SIDEBARTOGGLE_H
#define NED_UI_SIDEBARTOGGLE_H

#include "Theme.h"
#include "Widget.h"

namespace ned::ui {

class ProjectSidebar;

class SidebarToggle : public Widget {
  public:
    // brush must outlive this widget (the usual convention). sidebar starts
    // unset (nullptr) -- wired in after construction via SetSidebar, the
    // same "connect after the widget tree exists" pattern
    // BufferView::SetScrollBar/SetProjectSidebar already use: main.cpp
    // builds this widget and ProjectSidebar as siblings in the same
    // composition, so neither can hold a reference to the other at
    // construction time.
    explicit SidebarToggle(const Brush& brush);

    void SetSidebar(ProjectSidebar* sidebar);

    void Paint(Canvas c) override;
    bool OnEvent(ftxui::Event event) override;

  private:
    const Brush&    brush_;
    ProjectSidebar* sidebar_ = nullptr;
};

} // namespace ned::ui

#endif // NED_UI_SIDEBARTOGGLE_H
