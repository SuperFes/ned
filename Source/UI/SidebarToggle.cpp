#include "SidebarToggle.h"

#include "ProjectSidebar.h"

namespace ned::ui {

namespace {

    // Same BMP "Geometric Shapes"-adjacent family as the tree's own
    // disclosure triangles / ScrollArrowButton's ▲▼ -- guaranteed
    // single-column width in any monospace font. « points at the sidebar
    // (currently open, click to close it toward the left edge); » points
    // away from it (currently closed, click to open it).
    constexpr char32_t kSidebarOpenSymbol   = U'«';
    constexpr char32_t kSidebarClosedSymbol = U'»';

} // namespace

SidebarToggle::SidebarToggle(const ox::Brush& brush) : Widget{ox::FocusPolicy::None, ox::SizePolicy::flex()}, brush_(brush) {
}

void SidebarToggle::SetSidebar(ProjectSidebar* sidebar) {
    sidebar_ = sidebar;
}

void SidebarToggle::SetSidebarRow(ox::Widget* sidebarRow) {
    sidebarRow_ = sidebarRow;
}

void SidebarToggle::paint(ox::Canvas c) {
    const bool     sidebarOpen = sidebar_ != nullptr && sidebar_->active;
    const char32_t symbol      = sidebarOpen ? kSidebarOpenSymbol : kSidebarClosedSymbol;
    for (int y = 0; y < c.size.height; ++y) {
        c[{.x = 0, .y = y}] = ox::Glyph{.symbol = symbol, .brush = brush_};
    }
}

void SidebarToggle::mouse_press(ox::Mouse mouse) {
    if (sidebar_ != nullptr && mouse.button == ox::Mouse::Button::Left) {
        sidebar_->active = !sidebar_->active;
        if (sidebarRow_ != nullptr) {
            sidebarRow_->resize(sidebarRow_->size); // see SetSidebarRow -- active alone doesn't reflow
        }
    }
}

void SidebarToggle::mouse_release(ox::Mouse) {
    if (sidebar_ != nullptr && sidebar_->IsResizing()) {
        sidebar_->EndResize();
    }
}

} // namespace ned::ui
