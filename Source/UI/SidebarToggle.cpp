#include "SidebarToggle.h"

#include "ProjectSidebar.h"
#include "Text/Utf8.h"

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

SidebarToggle::SidebarToggle(const Brush& brush) : brush_(brush) {}

void SidebarToggle::SetSidebar(ProjectSidebar* sidebar) {
    sidebar_ = sidebar;
}

void SidebarToggle::Paint(Canvas c) {
    const bool     sidebarOpen = sidebar_ != nullptr && sidebar_->active;
    const char32_t symbol      = sidebarOpen ? kSidebarOpenSymbol : kSidebarClosedSymbol;
    // U+00AB/U+00BB are 2-byte UTF-8 -- unlike EchoArea/ModeLine's ASCII-only
    // single-`char` cast, this widget needs a real UTF-8 encode.
    // text::EncodeCodepointUtf8 already exists for exactly this.
    const std::string encoded = text::EncodeCodepointUtf8(symbol);
    for (int y = 0; y < c.size().height; ++y) {
        ftxui::Cell& cell = c[{.x = 0, .y = y}];
        cell.character    = encoded;
        brush_.ApplyTo(cell);
    }
}

bool SidebarToggle::OnEvent(ftxui::Event event) {
    if (const auto mouse = LocalMouseEvent(event)) {
        if (sidebar_ != nullptr && mouse->button == ftxui::Mouse::Left && mouse->motion == ftxui::Mouse::Pressed) {
            sidebar_->active = !sidebar_->active;
            return true;
        }
        // A resize-drag on ProjectSidebar's divider (round-2 sidebar
        // follow-up) can end with the cursor over this widget if the user
        // shrinks the sidebar down past it. Every leaf widget receives every
        // mouse event regardless of position (see Widget.h's own header
        // comment), so this doesn't even need LocalMouseEvent's own bounds
        // check to still see the release -- checked directly against the
        // raw event instead, matching the old mouse_release override's own
        // "no mouse-capture" reasoning.
    }
    if (event.is_mouse() && event.mouse().motion == ftxui::Mouse::Released) {
        if (sidebar_ != nullptr && sidebar_->IsResizing()) {
            sidebar_->EndResize();
            return true;
        }
    }
    return false;
}

} // namespace ned::ui
